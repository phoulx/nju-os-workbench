#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>
#include "fat32.h"

// 全局调试开关
static bool debug_enabled = true;
#define DEBUG_PRINT(...) do { if (debug_enabled) printf(__VA_ARGS__); } while(0)


#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_LONG_NAME_MASK (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | \
                            ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)
#define LAST_LONG_ENTRY 0x40


struct fat32lfn {
    u8  LDIR_Ord;
    u16  LDIR_Name1[5];
    u8  LDIR_Attr;
    u8  LDIR_Type;
    u8  LDIR_Chksum;
    u16 LDIR_Name2[6];
    u16 LDIR_FstClusLO;
    u16 LDIR_Name3[2];    
} __attribute__((packed));


#define BMP_SIGNATURE    0x4D42

struct bmp_file {
    char name[256];
    u32 first_cluster;
    u32 file_size;
    u32 cluster_count;
};



struct fat32hdr *hdr;
u8 *disk_base;
void *mmap_disk(const char *fname);
void full_scan();
void handle(u8 *entry, int len);

int first_data_sector;
int total_clusters;

int entry_size = sizeof(struct fat32dent);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s fs-image\n", argv[0]);
        exit(1);
    }

    setbuf(stdout, NULL);

    assert(sizeof(struct fat32hdr) == 512);
    assert(sizeof(struct fat32dent) == 32);

    hdr = mmap_disk(argv[1]);


    first_data_sector = hdr->BPB_RsvdSecCnt + hdr->BPB_NumFATs * hdr->BPB_FATSz32;
    total_clusters = (hdr->BPB_TotSec32 - first_data_sector) / hdr->BPB_SecPerClus;

    full_scan();



    munmap(hdr, hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec);
    
    return 0;
}


