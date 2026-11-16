## 2026-11-16

- 22:32（UTC+8）flap32 和 uefi64 在确认 PCI 消息中断静默、I/O APIC 路由已屏蔽后，为相关 DRHD 准备并提交 EIME。部分提交或启动路由解屏蔽失败时先恢复屏蔽，再逆序停用重映射。可安全回退时按 BSP 实际 LAPIC 模式重建兼容路由，硬件状态不明则停止初始化。

- 22:12（UTC+8）x86/x64 内核通过公共 IOMMU 接口分派 DMA、中断重映射和故障事件。NVMe 与 xHCI 通过 Devsman 绑定和释放 MSI/MSI-X，AHCI 的兼容 INTx 判断交给 Devsman。启动识别 DMAR 和 IVRS，当前启用 Intel 硬件后端。

- 21:55（UTC+8）Intel VT-d 实现迁入 devdriv/core/iommu，命名空间统一为 VTd。flap32、uefi64 和 long64 的内核源列表用通配符纳入该层目录。 公共 IOVA 映射和解绑入口保留在 memoman-iova.cpp，VT-d 硬件操作由 Intel 后端负责。

- 21:43（UTC+8）flap32 和 uefi64 按 PCI 设备及 I/O APIC 的 DRHD 归属启用多单元中断重映射。各单元独立分配、失效并回收 IRTE，多单元采用 xAPIC 格式。启动失败时屏蔽受影响的路由，逆序停用单元。VT-d 接口归入 Vtd 命名空间，DMA 映射继续使用 Iova。

- 19:38（UTC+8）flap32 和 uefi64 按设备归属为各 DRHD 准备直通 Context Table，再逐单元启用 DMA 翻译。运行时 DMA 失效和故障检查沿用域所属单元。启用失败时逆序回退，硬件状态无法确认时停止设备驱动启动。

- 18:44（UTC+8）Rust i686 用户程序同步 LLVM data layout 并链接 Mecocoa `crt0.o`，由 `_start` 完成运行库初始化、调用 Rust `main` 并经 `exit` 退出；构建依赖跟踪启动对象和 `libx86.a`，修复新版 `core` 的 x87 ABI 检查及产物入口为零的问题。

- 17:59（UTC+8）Mecocoa i686 Rust 目标登记为 Unix family，迁移到 2026-11-16 nightly 和 edition 2024，按新版 schema 固定静态重定位；`mcca-rt` 移至 `accmlib/rust`，用户运行库接入 Mempool 带对齐分配并注册 Rust `GlobalAlloc`，示例启用官方 `alloc` 验证 Box、Vec、String 及 64 字节对齐对象。

- 15:42（UTC+8）flap32 和 uefi64 在 PCI 枚举后解析各 DRHD 的设备 Scope，按 Segment 匹配 PCI 端点、桥下游和 include-all 归属，并记录 I/O APIC 的所属单元和 Source ID。该阶段保留单 DRHD 接管路径。

- 15:33（UTC+8）flap32 和 uefi64 按 DMAR 中的 DRHD 数量分配 VT-d 状态。寄存器、DMA 上下文、失效队列、中断重映射表和故障记录归各自单元，DMA 域沿用所属单元执行失效和回退。接管条件仍为单个 include-all DRHD。

- 14:01（UTC+8）flap32 和 uefi64 的 Ring1 驱动可以运行时逐项绑定和释放 MSI-X 表项。事件和 ACK 按表项及绑定代次处理，关闭设备时释放对应绑定。 e1000 驱动在 Intel 82574L（8086:10D3）上将 RX 与链路事件绑定到表项 0、TX 完成事件绑定到表项 1。第二项绑定失败时先释放第一项，再回退到原有中断路径。

- 12:12（UTC+8）flap32 和 uefi64 为单 DRHD 下匹配 DMAR Scope 的设备恒等映射固件保留区，普通 DMA 分配和解绑避开 RMRR。

- 11:14（UTC+8）flap32 和 uefi64 在 APIC 模式确定后为当前接管的单 DRHD 启用 VT-d Fault Event。中断有界收集并清除故障记录，由设备服务线程限流输出；保留 QI 错误位及不支持故障中断时的同步检查。

## 2026-11-15

- 19:47（UTC+8）flap32 和 uefi64 共用 BSS 中的 IRQ 向量状态表。Ring1 与内核消息中断从各自的额度申请向量，关闭中断源并清空目标 LAPIC 上的在途中断后回收。共享 GSI 在最后一个绑定释放时回收，非 PCI Ring1 在 PIC 下使用固定向量，在 I/O APIC 下按 GSI 申请动态向量。

