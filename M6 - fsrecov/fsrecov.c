#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fat32.h"

// 调试打印开关
static bool debug_enabled = true;
#define DEBUG_PRINT(...) do { if (debug_enabled) printf(__VA_ARGS__); } while(0)

#define BMP_SIGNATURE 0x4D42 // 'BM'

#define TEMP_FILE_TEMPLATE "/tmp/fsrecov_XXXXXX"

#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define LAST_LONG_ENTRY 0x40

// ====== 数据结构定义 ======
struct fat32lfn {
    u8  LDIR_Ord;
    u16 LDIR_Name1[5];
    u8  LDIR_Attr;
    u8  LDIR_Type;
    u8  LDIR_Chksum;
    u16 LDIR_Name2[6];
    u16 LDIR_FstClusLO;
    u16 LDIR_Name3[2];
} __attribute__((packed));

struct output_file {
    char* name;
    u8* start;
    u32 size;
};

struct entry_part {
    void* entry;
    int len;
};

struct waiting_entries {
    struct entry_part heads[10], tails[10];
    int head_count, tail_count;
};

// ====== 全局变量 ======
struct fat32hdr* hdr;
u8* disk_base;
u8* disk_end;
int first_data_sector;
int total_clusters;
int entry_size = sizeof(struct fat32dent);
struct waiting_entries waiting = {0};  // 跨簇待匹配的目录项

// ====== 函数前向声明 ======
void* mmap_disk(const char*);
void full_scan(void);
void handle(u8* entry, int len);


int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s fs-image\n", argv[0]);
        exit(1);
    }

    setbuf(stdout, NULL);

    assert(sizeof(struct fat32hdr) == 512);
    assert(sizeof(struct fat32dent) == 32);

    disk_base = mmap_disk(argv[1]);
    hdr = (struct fat32hdr*)disk_base;

    disk_end = disk_base + hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec - 1;
    first_data_sector = hdr->BPB_RsvdSecCnt + hdr->BPB_NumFATs * hdr->BPB_FATSz32;
    total_clusters = (hdr->BPB_TotSec32 - first_data_sector) / hdr->BPB_SecPerClus;

    full_scan();

    munmap(hdr, hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec);

    return 0;
}


void* mmap_disk(const char* fname)
{
    int fd = open(fname, O_RDWR);

    if (fd < 0) {
        goto release;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        goto release;
    }

    struct fat32hdr* hdr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (hdr == MAP_FAILED) {
        goto release;
    }

    close(fd);

    assert(hdr->Signature_word == 0xaa55);
    assert(hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec == size);

    return hdr;

release:
    perror("map disk");
    if (fd > 0) {
        close(fd);
    }
    exit(1);
}


bool is_dirent_basic(struct fat32dent* dent)
{   // 根据FAT32规范，第一个字节为0x00表示当前目录项及之后的目录项均为空
    if (dent->DIR_Name[0] == 0x00) {
        return false;
    }
    // 如果需要更严格，可以加上8.3文件名的合法字符检查

    // Attr最高2位和NTRes都是保留位，应该为0
    if ((dent->DIR_Attr & 0b11000000) != 0) {
        return false;
    }
    if (dent->DIR_NTRes != 0) {
        return false;
    }
    // dot、dotdot目录和已删除目录项，提前判断，因为簇号可能为0
    if (dent->DIR_Name[0] == '.' || dent->DIR_Name[0] == 0xE5) {
        return true;
    }
    // 簇号
    int clus_num = dent->DIR_FstClusHI << 16 | dent->DIR_FstClusLO;
    if (clus_num < 2 || clus_num > total_clusters + 1) {
        return false;
    }
    // 认为文件大小不超过64MB（仅对本实验有效）
    if (dent->DIR_FileSize > 64 * 1024 * 1024) {
        return false;
    }
    return true;
}


bool is_dirent_long(struct fat32lfn* lfn)
{   // 长文件名最多255字符，而每个条目可存13字符，所以条目序号为1-20
    int ord = lfn->LDIR_Ord & ~LAST_LONG_ENTRY;
    if (ord == 0 || ord > 20) {
        return false;
    }
    if (lfn->LDIR_Attr != ATTR_LONG_NAME) {
        return false;
    }
    if (lfn->LDIR_Type != 0) {
        return false;
    }
    if (lfn->LDIR_FstClusLO != 0) {
        return false;
    }
    return true;
}


