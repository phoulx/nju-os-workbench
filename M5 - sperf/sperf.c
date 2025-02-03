#define _GNU_SOURCE // memfd_create

#include <assert.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char name[64];
    double time;
} SyscallLog;

typedef struct {
    SyscallLog* logs; // 动态数组
    int count;
    int capacity;
} SyscallStats;

// 提取syscall名称及耗时
SyscallLog* extract(char* line, regex_t* reg)
{
    regmatch_t matches[3];
    int len;

    static SyscallLog log;

    if (regexec(reg, line, 3, matches, 0) == 0) {
        len = matches[1].rm_eo - matches[1].rm_so;
        strncpy(log.name, line + matches[1].rm_so, len);
        log.name[len] = '\0';

        len = matches[2].rm_eo - matches[2].rm_so;
        char time_str[16];
        strncpy(time_str, line + matches[2].rm_so, len);
        time_str[len] = '\0';
        log.time = atof(time_str);

        return &log;
    } else {
        // printf("No match!\n %s\n", line);
        return NULL;
    }
}

// 更新统计数据
int update_stats(SyscallLog* log, SyscallStats* stats)
{
    for (int i = 0; i < stats->count; i++) {
        if (strcmp(stats->logs[i].name, log->name) == 0) {
            stats->logs[i].time += log->time;
            return 0;
        }
    }

    if (stats->count == stats->capacity) { // 扩容
        stats->capacity = stats->capacity == 0 ? 10 : stats->capacity * 2;
        stats->logs = realloc(stats->logs, stats->capacity * sizeof(SyscallLog));
        assert(stats->logs != NULL);
    }

    memcpy(&stats->logs[stats->count], log, sizeof(SyscallLog));
    stats->count++;

    return 0;
}

int compare(const void* a, const void* b)
{
    double diff = ((SyscallLog*)b)->time - ((SyscallLog*)a)->time;
    return diff > 0 ? 1 : -1;
}

// 整理并打印统计数据
int output(SyscallStats* stats, bool is_end)
{
    // 排序（原地即可）
    qsort(stats->logs, stats->count, sizeof(SyscallLog), compare);

    double total = 0;
    for (int i = 0; i < stats->count; i++) {
        total += stats->logs[i].time;
    }

    printf("Total: %fs\n", total);
    for (int i = 0; i < stats->count; i++) {
        int ratio = stats->logs[i].time / total * 100;
        printf("%s (%d%%)\n", stats->logs[i].name, ratio);
        if (i == 4) {
            printf("...\n");
            break;
        }
    }

    // 分隔标记
    for (int i = 0; i < 80; i++) {
        putchar('\0');
    }
    if (!is_end) {
        printf("====================\n");
    }

    fflush(stdout);
    return 0;
}

void child_process(int pfd[], int argc, char* argv[])
{
    // 其实没必要用memfd，因为pipe的FD直接对应在/proc/self/fd下
    int memfd = memfd_create("strace_output", MFD_CLOEXEC);
    assert(memfd != -1);
    char memfd_path[64];
    snprintf(memfd_path, sizeof(memfd_path), "/proc/self/fd/%d", memfd);

    // 如果在子进程最开始就关闭stdout和stderr，下面的command的输出会到pipe，原因未详
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // 为了避免command的stderr输出干扰，这里没有直接重定向子进程的stderr
    // 而是借助strace的-o参数，用一个临时文件（memfd）作中介再重定向到pipe
    close(pfd[0]);
    int dpr = dup2(pfd[1], memfd);
    assert(dpr != -1);
    close(pfd[1]);

    // 组合命令及参数
    char* exec_argv[argc + 4];
    exec_argv[0] = "strace";
    exec_argv[1] = "-T";
    exec_argv[2] = "-o";
    exec_argv[3] = memfd_path;
    for (int i = 1; i < argc; i++) {
        exec_argv[i + 3] = argv[i];
    }
    exec_argv[argc + 3] = NULL;

    // 拷贝PATH环境变量
    char path_str[1024] = "PATH=";
    strncat(path_str, getenv("PATH"), sizeof(path_str) - 5);
    char* exec_envp[] = { path_str, NULL };

    execve("/usr/bin/strace", exec_argv, exec_envp);

    // 正常不会执行到这里
    assert(false);
}

void parent_process(int pfd[])
{
    close(pfd[1]);
    FILE* pipe_fp = fdopen(pfd[0], "r");
    assert(pipe_fp != NULL);

    SyscallStats stats = { NULL, 0, 0 }; // 记录统计数据的数组
    char* line = NULL;
    size_t len = 0;
    clock_t prev = clock();

    // 正则表达式匹配（提前编译以节省开销）
    regex_t reg;
    const char* pattern = "^([a-z0-9_]+)\\(.*<([0-9.]+)>\n$";
    int rc = regcomp(&reg, pattern, REG_EXTENDED);
    assert(rc == 0);

    while (getline(&line, &len, pipe_fp) != -1) {
        // 因为getline是阻塞的，所以如果返回-1，说明子进程的写端已关闭
        // 间接说明子进程已经结束，不必再使用waitpid来检查

        SyscallLog* log = extract(line, &reg);
        if (log == NULL) {
            continue;
        }
        update_stats(log, &stats);

        clock_t now = clock();
        if ((now - prev) * 1000 / CLOCKS_PER_SEC > 100) { // 间隔达到0.1s
            prev = now;
            output(&stats, false);
        }
    }
    output(&stats, true);

    free(line);
    free(stats.logs);
    regfree(&reg);
    fclose(pipe_fp);
}

int main(int argc, char* argv[])
{
    int pfd[2];
    assert(pipe(pfd) == 0);

    pid_t pid = fork();
    assert(pid != -1);

    if (pid == 0) {
        child_process(pfd, argc, argv);
    } else {
        parent_process(pfd);
    }

    return 0;
}
