# LAB - M2
https://jyywiki.cn/OS/2024/labs/M2.md  
看实验说明，基本一头雾水

## 思路
M2要实现一个协程库，部分框架已经给了
首先要有一个存放当前所有协程的索引结构，可以是数组、链表或其他什么
这里用了链表，操作主要有3个：插入、删除、随机选取

co_start要做的是：
1. 申请协程内存
2. 添加到链表

co_yield工作：
1. 保存现场（setjmp）
2. 切换协程（从就绪协程中随机选择一个）
    2.1 如果是未运行过的，要设置新协程的相关寄存器（借助汇编）
    2.2 如果已运行过，直接longjmp，啥都不管
3. 等切换回来，继续往下走（也可以不管）

co_wait：
1. 设置相关waiting状态
2. 调用`co_yield`，空转，直到等待的那个协程结束


## 实现注意点
- 协程栈的方向是从高地址到低地址，所以切换时传递的是栈的最高地址（即栈底）
- 栈衹有第一次切换需要手动传递栈基址，後面的切换都是setjmp/longjmp自动完成，所以全程不用保存动态变化的栈顶指针（rsp）
- 切换时的函数需要用`wrapper`包一下，因为执行完了要手动yield
- CO_WAITING并不是协程在等待时机运行，即等的不是自己，而是其他协程
- 协程执行完了，等待者的状态需及时调整，以获得运行机会
- 如果不做16位对齐，很可能在调用库函数（如sprintf）时出现奇怪的Segmentation Fault
- 注意释放资源的时机，在co_wait释放比较合理
- 多用assert


## 遗留问题
- `setjmp/longjmp`的机制和原理
- 那段汇编（`stack_switch_call`）也没有很懂
- 协程栈是不是需要管理，如栈溢出
- 随机选择算法不够好（但基本够用）
- 不支持嵌套wait

## 其他解答
网上公开的解答都不够好，比如：
- [知乎这个](https://zhuanlan.zhihu.com/p/490475991)，他没搞明白CO_WAITING是啥，恢复堆栈的操作也十分冗余
- [littlesun](https://littlesun.cloud/2023/07/30/协程库-libco/)，跟知乎学的
- [jiaweihawk](https://jiaweihawk.gitee.io/2021/08/06/操作系统-设计与实现-三/)，也是很冗长，还有不明所以的递归

上面这几个完全不必参考。以下两篇算是写的还不错的，可以看看，但没有给代码：
- [vgalaxy](https://vgalaxy.work/posts/os-mini-lab/)
- [noicdi](https://www.noicdi.com/posts/5e8e42b3.html)

通过搜索（可以[挑一个特别的函数名如`stack_switch_call`并指定文件路径搜索](https://github.com/search?q=stack_switch_call+path%3Aco.c&type=code)），发现Github上有一些不错的实现，值得参考（但既然你找到了这里，直接看我的就行了）：
- https://github.com/cpyhal3515/os_learning/tree/main/os_m_code/M2_libco
- https://github.com/SToPire/os-workbench/tree/master/libco
- https://github.com/iamxym/os-workbench/tree/master/libco
- https://github.com/hydra24-njus/os-workbench/tree/main/libco
- https://github.com/heiyan-2020/oslab/tree/master/libco
- https://github.com/ZRZ-Unknow/os-workbench/tree/master/libco
- https://github.com/SiyuanYue/NJUOSLab-M2-libco
- https://github.com/shuiyuncode/os-workbench-2022/tree/main/libco

实验手册的内容其实就很丰富，但一开始有点看不懂，有些说明需要做完实验才明白在讲什么