- 18:47（UTC+8）flap32 和 uefi64 在启动时共用普通设备 IRQ 入口生成器。Ring1 和内核消息中断按向量使用启动时生成的入口，进程页表保留入口页的只读内核映射。生成成功后 flap32 的 Ring1 向量池可用 32 项，静态汇编入口可回退。

- 15:59（UTC+8）flap32 和 uefi64 共用 Devsman 固定中断绑定与释放，PS/2 键鼠和串口使用同一接口。内核按目标架构安装 IDT 门，固定路由在驱动绑定前保持屏蔽。

- 15:40（UTC+8）flap32 的 PIT、RTC 和 PS/2 初始化沿用 Unisym 的现有入口。Devsman 先安装并启用固定中断路由，再在本核中断关闭的情况下初始化设备，APIC 模式随后恢复 8259 的屏蔽状态。

- 14:48（UTC+8）flap32 的兼容 I/O APIC 模式按 MADT 选择控制器和 GSI，并应用 ISA IRQ Override。PIT 使用 MADT 指定的单条边沿路由，拓扑不可用时沿用固定 24 路及 PIT 双引脚兼容配置。Ring1 ISA 与内核 GSI 绑定共用解析结果，固定 ISA 驱动在就绪后开启电平路由。AHCI 使用 PCI INTx 资源绑定 GSI，绑定失败时轮询。

- 13:13（UTC+8）x86／x64 在启动时按 PCI、xHCI、I/O APIC 和 Ring1 中断预算选择 VT-d IRTE 容量，按二次幂分配 256 至 32768 项。硬件表由 `UniquePtr` 管理，空闲项复用 `Bitmap`，运行期间表地址保持固定，缓存失效确认后才回收表项。

- 12:28（UTC+8）补全 ACCM 正常退出生命周期：实现 `atexit`、`__cxa_atexit` 和 `__cxa_finalize` 的不限量逆序回调登记，x86 CRT 在 `main` 返回后经 `exit` 执行已登记析构，再由 `_exit` 终止进程；移除签名错误的空 `atexit` 实现。

- 11:45（UTC+8）flap32 和 uefi64 共用 BSP 的 x2APIC/EIME 启动决策，先安装并提交带屏蔽的 VT-d 路由，再切换 LAPIC。准备失败时恢复兼容路由。flap32 AP 上线前确认本核已采用 BSP 模式，xHCI、NVMe、Ring1 和 ACPI GSI 共用重映射路径。旧 I/O APIC 支持在屏蔽下切换触发模式完成 EOI，flap32 AHCI 在 EIME 下使用轮询。

## 2026-11-14

- 22:14（UTC+8）恢复 Unisym 共用头文件中 ACCM 的 `errno` 声明，并使 libc 的 `_errno()` 使用相同的 C 链接约定，修复 x86 ACCM 文件和套接字封装的编译错误。

- 21:56（UTC+8）x64 使用 DMAR I/O APIC Scope 为启动 ISA、ACPI GSI 和 Ring1 INTx 建立 IRTE，按物理 pin 完成重映射电平中断。固件已启用 x2APIC 且 VT-d 与 I/O APIC 均满足条件时，在启动驱动前提交 EIME。xHCI、NVMe 和 Ring1 使用重映射 MSI/MSI-X，兼容模式继续支持原有消息格式。

- 21:15（UTC+8）统一 flap32 与 x64 的 I/O APIC 路由管理，登记启动 ISA 路由并集中维护 ACPI GSI 与 Ring1 路由的归属、共享引用和屏蔽状态。ACPI 解绑等待回调退出后恢复原 RTE，Ring1 电平中断继续等候各绑定 ACK，flap32 的 PIC 模式继续使用原有屏蔽路径。

- 20:09（UTC+8）将 I/O APIC 路由锁及 GSI 绑定冲突检查扩展到 32 位 x86，统一 `x86_ioapic_` 路由接口。x86 保留固定基址和 24 路 GSI，x64 继续按 MADT 查找控制器。

- 19:57（UTC+8）x64 I/O APIC 启动配置、ACPI GSI 与 Ring1 共用路由锁，保护 selector/data 窗口及路由更新；同一 GSI 拒绝跨路径重复绑定，屏蔽操作只修改 RTE 低 32 位的 mask 位。

