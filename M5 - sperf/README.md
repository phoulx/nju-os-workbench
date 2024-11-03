# LAB - M5
https://jyywiki.cn/OS/2024/labs/M5.md  

## 思路
看了实验要求，感觉是不小的工程，有点害怕  
还好实验文档挺详细的，并且给了重要步骤：
1. 使用 fork 创建一个新的进程；
2. 子进程使用 execve 调用 strace COMMAND ARG...，启动一份 strace；注意在 execve 成功返回以后，子进程已经不再受控制了，strace 会不断输出系统调用的 trace，直到程序结束。程序不结束 strace 也不会结束；
3. 父进程想办法不断读取 strace 的输出，直到 strace 程序结束，读取到输出后，如果距离上次输出已经超过 100ms，就把统计信息打印到屏幕上。

另外注意到文档中的一句话：
>这个实验也替代了通常《操作系统》课程中常见的管道 (shell) 作业

### 初步理解
核心部分应该是解析`strace`的输出  
而难点则是如何在进程间传递信息  
具体来说，输出的内容要保存在什么地方，使得写入和读取（可能重复读？）都较方便
- 通过文件——肯定可行但是不是将问题复杂化了？
- 重定向输出到父进程stdin？

答案是管道。搜索并参考了别人的解答，梳理一下：  
建立管道，fork  
- 子进程：
1. 重定向stderr到管道写端
2. 拷贝command参数和环境变量
3. 调用execve执行command
4. 等待进程结束（不用管了）
- 父进程：  
以0.1s为周期，循环解查子进程是否结束，如未：  
1. 读取管道中的所有数据
2. 解析
3. 保存结果，排序
4. 打印统计数据和分隔符
如果子进程结束，则退出循环

看起来又有个问题是，如果管道读取时结尾在某行中间，怎么办？  
用一个buffer？  

### 困难解决
如何从管道读取数据？有几个方法：
- 系统调用read：低级别的，用于从文件描述符读取原始字节数据
- 标准库函数fgets：用于从 FILE* 流中读取一行文本数据，最多读取指定数量的字符
- 标准库函数getline：用于从 FILE* 流中读取一行文本数据，自动调整缓冲区大小以容纳整行数据

很明显，这里适合用getline，因为管道中的数据（即strace结果）是分行的，并且每行不会太长。  
fgets的指定buffer大小没什么作用；而read更是需要额外处理断行的情况，较麻烦。  

循环怎么写？  
先想到的是：在每个循环中，循环执行getline，直到读到EOF，这时处理数据，最後sleep一个时间周期（0.1s）  
但这样有个小问题，getline是阻塞的，如果读到半行会等待直到换行符，这样该次循环时间应该会变长（虽然影响不大）  
更合理的方法是以读取数据为主循环，在循环中检查时间，每读一行检查一次是否过了0.1s  

但最终还是遇到断行的问题：
```
No match!
 write(2, "\n", 1
No match!
 )                       = 1 <0.000018>
```
开如以为是读到写了一半的数据，但好像又有哪里不对，再仔细看，发现系统调用正在向stderr写数据！  
所以原因是，在execve执行下列命令时：
```
strace -T command <options>
```
command的stderr输出干扰到了strace的输出  
这个问题jyy在文档中有提到  
>例如，strace 默认会把 trace 输出到 stderr，这个行为没啥问题。但如果 strace 追踪的程序也输出到 stderr，不就 “破坏” 了 strace 的输出了吗。而且，即便解决了上面的问题 (比如把程序的 stderr 重定向到 /dev/null 丢弃)，...

虽然说得轻描淡写，但这个问题并不好解决。  
努力问GPT之後，仍然无法在保持strace输出到stderr不变的同时，「把程序的 stderr 重定向到 /dev/null 丢弃」。  
看了github上的二十多个解答，也没有一个做到。  
因为上面的命令在我们子程序中是作为整体给execve执行的，要想单独修改command的输出，似乎衹能深入到strace的内部（超纲了）  
好在strace有一个指定输出的参数`-o`，将其输出到临时文件并再重定向到管道，即可将两者分离  


## 实现注意点
1. 存syscall我用动态数组存的，为了避免过多的内存申请，可以预申请一大块（加个capacity）
2. 正则表达式提前编译，而不是用一次编译一次
3. 及时释放内存


## 遗留问题
1. 输出属于TopN问题，所以可能用一个heap（最大堆）效率更高？不过这里数据量很小，~~偷懒~~简单起见就直接用qsort了
2. 统计时明显用一个hash表更好，但C没有类似的内置类型，也是简单起见直接用数组了
3. 正则pattern未仔细测试
4. 没考虑数字精度


## 其他解答
随便挑了几个（前两个附了实验报告）  
https://github.com/lxmwust/os-workbench/tree/main/sperf  
https://github.com/cpyhal3515/os_learning/tree/main/os_m_code/M3_sperf  
https://github.com/MxLearner/os/tree/main/sperf  
https://github.com/iamxym/os-workbench/tree/master/sperf  
https://github.com/KruskalLin/os-workbench/tree/master/sperf  
https://github.com/ZRZ-Unknow/os-workbench/tree/master/sperf  
https://github.com/insorker/nju-os-workbench-2022/tree/main/sperf  
