# LAB - M6

https://jyywiki.cn/OS/2024/labs/M6.md

印象中持久化是操作系统三个 pieces 中最麻烦的，因为要跟实际的存储设备打交道，没那么自由

## 先读手册

> jyy：这是一个阅读手册的训练

~~因为没足够的时间~~，开始并不想自己看 FAT Specification  
打算直接参考其他解答，调调通过就算了

但直接抄自己看不懂的代码很难受，最後还是没走捷径，看完了 FAT Specs  
如 jyy 所说，不长，也比较好读  
不得不说 FAT 的一些设计挺蠢的，比如居然不能存奇数秒  
同样用 32 个 bit 表示时间，Unix 时间戳就优雅多了

看完文档，心中有底，才感觉到本实验确实不困难

### FAT32 文件系统

按`mkfs.fat -v -F 32 -S 512 -s 8 fs.img`格式化
little endian：以 Byte 为单位倒序

#### 空间解析

整个空间分为 3 部分：保留区、FAT 表（可能多份）、数据区

1. 保留区  
   保留区占 32 个 Sector，最重要的是#0（第一个）Sector，称为 Boot Sector（512Bytes），其中核心是开头的 BPB 部分（大小 64Bytes）  
   以下为本实验参考 image 的保留区空间

   - #1 sector 为 FSInfo，以 0x41615252 开头（Specs 上说 FSInfo Copy 会在#7 sector，但本例中无）
   - #2-5 sector 为空（填 0）
   - #6 sector 为#0 sector 的 Copy
   - #7-31 sector 为空（填 0）

2. FAT 表  
   第一份位于 0x4000，即#32 sector（512×32=0x4000）  
   FAT Size = 0x10000 （可在 BPB 中查到）  
   所以第二份 FAT 表在 0x14000-0x23999  
   FAT 表从第 8 个 Byte 开始，每 4 个 Byte 为一条 Entry，对应从簇号 2 开始

3. 数据区  
   紧挨着最後一份 FAT 表的结尾  
   簇号从这里开始算（上面的保留区和 FAT 表，不管有多少 sector 都不算簇），从 2 算起  
   数据在当前簇是否结束，要查对应的 FAT Entry

#### 长文件名

实验文档的图很清楚  
文件的主要信息还在短名

#### 从根目录遍历

如何读取一个 FAT32 分区，并列出所有目录？

1. 读 BPB，找到根目录的簇号（如 2）
2. 找到簇号对应的数据，里面有目录的具体信息对应的簇号（如 3）
3. 再到对应簇读取数据，依次遍历
   由此可见，FAT 表的作用仅仅是在一个簇无法存下数据时，链向下一个簇

### Bitmap 图片格式

整体分为头部+数据区，比较简单，直接看图

- 以 424D 开头
- 文件 header 包含 filesize、宽、高、数据区偏移
- 数据区每 3 个 Byte 表示一个像素，每行的 padding 按 4Byte 补齐
  甚至实验中无需关注其细节，就可以恢复约一半

## 解决思路

图片在 FS 中的存储，理想情况是连续的，可能会有部分图片分块  
在 FS 格式化後，FAT 表被清空，图片数据仍保留在原处

根据文档给的参考，结合我的思考，设置恢复目标为：

- 恢复所有的文件名，包括跨簇条目
- 假定所有文件的数据连续存储，即：恢复数据连续的 BMP 文件，放弃数据不连续的文件
- 性能良好

整体流程如下：
1. 顺序扫描 img 中每个 cluster，判断是否为包含目录的簇，如果不是则跳过
2. 对于包含目录的簇，顺序扫描整个簇，找到所有目录条目（长文件名和短文件名为一个整体）
3. 同时将跨簇的条目单独列出，等待最後匹配并得到有效的目录条目
4. 对于单个目录条目，如果有效：
   - 根据长文件名条目，提取文件名
   - 根据短文件名条目，找到数据所在簇及数据长度
   - 将数据写入临时文件
   - 调用 popen，得到 sha1sum
   - 打印结果

## 实现注意点

- 最好先手动模拟一遍 FAT32 目录的解析（从根目录开始），可借助直接读二进制的插件（如 VSCode - Hex Editor）
- 没必要全盘扫描找目录项，因为目录项一定是从簇的头部开始存，所以扫描簇头即可快速判断
- 判断目录项的逻辑可充分利用 Specs 中的相关描述
- 跨簇匹配因为数量较小，直接用了数组，如果数量大可以考虑其他结构

### 简洁性

在搞清楚要做的事情後，感觉代码量应该不大，但实际完成居然还是写了 400 行  
看到有的答案仅用了不到 200 行，不得不让我反思几秒...

回顾一遍，其中最明显的是跨簇匹配的逻辑较复杂，如果不做跨簇匹配的话，可以省去约 80 行  
而且跨簇的条目很少，示例 img 的 97 个条目中仅 1 个跨簇

一些函数也许可以合并（比如 is_dirent_basic 和 is_dirent_long）

如果将一些判断、循环等改为C语言常见的单行简写，也可以缩小总行数  
出于易读性的考虑，还是保持当前显式风格不变

## 结果

对于参考 img，首先挂载，运行`sha1sum *.bmp`，得到标准答案（criteria.txt）  
make 并运行，得到我的答案，输出到文件 my.txt  
运行 grade.py，结果如下：

```
NAME correct: 97/97 = 100.00%
SHA1 correct: 47/97 = 48.45%
```

## 遗留问题

1. 对于文件在非连续簇的情况，文档中给了有向图的解法  
   除了图算法不好做，就算是求解差分（像素是否邻近）也比较困难  
   对于簇 A、簇 B，至少有两种不好处理的情况：

   - 簇 A 末尾像素恰好为图片一行的末尾像素
   - 同一像素点的 3 个字节，被簇 A 簇 B 分开
     所以先放弃了

2. 另外遇到的一个不算问题的问题，就是用什么指针来表示一段 32 字节的目录项内存，可选项：

   - void \*
   - u8 \*
   - struct fat32dent \*
   - struct fat32lfn \*
   - 用一个新的 struct？

3. 没有适配文件名有非 ASCII 字符的情况（需要解析 16-bit unicode）

## 其他解答

去年以前，本实验名称为`frecov`，今年加了个`s`，搜索时可注意

搜索结果中，也许可以参考的有：
- https://github.com/Cookiecoolkid/os-workbench/blob/main/fsrecov
- https://github.com/congee524/OSLAB_HBK/blob/master/frecov
- https://github.com/SToPire/os-workbench/blob/master/frecov
- https://github.com/Ouhznehc/NJU-OSLAB/blob/main/frecov

> 吐槽：学生时代大家好像都会无顾忌地往main函数里塞满东西...

在读明白Specs的情况下，本Lab逻辑并不复杂。借助LLM工具的提示，其实不太需要参考他人解答  
所以上面列的我也没细看（何况可读性都挺差的...

最後感谢生成式AI