- 15:52（UTC+8）LSI SAS 对已枚举的 SATA 硬盘在分区挂载流程后尝试只读 SAT IDENTIFY DEVICE，报告型号和设备声明的 SMART、LBA48、TRIM 能力；不改变常规读写，不发送 SMART 或 TRIM 命令。

- 15:36（UTC+8）x64 LSI SAS／Parallel 硬盘在 READ CAPACITY(10) 饱和时读取 64 位容量，跨 32 位 LBA 的读写使用 16 字节命令；修正 GPT 高 LBA 分区及备份表读取的位宽限制。保留 BusLogic 和光驱原有命令路径。

## 2026-11-13

- 16:36（UTC+8）H7 设备树登记两路 SDMMC、SD 卡及已识别的分区，SDMMC1 引脚由板级配置提供。首个可用 FAT 卷继续挂载到 /mnt/sd0.0。

- 15:00（UTC+8）H7 登记 FMC 控制器、RCC 状态和 SDRAM 子节点，集中配置 SDRAM 地址、容量及保留区；早期初始化失败时不将 SDRAM 加入内存池。

## 2026-11-12

- 11:32（UTC+8）H743 新增独立 GCC 构建入口、GNU 链接脚本及 GAS 启动与上下文汇编，复用当前 Keil 源码清单和向量顺序，初始化数据、全局构造和驱动登记表，产物使用独立目录。

## 2026-11-11

- 18:09（UTC+8）H7 XART 按 Unisym 实例登记 UART/USART 和板级引脚描述；设备节点增加四态 rcc_state，由 GPIO、XART 驱动登记 RCC 状态，摘要统一显示并仅在 Disabled 时省略详情，移除调试层的 UART 专用查询。

## 2026-11-10

- 21:39（UTC+8）Devsman 声明按十个领域分组，USB、网络及调试组内按用途排列；保留原有签名、默认参数和条件编译范围。

- 19:44（UTC+8）公共按名节点查询及递归查找开放给 H7，修复 GPIO 登记调用 FindNamedNode 的链接错误。

- 19:35（UTC+8）GPIO 共用控制器登记和状态查询，H7 自动登记全部端口，设备树按实时 RCC 状态展开引脚；Intel 接入公共状态查询，板级 LED、按键配置集中到 unicfg，配置头直接包含 GPIO 类型定义。

- 14:02（UTC+8）H7 登记 USB 供电和触摸的软件 I²C 总线、器件及板级资源，关联现有驱动对象；触摸初始化失败和关闭时先注销器件、总线，再释放 runtime。

- 13:51（UTC+8）设备树 H 输出新增 MMIO、具名 GPIO、总线地址和配置参数显示，并显示节点的控制器关联；USART1 引脚登记使用板级端口字母，GPIO 复用查询正确提取 AF 编号。

## 2026-11-09

- 21:46（UTC+8）H7 MSC 接通公共探测、分区登记与 VFS 挂载；拔出和角色切换先确认传输停止，再取消请求并解除驱动引用，卸载等待旧文件引用释放。USB 存储补齐整盘 FAT12/16/32 挂载；板级 MscWriteEnabled 默认关闭写入，保留验证后开放入口。

- 20:45（UTC+8）鼠标消息链统一使用 MouseGeneralMessage，PS/2 按包头符号位恢复完整位移，USB 保留八位有符号解码；移除 HID 位移拆分，队列合并及光标、窗口移动检查宽整数边界，同步 H7 鼠标例程。

- 20:39（UTC+8）H7 MSC 按端点包长限制单次最多 8 KiB，读写改用设备持有的堆缓冲；USB 服务推进超时取消，确认通道停止并清理 FIFO 后结束命令，停止失败保留缓冲。OTG Bulk 检查长度及底层提交结果。

- 15:38（UTC+8）H7 OTG2 内置 FS PHY 初始化保留 USB 睡眠时钟、关闭 ULPI 睡眠时钟，适配 RTOS 空闲任务的 WFI。

- 15:15（UTC+8）修复 x86/x64 切换分辨率后光标仍受启动尺寸限制的问题，光标绝对与相对移动改用当前屏幕窗口尺寸裁剪。