bool is_dirent_cluster_possibly(u8* cluster_start)
{   // 因为目录条目（dirent）一定是从簇的开头开始的，所以仅检查第一个条目即可，如果不是则忽略该簇
    // 如果簇的前32字节是目录项（包括文件、目录、长文件名、已删除项等），则认为该簇包含目录项
    // 粗略筛选，目的是快速排除，可以多但不能少
    return is_dirent_basic((struct fat32dent*)cluster_start)
        || is_dirent_long((struct fat32lfn*)cluster_start);
}


u8* first_byte_ptr_of_cluster(int clus_num)
{   // FAT32 spec
    int first_sector_of_cluster = first_data_sector + (clus_num - 2) * hdr->BPB_SecPerClus;
    u8* cluster_start = disk_base + first_sector_of_cluster * hdr->BPB_BytsPerSec;
    return cluster_start;
}


u8 calc_checksum(const u8* pFcbName)
{   // FAT32 spec
    u8 sum = 0;
    for (int i = 11; i != 0; i--) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *pFcbName++;
    }
    return sum;
}


void extract_name_from_lfn(struct fat32lfn* lfn, char* name)
{   // 因为文件名仅包含ASCII字符，所以直接赋值即可（原16位unicode码截断保留低8位）
    for (int i = 0; i < 5; i++) {
        name[i] = lfn->LDIR_Name1[i];
    }
    for (int i = 0; i < 6; i++) {
        name[i + 5] = lfn->LDIR_Name2[i];
    }
    for (int i = 0; i < 2; i++) {
        name[i + 11] = lfn->LDIR_Name3[i];
    }
    name[13] = '\0';
}


bool matched(struct fat32lfn* head, struct fat32lfn* tail)
{
    // 本实验仅根据checksum匹配跨簇条目
    assert(is_dirent_long(head));
    if (is_dirent_long(tail)) {
        return head->LDIR_Chksum == tail->LDIR_Chksum;
    }
    if (is_dirent_basic((struct fat32dent*)tail)) {
        return head->LDIR_Chksum == calc_checksum(((struct fat32dent*)tail)->DIR_Name);
    }
    assert(0);
}


void match_entries()
{   // 将等待匹配的head和tail组合，匹配成功则处理
    u8* combined;
    for (int i = 0; i < waiting.head_count; i++) {
        u8* head = waiting.heads[i].entry;
        int head_len = waiting.heads[i].len;
        for (int j = 0; j < waiting.tail_count; j++) {
            if (waiting.tails[j].entry == NULL) {
                continue;
            }
            u8* tail = waiting.tails[j].entry;
            int tail_len = waiting.tails[j].len;
            // 匹配成功，拷贝到堆内存合并，再处理
            if (matched((struct fat32lfn*)head, (struct fat32lfn*)tail)) {
                DEBUG_PRINT("Matched: head 0x%tx - tail 0x%tx\n", head - disk_base, tail - disk_base);
                combined = malloc(head_len * entry_size + tail_len * entry_size);
                memcpy(combined, head, head_len * entry_size);
                memcpy(combined + head_len * entry_size, tail, tail_len * entry_size);
                handle(combined, head_len + tail_len);
                waiting.tails[j].entry = NULL;
                free(combined);
            }
        }
    }

    // 未匹配的tail，可能是独立的短文件名条目（虽然示例img中没有）
    for (int i = 0; i < waiting.tail_count; i++) {
        if (waiting.tails[i].entry != NULL && waiting.tails[i].len == 1) {
            handle(waiting.tails[i].entry, waiting.tails[i].len);
        }
    }
}


void outprint(struct output_file file)
{
    // 写入临时文件
    char file_path[] = TEMP_FILE_TEMPLATE;
    int fd = mkstemp(file_path);
    assert(fd >= 0);    
    ssize_t bytes_written = write(fd, file.start, file.size);
    close(fd);

    // 计算sha1
    char cmd[256];
    snprintf(cmd, 256, "sha1sum %s", file_path);
    FILE* fp = popen(cmd, "r");
    char sha1[64];
    fscanf(fp, "%s", sha1);
    pclose(fp);

    printf("%s  %s\n", sha1, file.name);

    unlink(file_path);
}


