# LAB - L0
https://jyywiki.cn/OS/2024/labs/L0.md  
开始非常迷惑，甚至不懂图片要在终端显示（以字符绘图）还是在新窗口显示。

## 理解实验要求
线索在实验说明中，「调试 MBR 中的加载器」代码即为框架，课堂上也有演示。  
阅读代码，发现框架分两个部分：
- QEMU：模拟硬件
- ABSTRACT_MACHINE：在硬件基础上抽象出一套API

实验要做的都是基于AM的API（不过今年的文档还没有，[去年的在这](https://jyywiki.cn/AbstractMachine/AM_Spec.html)），本次实验主要用到IOE部分。

具体硬件的定义都在`amdev.h`中，如屏幕长宽等信息在`GPU_CONFIG`，将数据写入屏幕某一坐标则是`AM_GPU_FBDRAW`。

还可参考[ICS-PA的说明](https://nju-projectn.github.io/ics-pa-gitbook/ics2023/2.5.html#vga)：
>AM_GPU_FBDRAW, AM帧缓冲控制器, 可写入绘图信息, 向屏幕(x, y)坐标处绘制w*h的矩形图像. 图像像素按行优先方式存储在pixels中, 每个像素用32位整数以00RRGGBB的方式描述颜色.

## 步骤
弄懂了要干什么，实现起来比较简单，就是逐个像素填充颜色。  
当然首先要有图片的二进制数据，我这里是用python解析并转为C程序可用的格式。  
数据我用一维数组存放的，读取时做一下映射，得到当前位置的颜色。  
最後再加上按键判断。

`ioe.c`编译时有问题，用`GCC diagnostic ignored "-Warray-bounds"`忽略。

## 遗留问题
目前只能跑在默认的x86-qemu (32-bit) ，尝试改成64位没有成功。  
在debug-bootloader（打印hello）试了64位也不行，所以估计要改AM框架才能兼容。