void *mmap_disk(const char *fname) {
    int fd = open(fname, O_RDWR);

    if (fd < 0) {
        goto release;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        goto release;
    }

    struct fat32hdr *hdr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
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


bool is_dirent_basic(struct fat32dent *dent) {
    // 根据FAT32规范，第一个字节为0x00表示当前目录项及之后的目录项均为空
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
    // dot/dotdot目录和已删除目录项，提前判断，因为簇号可能为0
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

bool is_dirent_long(struct fat32lfn *lfn) {
    // 长文件名最多255字符，而每个条目可存13字符，所以条目序号为1-20
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

bool is_dirent_cluster_possibly(u8 *cluster_start) {
    // 因为目录条目（dirent）一定是从簇的开头开始的，所以仅检查第一个条目即可，如果不是则忽略该簇
    // 如果簇的前32字节是目录项（包括文件、目录、长文件名、已删除项等），则认为该簇包含目录项
    // 粗略筛选，目的是快速排除，可以多但不能少
    return is_dirent_basic((struct fat32dent *)cluster_start) \
        || is_dirent_long((struct fat32lfn *)cluster_start);
}

// Copy from FAT32 spec
unsigned char calc_checksum(const unsigned char *pFcbName) {
    unsigned char sum = 0;
    for (int i = 11; i != 0; i--) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *pFcbName++;
    }
    return sum;
}


bool matched(struct fat32lfn *head, struct fat32lfn *tail) {
    // 仅根据checksum判断是否匹配
    assert(is_dirent_long(head));
    if (is_dirent_long(tail)) {
        return head->LDIR_Chksum == tail->LDIR_Chksum;
    }
    if (is_dirent_basic((struct fat32dent *)tail)) {
        return head->LDIR_Chksum == calc_checksum(((struct fat32dent *)tail)->DIR_Name);
    }
    assert(0);
}

struct entry_part {
    void *entry;
    int len;
};

struct waiting_entries {
    struct entry_part heads[10];
    int head_count;
    struct entry_part tails[10];
    int tail_count;
};

// 条目跨簇时，等待匹配（因为数据量小，直接用数组即可）
struct waiting_entries waiting = {
    .head_count = 0,
    .tail_count = 0
};

void match_entries() {
    u8 *combined;
    for (int i = 0; i < waiting.head_count; i++) {
        u8 *head = waiting.heads[i].entry;
        int head_len = waiting.heads[i].len;
        for (int j = 0; j < waiting.tail_count; j++) {
            if (waiting.tails[j].entry == NULL) {
                continue;
            }
            u8 *tail = waiting.tails[j].entry;
            int tail_len = waiting.tails[j].len;
            if (matched((struct fat32lfn *)head, (struct fat32lfn *)tail)) {
                combined = malloc(head_len * entry_size + tail_len * entry_size);
                memcpy(combined, head, head_len * entry_size);
                memcpy(combined + head_len * entry_size, tail, tail_len * entry_size);
                handle(combined, head_len + tail_len);
                waiting.tails[j].entry = NULL;
                free(combined);
            }
        }
    }

    DEBUG_PRINT("\nUnmatched tails:\n");
    for (int i = 0; i < waiting.tail_count; i++) {
        if (waiting.tails[i].entry != NULL && waiting.tails[i].len == 1) {
            handle(waiting.tails[i].entry, waiting.tails[i].len);
        }
    }
}

void search_cluster(u8 *cluster_start, int clus_num) {
    // 遍历目标簇，找到所有bmp文件的目录项（包括长文件名条目）
    u8 *p = cluster_start;

    // 先处理cluster开头处跨簇的情况
    struct fat32lfn *lfn = (struct fat32lfn *)p;
    // 1. 开头为lfn条目且非首个
    if (is_dirent_long(lfn) && ((lfn->LDIR_Ord & LAST_LONG_ENTRY) == 0)) {
        DEBUG_PRINT("  is_lfn ent\n");
        waiting.tails[waiting.tail_count++] = (struct entry_part){
            .entry = p,
            .len = lfn->LDIR_Ord + 1
        };
        p += entry_size * (lfn->LDIR_Ord+1);
    }
    // 2. 开头为短文件名条目，也有可能是跨簇的长文件名
    else if (is_dirent_basic((struct fat32dent *)p)) {
        DEBUG_PRINT("  is_basic ent\n");
        waiting.tails[waiting.tail_count++] = (struct entry_part){
            .entry = p,
            .len = 1
        };
        p += entry_size;
    }

    DEBUG_PRINT("  Cluster %d: offset 0x%lx\n", clus_num, p - disk_base);

    // 遍历cluster中所有条目
    u8 *curr = p;
    int curr_entries = 0;
    while (p < cluster_start + hdr->BPB_BytsPerSec * hdr->BPB_SecPerClus) {
        if (is_dirent_basic((struct fat32dent *)p)) {
            handle(curr, curr_entries+1);
            p += entry_size;
            curr = p;
            curr_entries = 0;
        }
        else if (is_dirent_long((struct fat32lfn *)p)) {
            p += entry_size;
            curr_entries++;
        }
        else {
            DEBUG_PRINT("  break at offset 0x%lx\n", p - disk_base);
            // 非目录项，说明该簇不含目录项，或者已遍历结束
            break;
        }
    }
    if (curr != p) {
        // 该簇最后一个目录项为长文件名，等待匹配
        waiting.heads[waiting.head_count++] = (struct entry_part){
            .entry = curr,
            .len = curr_entries
        };
    }

}


u8* first_byte_of_cluster(int clus_num) {
    int first_sector_of_cluster = first_data_sector + (clus_num - 2) * hdr->BPB_SecPerClus;
    u8 *cluster_start = disk_base + first_sector_of_cluster * hdr->BPB_BytsPerSec;
    return cluster_start;
}

void full_scan() {
    disk_base = (u8 *)hdr;
    for (int clus_num = 2; clus_num < total_clusters + 2; clus_num++) {
        u8 *cluster_start = first_byte_of_cluster(clus_num);
        if (is_dirent_cluster_possibly(cluster_start)) {
            DEBUG_PRINT("\nCluster %d:\n", clus_num);
            search_cluster(cluster_start, clus_num);
        }

    }

    // 处理跨簇的情况
    DEBUG_PRINT("\nMatching entries with checksum...\n");
    for (int i = 0; i < waiting.head_count; i++) {
        DEBUG_PRINT("  Head %d: offset 0x%lx, len %d\n", i, 
            ((u8 *)waiting.heads[i].entry - disk_base), waiting.heads[i].len);
    }
    for (int i = 0; i < waiting.tail_count; i++) {
        DEBUG_PRINT("  Tail %d: offset 0x%lx, len %d\n", i,
            ((u8 *)waiting.tails[i].entry - disk_base), waiting.tails[i].len);
    }
    match_entries();

}


void extract_name_from_lfn(struct fat32lfn *lfn, char *name) {
    // 因为文件名仅包含ASCII字符，所以直接赋值即可（原16位截断保留低8位）
    for (int i = 0; i < 5; i++) {
        name[i] = lfn->LDIR_Name1[i];
    }
    for (int i = 0; i < 6; i++) {
        name[i+5] = lfn->LDIR_Name2[i];
    }
    for (int i = 0; i < 2; i++) {
        name[i+11] = lfn->LDIR_Name3[i];
    }
    name[13] = '\0';
}

// 解析长文件名，找到对应簇号和文件大小，写入
void handle(u8 *entry_start, int len) {
    DEBUG_PRINT("Entry at offset 0x%lx, len %d\n", entry_start - disk_base, len);
    struct fat32dent *basic_entry = (struct fat32dent *)entry_start + (len - 1);
    char *short_name = (char *)basic_entry->DIR_Name;
    DEBUG_PRINT("  Short name: %s\n", short_name);
    int clus_num = basic_entry->DIR_FstClusHI << 16 | basic_entry->DIR_FstClusLO;
    if (clus_num == 0 || clus_num > total_clusters + 1 \
        || basic_entry->DIR_Attr & ATTR_DIRECTORY \
        || basic_entry->DIR_Name[0] == 0xE5) {
        DEBUG_PRINT("  Invalid file, skipped\n");
        return;
    }
    if (*(u16 *)first_byte_of_cluster(clus_num) != BMP_SIGNATURE) {
        DEBUG_PRINT("  Not a BMP file\n");
        return;
    }

    char long_name[255];
    long_name[0] = '\0';
    for (int i = len - 2; i >= 0; i--) {
        struct fat32lfn *lfn = (struct fat32lfn *)entry_start + i;
        // Decode Unicode characters (16-bit) to UTF-8
        char part_name[14];
        extract_name_from_lfn(lfn, part_name);
        strncat(long_name, part_name, 13);
    }
    DEBUG_PRINT("  Long name: %s\n", long_name);
    

    int file_size = basic_entry->DIR_FileSize;
    DEBUG_PRINT("  Cluster: %d, Size: %d\n", clus_num, file_size);
    
}