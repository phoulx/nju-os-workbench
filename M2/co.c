#include "co.h"
#include <stdlib.h>
#include <stdint.h>
#include <setjmp.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/*----------- 数据结构定义 -----------*/

enum co_status {
    CO_NEW = 1, // 新创建，还未执行过
    CO_RUNNING, // 已经执行过
    CO_WAITING, // 在 co_wait 上等待
    CO_DEAD,    // 已经结束，但还未释放资源
};

// 每个协程的堆栈使用不超过 64 KiB
#define STACK_SIZE (64 * 1024)

struct co {
    const char *name;
    void (*func)(void *); // co_start 指定的入口地址和参数
    void *arg;

    enum co_status status;  // 协程的状态
    struct co *    waiter;  // 是否有其他协程在等待当前协程
    jmp_buf        context; // 寄存器现场
    uint8_t        stack[STACK_SIZE]; // 协程的堆栈

    void * stack_base; // 栈的基址（起点），为栈空间最大地址
    struct co * next;
};

/*----------- 链表操作 -----------*/

int co_num = 0;
struct co *current = NULL;
struct co *head = NULL;

// 默认插入到current後
void insert_co(struct co *co) {
    if (current == NULL) {
        current = co;
        co->next = NULL;
    } else {
        struct co *temp = current->next;
        current->next = co;
        co->next = temp;
    }
    co_num++;
}

void remove_co(struct co *co) {
    assert(current != co);
    assert(co_num > 0);
    struct co *prev = NULL;
    struct co *temp = head;
    while (temp != NULL) {
        if (temp == co) {
            if (prev == NULL) {
                head = temp->next;
            } else {
                prev->next = temp->next;
            }
            break;
        }
        prev = temp;
        temp = temp->next;
    }
    free(co);
    co_num--;
}

struct co* random_select_co() {
    assert(co_num > 0);
    int num = rand() % co_num;
    struct co *temp = head;
    while (num--) {
        temp = temp->next;
    }
    return temp;
}

/*----------- 切换协程栈 -----------*/

static inline void
stack_switch_call(void *sp, void *entry, uintptr_t arg) {
    asm volatile (
#if __x86_64__
        "movq %0, %%rsp; movq %2, %%rdi; jmp *%1"
          :
          : "b"((uintptr_t)sp),
            "d"(entry),
            "a"(arg)
          : "memory"
#else
        "movl %0, %%esp; movl %2, 4(%0); jmp *%1"
          :
          : "b"((uintptr_t)sp - 8),
            "d"(entry),
            "a"(arg)
          : "memory"
#endif
    );
}

/*----------- 接口实现 -----------*/

struct co *co_start(const char *name, void (*func)(void *), void *arg) {
    struct co *new_co = malloc(sizeof(struct co));
    assert(new_co != NULL);

    new_co->name = name;
    new_co->func = func;
    new_co->arg = arg;
    new_co->status = CO_NEW;
    new_co->waiter = NULL;
    // 栈最高地址为栈底，向下生长
    // - 8 是因为x86-64要求堆栈按照16字节对齐（临时解决）
    new_co->stack_base = new_co->stack + STACK_SIZE - 8;

    insert_co(new_co);
    return new_co;
}

void wrapper(void *arg) {
    if (current->status == CO_NEW) {
        current->status = CO_RUNNING;
    }
    current->func(arg);
    current->status = CO_DEAD;
    if (current->waiter != NULL) {
        // CO_WAITING需要在这儿改，否则random_select_co会空转
        current->waiter->status = CO_RUNNING;
    }
    // 此时不能释放资源（不然co_wait就找不到了）
    co_yield();
}

void co_yield() {
    struct co *choice;
    do {
        choice = random_select_co();
    } while (choice->status != CO_NEW && choice->status != CO_RUNNING);

    assert(current != NULL && choice != NULL);
    assert(current->status != CO_NEW);

    int val = setjmp(current->context);
    if (val == 0) { // 调用时setjmp第一次返回0，保存现场
        current = choice;
        if (choice->status == CO_NEW) {
            stack_switch_call(choice->stack_base, wrapper, (uintptr_t)choice->arg);
        } else {
            longjmp(choice->context, 1);
        }
    } else { //longjmp第二次返回，恢复现场
        assert(val == 1);
        return;
    }
}

void co_wait(struct co *co) {
    assert(co != NULL);
    assert(co->status != CO_WAITING);

    current->status = CO_WAITING;
    co->waiter = current;
    while (co->status != CO_DEAD) {
        co_yield();
    }
    remove_co(co);
}

/*----------- 初始化及退出 -----------*/

__attribute__((constructor)) void init() {
    srand(time(NULL));

    struct co *main_co = malloc(sizeof(struct co));
    assert(main_co != NULL);
    main_co->name = "main";
    main_co->status = CO_RUNNING;
    current = head = main_co;
    co_num = 1;
}

__attribute__((destructor)) void fini() {
    struct co *temp = head;
    while (temp != NULL) {
        struct co *next = temp->next;
        free(temp);
        temp = next;
    }
}