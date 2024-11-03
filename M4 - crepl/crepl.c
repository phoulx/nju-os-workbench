#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <dlfcn.h>


typedef struct {
    char wrapper_func[64];
    char cfile[64];
    char sofile[64];
    int sequence;
} Info;


// 解析输入，写入C文件，返回文件信息
Info* write_to_file(char *line) {
    if (line[0] == '\n') {
        return NULL;
    }

    static int count = 0;
    char func_content[4096];
    Info* res = malloc(sizeof(Info));
    if (res == NULL) {
        return NULL;
    }

    // 序号用于wrapper函数名
    res->sequence = ++count;
    strcpy(res->wrapper_func, "");
    sprintf(res->cfile, "/tmp/crepl_%02d.c", res->sequence);

    // 按照是否以「int 」开头判断是函数还是表达式
    if (strncmp(line, "int ", 4) != 0) {
        // 表达式，包装为函数，且需返回函数名以便调用
        sprintf(res->wrapper_func, "_wrapper%02d", res->sequence);
        sprintf(func_content, "int %s() { return %s; }", res->wrapper_func, line);
    } else {
        // 函数，直接写入，且函数名为空
        sprintf(func_content, "%s", line);
    }

    FILE *fp = fopen(res->cfile, "w");
    if (fp == NULL) {
        return NULL;
    }

    fputs(func_content, fp);
    fclose(fp);

    return res;
}

// 编译为共享库
int compile_to_share(Info *info) {
    sprintf(info->sofile, "/tmp/crepl_%02d.so", info->sequence);
    char* exec_args[] = {"gcc", "-shared", "-fPIC", "-Wno-implicit-function-declaration", info->cfile, "-o", info->sofile, NULL};

    int pid = fork();
    if (pid == 0) {
        // 子进程，编译
        execvp("gcc", exec_args);
        perror("execvp");
        exit(1);
    } else {
        // 父进程，等待子进程结束
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0;
        } else {
            return -1;
        }
    }
}

// 加载共享库，输出结果
int load_and_output(Info *info) {
    void *handle;
    int (*func)(void);

    // 全局加载
    handle = dlopen(info->sofile, RTLD_LAZY | RTLD_GLOBAL);
    if (handle == NULL) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return -1;
    }

    // 如果输入为函数，加载完即退出，不释放handle
    if (info->wrapper_func[0] == '\0') {
        printf("OK.\n");
        return 0;
    }

    // 表达式的话，找到wrapper
    func = dlsym(handle, info->wrapper_func);
    if (func == NULL) {
        fprintf(stderr, "dlsym: %s\n", dlerror());
        return -1;
    }

    printf("= %d\n", func());
    dlclose(handle);

    return 0;
}


int main(int argc, char *argv[]) {
    static char line[4096];

    while (1) {
        printf("crepl> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        Info *info = write_to_file(line);
        if (info == NULL) {
            printf("Failed to write to file.\n");
            continue;
        }

        if (compile_to_share(info)) {
            printf("Failed to compile.\n");
            continue;
        }

        if (load_and_output(info)) {
            printf("Failed to load and output.\n");
        }

        free(info);
    }
}