void handle(u8* entry_start, int len)
{   // 对文件条目的最终处理函数

    struct fat32dent* basic_entry = (struct fat32dent*)entry_start + (len - 1);
    char* short_name = (char*)basic_entry->DIR_Name;
    int clus_num = basic_entry->DIR_FstClusHI << 16 | basic_entry->DIR_FstClusLO;

    if (clus_num == 0 || clus_num > total_clusters + 1
        || basic_entry->DIR_Attr & ATTR_DIRECTORY
        || basic_entry->DIR_Name[0] == 0xE5) {
        return;
    }
    if (*(u16*)first_byte_ptr_of_cluster(clus_num) != BMP_SIGNATURE) {
        return;
    }

    // 解析长文件名
    char long_name[256] = {0};
    for (int i = len - 2; i >= 0; i--) {
        struct fat32lfn* lfn = (struct fat32lfn*)entry_start + i;
        char part_name[14];
        extract_name_from_lfn(lfn, part_name);
        strncat(long_name, part_name, 14);
    }
    if (long_name[0] == '\0') {
        strncpy(long_name, short_name, 13);
    }

    // 文件大小
    int file_size = basic_entry->DIR_FileSize;
    u8* bmp_start = first_byte_ptr_of_cluster(clus_num);
    if (bmp_start + file_size > disk_end) {
        file_size = disk_end - bmp_start + 1;
    }

    struct output_file file = {
        .name = long_name,
        .start = bmp_start,
        .size = file_size
    };
    outprint(file);
}


void search_cluster(u8* cluster_start, int clus_num)
{
    // 遍历目标簇，找到所有bmp文件的目录项（包括长文件名条目）
    u8* p = cluster_start;

    // 先处理cluster开头处的条目可能跨簇的情况
    struct fat32lfn* lfn = (struct fat32lfn*)p;
    // 1. 开头为lfn条目且非首个，一定是跨簇的长文件名，加入tails等待匹配
    if (is_dirent_long(lfn) && ((lfn->LDIR_Ord & LAST_LONG_ENTRY) == 0)) {
        waiting.tails[waiting.tail_count++] = (struct entry_part) {
            .entry = p,
            .len = lfn->LDIR_Ord + 1
        };
        p += entry_size * (lfn->LDIR_Ord + 1);
    }
    // 2. 开头为短文件名条目，也有可能是跨簇的长文件名，加入tails等待匹配
    else if (is_dirent_basic((struct fat32dent*)p)) {
        waiting.tails[waiting.tail_count++] = (struct entry_part) {
            .entry = p,
            .len = 1
        };
        p += entry_size;
    }

    DEBUG_PRINT("  Cluster %d: offset 0x%tx\n", clus_num, p - disk_base);

    u8* curr = p;
    int curr_entries = 0;
    // 遍历cluster中所有条目
    while (p < cluster_start + hdr->BPB_BytsPerSec * hdr->BPB_SecPerClus) {
        if (is_dirent_basic((struct fat32dent*)p)) {
        // 遇到短文件名，表示条目结束，处理当前条目
            handle(curr, curr_entries + 1);
            p += entry_size;
            curr = p;
            curr_entries = 0;
        } else if (is_dirent_long((struct fat32lfn*)p)) {
        // 遇到长文件名，继续遍历
            p += entry_size;
            curr_entries++;
        } else {
            // 非目录项，说明该簇不含目录项，或者已遍历结束
            DEBUG_PRINT("  break at offset 0x%tx\n", p - disk_base);
            break;
        }
    }
    if (curr != p) {
        // 该簇最后一个目录项为长文件名，未结束，加入heads等待匹配
        waiting.heads[waiting.head_count++] = (struct entry_part) {
            .entry = curr,
            .len = curr_entries
        };
    }
}


void full_scan()
{
    for (int clus_num = 2; clus_num < total_clusters + 2; clus_num++) {
        u8* cluster_start = first_byte_ptr_of_cluster(clus_num);
        if (is_dirent_cluster_possibly(cluster_start)) {
            search_cluster(cluster_start, clus_num);
        }
    }
    // 处理跨簇的情况
    match_entries();
}
