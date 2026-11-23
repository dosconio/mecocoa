
反客 STM32H750XBH6开发板 32MB-W25Q256

快速验证只需要拷贝SD卡和烧录elf即可。如果内部flash没有加载器也需要烧录官方的hex文件。

构建依赖
- UNISYM ASM, 用于汇编（由于编译 aasm 需要的库也需要 aasm 生成，所以发布包附赠一份 Linux ABI 静态链接版本的 aasm）
- LLVM, 用于用户程序构建
- ARM GCC 或 Keil MDK-ARM
- Linux 同款的 menuconfig

1. 构建前，在 Unisym 仓库执行 `git pull`。

2. **定义 ubinpath、uobjpath、uincpath 环境变量**。

3. 在 Unisym 仓库执行：

```
make list cgstm32h7
```

4. 在 Mecocoa 根目录执行以下命令，产生配置文件（与 Linux 平台相同）：

```
make config # 如果没有 include/autoconf.h 文件，必须执行此命令
make -C depends/freetype clean-ac7m-gcc ac7m-gcc # 编译GCC版TTF字体支持库
make -f accmlib/accma32.clang.make       # 生成 stm32h7 应用链接库
make -f subapps/Makefile.clang.a32       # 生成 stm32h7 实用应用
```

   应用ELF生成在  $uobjpth/sapp-a32-pi 目录下。可以复制到SD卡的 X:\ac7m\apps

5. 进入本目录并执行：

   `make`

> 参考项目的环境变量，不要漏
> 下载方式参考反客提供的「6.外置下载算法以及USB下载教程/外置下载算法使用说明.pdf」
> Windows 产生的 .o .d 与Linux环境下产生的 .o .d 格式不一样，不要混用。换环境必须手动删除中间文件重新生成。

烧录提示：

文件：her\qrs-projects\FankeH750\STM32H750XBH6.hex (官方提供的)
→ 0x08000000
文件：mcca-stm32h750X-ac7m-gcc.elf
→ 0x90000000

配置说明：
- `CONFIG_NommuHeapSize` 默认512，单位4K，512*4K=2M
	- 是用户程序的堆大小，如果用户程序有动态内存分配，需要根据实际需求调整。
	- 没有 MMU 的芯片不得不设置这个限制


