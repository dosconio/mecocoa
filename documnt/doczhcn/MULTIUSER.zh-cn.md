# 用户管理

当前实现采用内置账户表和单套进程 UID/GID。账户资料、身份切换和认证规则由 `Userman` 提供；VFS 等模块只接收数字凭据快照，不读取账户表或密码。

## 内置账户

| 用户名 | UID | 主组名 | GID | 家目录 | shell | 原型密码 |
| --- | --- | --- | --- | --- | --- | --- |
| root | 0 | root | 0 | `/` | `/md0/cot` | `p` |
| user | 1000 | user | 1000 | `/home` | `/md0/cot` | `p` |

账户和组记录目前编译在 `mecocoa/userman.cpp` 中，不读取 `/etc/passwd`、`/etc/group` 或 shadow 文件，也没有运行时增删账户和修改密码接口。固定密码 `p` 用于这一阶段的功能接通。公开账户查询返回的密码字段为 `x`，不会返回认证口令。

root 执行 `su` 时可以直接选择目标账户；普通用户切换账户时需要输入 `p`。终端当前仍由内核 `read()` 回显，密码输入暂时也会显示。启动流程继续直接进入原有 root shell，尚未改为启动登录界面。

## 命令

| 命令 | 行为 |
| --- | --- |
| `id` | 显示当前 UID、主 GID 和能够解析的账户名、组名；没有名字时保留数字 |
| `whoami` | 根据当前有效 UID 查询用户名；没有对应账户时报错 |
| `su user` | 切换当前命令进程为 user，进入 `/home` 并执行 `/md0/cot` |
| `su root` 或 `su` | 切换为 root；普通用户需要输入 `p` |

`su` 本身已经是调用 shell 创建的命令进程，所以认证成功后直接通过 execve 替换自身，不再额外 fork。退出新 shell 后回到原调用 shell。新 shell 的 USER、LOGNAME、HOME、SHELL 按目标账户重新构造，PATH 保留调用环境配置，缺失或为空时回退为 `/md0`，以保留系统卷 apps 目录的命令搜索路径；其余环境变量不继承。家目录访问和 shell 执行都发生在目标身份提交之后；失败则退出命令，不回退启动 root shell。

`/home` 的默认 owner/group/mode 不会因为增加账户而自动变化。需要让 user 独占该目录时，先以 root 执行 `chown 1000:1000 /home` 和 `chmod 0700 /home`。RootFs 上的修改只保留在本次启动期间；FAT 的原生权限支持范围参见文件系统文档。chmod 仍使用八进制 mode；chown 支持用户名、组名及数字身份，ls 长格式优先显示账户名和组名，查询不到名称时保留数字。

### 文件属主与属组命令

| 命令 | 行为 |
| --- | --- |
| `ls -l /` | 长格式显示名称；没有对应用户或组、名称查询失败时显示数字 UID/GID |
| `chown user:user /home` | 同时设置属主与属组为 user |
| `chown user:1000 /home` | 用户名与数字 GID 混用 |
| `chown 1000:user /home` | 数字 UID 与组名混用 |
| `chown :user /home` | 只设置属组，保留原属主 |
| `chown user /home` | 只设置属主，保留原属组 |
| `chown 1234:1234 /home` | 数字 ID 不要求账户表中存在对应记录，之后 ls 可以显示为数字 |

chown 的全数字参数按十进制 ID 解释，有效范围为 `0..2147483647`，可与名称混用。名称不存在、数字越界、空参数、空组名或多个冒号均报错；先解析完整用户和组参数，再修改目标文件。`user:` 与 `:` 当前不接受。继续支持初始 `--` 和多个文件路径，名称解析不改变内核的属主修改授权及文件系统后端能力。

## 内核接口与授权

`ProcessCredentials` 定义在 `include/userman.hpp`，仅包含数字 UID/GID，不依赖账户文件、Taskman 数据结构或认证实现。ProcessBlock 私有保存凭据和对应 Mutex，外部模块通过 Userman 读取快照、初始化、继承或修改身份。读取完成后释放凭据锁，再进入文件系统、映射或其他子系统。

| 接口 | 行为 |
| --- | --- |
| `GetCredentials` | 返回同一次持锁期间取得的 UID/GID 副本 |
| `InitializeCredentials` | 仅用于尚未发布的进程，初始化不再获取目标进程锁 |
| `InheritCredentials` | 先读取父进程快照，再初始化尚未发布的子进程 |
| `SetUserID` | root 可以设置合法数字 UID；普通用户只允许保持当前 UID |
| `SetGroupID` | 根据当前 UID 判断权限；root 可以设置合法数字 GID，普通用户只允许保持当前 GID |
| `Login` | root 直接选择账户，其他调用者验证固定密码；成功后在同一把锁内提交目标 UID/GID |
| `MayControl` | 提供 root 或相同 UID 的进程操作身份规则；信号入口继续保留服务进程保护 |