- 14:09（UTC+8）H7 USB OTG Host 接入现有 OTGHostDevice 枚举器：端口稳定等待、复位和恢复分阶段推进，URB 完成经持久通道记录交给 USB 服务线程，支持枚举超时有限重试及拔出清理。Unisym 增加实际毫秒推进接口并保留无参路径，Hub 等待适配实际时间；H7 接入 USB 宿主分配器，时序和重试参数放入板级 unicfg。

## 2026-11-08

- 20:58（UTC+8）`void RgbLcd_PumpPresentation();` H7 硬件光标移动改为 VBlank 一致提交，分离逻辑位置、待生效与已显示窗口，合并连续移动；通过重载完成中断和 Graphic 服务循环推进最后请求，保留初始化与迁层脱离的即时提交。


- 12:41（UTC+8）实现 H7 多图层 Stage D 的层链、层面绘图与光标迁层。
	- 屏幕持有 Dchain，各硬件层绑定独立 LayerManager2；层面按实际 pitch 和 RGB565/ARGB8888 格式读写、裁剪。
	- Cursor 迁入第二层，以层内坐标绘图、屏幕坐标定位；移动只更新覆盖窗口，替换静态红块。
	- 同步修正点击、悬停、置顶和窗体遍历对首项光标的依赖，保留软件光标及迁层失败回退。

- 10:35（UTC+8）将 Graphic 的触摸设备登记、帧消费、主指光标转换与鼠标按键合并移入 `graphic-touch.cpp`，保留原有输入行为。

- 完成触摸接口与 H7 GUI 接入重构。
	- Unisym 使用 Point/Rectangle 描述触点和范围，查询接口统一使用 get/is 命名，按设备容量分配 UniquePtr 存储；Touch.cpp 实现帧槽交接、硬件 ID 跟踪、主指事件、取消与解绑。
	- GT 驱动直接写采集槽，应答重试保留原槽；原始接收区和兼容快照移入持久存储。
	- Graphic 借用只读帧处理点击和拖动，主指抬起后等待全部离屏；移除大帧结构、复制队列和重复跟踪。

## 2026-11-07

- 17:48（UTC+8）实现 H7 多图层 Stage A。
	- 屏驱增加私有 ARGB8888 面描述、带源行距/长度检查的裁剪提交；使用 0xC00BC000 的 2 KiB 前缓冲，容量 16×32、pitch=64，另外 2 KiB 保留。
	- 初始化时提交 alpha=127 的半透明红色测试面，以 15×24 窗口显示在 (200,150)。
	- 启用覆盖层前检查像素读回及相邻 guard，失败保留单主层；增加初始化寄存器输出。

- 15:56（UTC+8）修复 STM32H7 开启 `CONFIG_DEBUG_STACKSIZE=1` 后的启动 UsageFault。
	- 切换到 PSP 时显式清除 `CONTROL.FPCA`，使实际异常栈帧与 H7 汇编使用的八字基本帧一致，修复栈错位导致的零返回地址。
	- 移除临时切换采样、转储函数和约 6 KiB 环形缓冲区；汇编文字池前显式对齐。

## 2026-11-06

- 修复 x86/x64 存储等待与唤醒：复用定时 IRQ 的 Callback 路径，使 FileSys、Devsman 可以等待完成通知，不再依赖 Devsman 派发自身的唤醒。
- 用栈上链表节点登记同控制器的多个等待者；在调度锁内登记、阻塞和通知，以等待编号隔离迟到定时器，并在线程销毁前移除节点。
- 存储等待开销优化
	- 公共等待保留初始短轮询；运行期有 IRQ 时按原始剩余期限安排唤醒，无 IRQ 时每 tick 阻塞后检查，初始化及 IF 关闭时仍有限轮询。
	- AHCI 仅在启用中断完成时获取完成锁；保留两条路径的错误检测、状态清理和原有事务 Mutex。
	- SCSI 删除无 IRQ 时重复的 10ms 延迟轮询，保留最早请求期限服务和无请求时取消定时器。
- 存储恢复结果统一
	- Unisym 增加统一恢复结果，分别表示原提交路径可用、受影响请求 DMA 已停止、对应引擎及共享 DMA 区已停止；不混同命令成败。
	- BusLogic、LSI MPI 提供详细恢复结果并保留原 bool Recover()；复位、请求失败处理、重新初始化及 SAS 目标身份验证顺序保持不变。
	- AHCI 按引擎停止结果处理共享区；NVMe 将请求停止依据传到数据缓冲收尾；PATA 保留 ATA 忙状态对 PIO 回退的限制；SCSI 不重复处理请求生命周期。
