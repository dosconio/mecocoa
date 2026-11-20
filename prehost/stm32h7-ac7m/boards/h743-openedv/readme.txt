STM32H743 OpenEdv board build steps

1. 构建前，在 Unisym 仓库执行 git pull。
2. 定义 ubinpath、uobjpath、uincpath 环境变量。
3. 在 Unisym 仓库执行：

   make list cgstm32h7

4. 在 Mecocoa 根目录执行以下命令，产生配置文件（与 Linux 平台相同）：

   make config # 如果没有 include/autoconf.h 文件，必须执行此命令
   make -f accmlib/accma32.clang.make # 生成 stm32h7 应用链接库
   make -f subapps/Makefile.clang.a32 # 生成 stm32h7 实用应用

   应用ELF生成在  $uobjpth/sapp-a32-pi 目录下。可以复制到SD卡的 X:\ac7m\apps

5. 进入本目录并执行：

   make

> 参考项目的环境变量，不要漏

【需要构建环境】
- armgcc, clang
- menuconfig
- (可选) keil

【实用应用】
- ribbon : 桌面和任务栏