GETI/SETI 保留现有 ABI，ID 有效范围仍为 `0..0x7FFFFFFF`。当前没有分别实现 real/effective/saved UID/GID，GETI 的有效身份查询与普通身份查询对应同一份凭据。单独 setuid 降权后不能直接 setuid 恢复 root；再次提升必须经过认证入口。setuid/setgid 与 chown 接受合法数字身份，不要求数字一定存在于账户表。

fork 和 EXEC 继承凭据快照，EXET 保留当前进程身份；主 ELF 与解释器执行授权使用同一份加载凭据。已经开始的授权操作使用其取得的快照，后续操作重新取得身份。MANA 的关机和重启操作要求 UID 0。

## 用户库和 syscall

query 使用 syscall_user_query_t，按头文件定义选择 UserByName、UserByID、GroupByName、GroupByID 或 Login，不凭文档手写枚举值。按名称查询时 key 为字符串指针，按数字查询时为 UID/GID；argument 为 syscall_account_buffer_t 指针，包含 data、size、uid、gid。调用者提供字符串缓冲，成功返回 0，size 写回所需字节数；缓冲不足返回 -ERANGE，记录不存在返回 -ENOENT。

用户查询的字符串依次为 name、password-placeholder、gecos、home、shell；组查询依次为 name、password-placeholder，均以 NUL 结束。内核不返回内部指针或真实口令。四个 libc 可重入查询在没有记录时返回 0 且 result 为空，其他失败返回正错误码。

Login 的 key 为用户名指针，argument 改为密码字符串指针。成功返回 0，并同时提交当前进程 UID/GID；失败保留原身份。当前原型内置 root/user，非 root 调用者验证固定密码 p，root 免密选择账户。UserLogin 将负 errno 转换为 -1 并设置 errno。它不是强制密码登录入口，未来 login 程序不能直接依赖 root 免密路径执行认证。

`pwd.h` 提供 getpwnam_r/getpwuid_r 和 Mecocoa 扩展 UserLogin；`grp.h` 提供 getgrnam_r/getgrgid_r。四个可重入查询使用调用者缓冲区，未找到记录时返回 0 且 result 为空，其他失败返回错误码。UserLogin 成功返回 0，失败返回 -1 并设置 errno。当前只有主组，gr_mem 返回空成员数组，尚未实现附加组授权。

系统卷通过 `.mcca.root` 识别。该卷原本为默认 `0644` 的普通文件合成权限改为 `0755`，供 apps 程序和 ELF 解释器使用；后端原生 mode 仍优先，只读属性仍限制写权限，no_execute 仍禁止执行。其他数据卷保留原挂载默认权限。

## 平台范围

| 路径 | 当前支持 |
| --- | --- |
| x86/x64 | 公共 Userman、USER、GETI/SETI、凭据继承与 su 的现有 exec 路径 |
| RISC-V | 公共 Userman 和显式 USER、GETI/SETI、MANA syscall 分派；不新增原本缺失的交互 shell 启动流程 |
| noMMU / H743 | 公共账户查询、认证和数字身份接口，ELF 初始化使用同一 Userman 接口；现有平坦映像路径尚未实现 EXEC/EXET，相关调用明确返回 ENOSYS，避免发送没有接收处理分支的请求后等待 |

noMMU 的账户记录不会自动启动 `/md0/cot`。后续支持指定用户直接加载应用时，必须在映像打开和执行授权之前确定目标凭据，不能先按 root 加载再事后修改 UID。

## 文件创建策略

进程创建掩码默认 0022，可通过 libc umask 和 COTLAB 内建 umask 修改；fork/EXEC 继承，EXET/su 保留。RootFs 现在可以创建内存文件及目录，初始 UID/GID/mode 取调用者和创建请求；setgid 父目录决定继承的组。FAT 仍使用挂载合成权限，不具备逐文件 Unix 创建属性保存能力。具体规则和验证命令见 FILESYS.zh-cn.md 的创建属性章节。

## 后续扩展

账户文件读写、持久化密码、关闭密码回显、启动登录界面、账户工具自动创建家目录、附加组以及完整 real/effective/saved 身份模型尚待实现。未来可以由 Userman 工作线程处理账户文件更新和认证请求；凭据快照和文件权限检查继续在本地完成，不能让 VFS 授权依赖向 Userman 发起同步 IPC，也不能在持有凭据锁或 VFS 锁时等待该线程。
