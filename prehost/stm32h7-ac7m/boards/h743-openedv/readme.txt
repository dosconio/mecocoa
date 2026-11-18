STM32H743 OpenEdv board build steps

1. 构建前，在 Unisym 仓库执行 git pull。
2. 定义 ubinpath、uobjpath、uincpath 环境变量。
3. 在 Unisym 仓库执行：

   make list cgstm32h7

4. 在 Mecocoa 根目录执行以下命令，产生配置文件（与 Linux 平台相同）：

   make config

5. 进入本目录并执行：

   make

> 参考项目的环境变量，不要漏
