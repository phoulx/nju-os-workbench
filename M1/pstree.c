#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>

#define MAX_LEN 128

bool show_pid = false;

// child-sibling representation （使用二叉树来表示多叉树的一种方法）
typedef struct proc {
    int pid;
    char name[MAX_LEN];
    int ppid;
    struct proc *child;
    struct proc *sibling;
} proc_t;

// DFS遍历，考虑到父进程很可能比较靠前，所以先找兄弟节点，再找子节点
proc_t *find_proc(proc_t *node, int pid) {
    if (node == NULL) {
        return NULL;
    }
    if (node->pid == pid) {
        return node;
    }
    proc_t *proc = find_proc(node->sibling, pid);
    if (proc) {
        return proc;
    }
    return find_proc(node->child, pid);
}

proc_t *create_proc(int pid, char *name, int ppid) {
    proc_t *proc = malloc(sizeof(proc_t));
    if (proc == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    proc->pid = pid;
    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->ppid = ppid;
    proc->child = NULL;
    proc->sibling = NULL;
    return proc;
}

void free_tree(proc_t *node) {
    if (node == NULL) {
        return;
    }
    free_tree(node->child);
    free_tree(node->sibling);
    free(node);
}

// 打印进程树，level表示当前层级，prefix表示每行前面的空格和连接线
void print_tree(proc_t *node, int level, char* prefix) {
    if (node == NULL) {
        return;
    }

    printf("%s", prefix);
    if (level > 0) {
        printf(node->sibling == NULL ? "└──── " : "├──── ");
    }
    if (show_pid)
        printf("%s(%d)\n", node->name, node->pid);
    else
        printf("%s\n", node->name);

    char new_prefix[MAX_LEN];
    strncpy(new_prefix, prefix, sizeof(new_prefix) - 1);
    if (level > 0) {
        strcat(new_prefix, node->sibling == NULL ? "      " : "│     ");
    }

    print_tree(node->child, level + 1, new_prefix);
    print_tree(node->sibling, level, prefix);
}

// 解析命令行参数，-p显示pid，-n是按数字顺序排序（并没有实现其他排序方式）
void parse_args(int argc, char *argv[]) {
    const struct option table[] = {
        {"show-pids", no_argument, NULL, 'p'},
        {"numeric-sort", no_argument, NULL, 'n'},
        {"version", no_argument, NULL, 'V'},
        {0, 0, 0, 0}
    };
    const char *usage = "Usage: pstree-[32|64] [-p] [-n] [-V]\n";

    int opt;
    assert(!argv[argc]);
    while((opt = getopt_long(argc, argv, "pnV", table, NULL)) != -1) {
        switch(opt) {
            case 'p':
                show_pid = true;
                break;
            case 'n':
                printf("It's always numeric.\n\n");
                break;
            case 'V':
                fprintf(stderr, "Version 0.1, to be continued...\n");
                exit(EXIT_SUCCESS);
            case '?':
                printf("%s\n", usage);
                exit(EXIT_FAILURE);
        }
    }
}

// 读取/proc/pid/status文件，获取进程信息并返回
proc_t *read_proc_info(pid_t pid) {
    char pname[MAX_LEN];
    pid_t ppid = 0;
    char info_path[MAX_LEN];

    int ret = snprintf(info_path, sizeof(info_path), "/proc/%d/status", pid);
    assert(ret > 0 && ret < sizeof(info_path));

    FILE *fp = fopen(info_path, "r");
    if (!fp) {
        perror("fopen");
        return NULL;
    }
    char line[MAX_LEN];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Name:", 5) == 0) {
            sscanf(line + 5, "%s", pname);
        }
        if (strncmp(line, "PPid:", 5) == 0) {
            sscanf(line + 5, "%d", &ppid);
            break;
        }
    }
    fclose(fp);

    proc_t *proc = create_proc(pid, pname, ppid);
    return proc;
}

// 遍历/proc目录（数字文件夹即是进程），构建进程树root
void tranverse_proc_dir(proc_t *root) {
    struct dirent *entry;
    DIR *dir = opendir("/proc");
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) {
            continue;
        }
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
            continue;
        }

        pid_t pid = atoi(entry->d_name);
        proc_t *proc = read_proc_info(pid);
        if (proc == NULL) {
            printf("Failed to read proc info for pid %d\n", pid);
            continue;
        }
        proc_t *parent = find_proc(root, proc->ppid);
        assert(parent);

        if (parent->child == NULL) {
            parent->child = proc;
        } else {
            // 插入到最后一个sibling节点後面（插到第一个也行，就是会破坏顺序）
            proc_t *last_one = parent->child;
            while (last_one->sibling) {
                last_one = last_one->sibling;
            }
            last_one->sibling = proc;
        }
    }

    closedir(dir);
}


int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    proc_t *root = create_proc(0, "idle", 0);

    tranverse_proc_dir(root);

    print_tree(root, 0, "");

    free_tree(root);

    return 0;
}
