# 用户管理

当前实现采用内置账户表和单套进程 UID/GID，支持动态附加组。账户资料、身份切换和认证规则由 `Userman` 提供；VFS 等模块只接收数字凭据快照，不读取账户表或密码。

## 内置账户

| 用户名 | UID | 主组名 | GID | 家目录 | shell | 原型密码 |
| --- | --- | --- | --- | --- | --- | --- |
| root | 0 | root | 0 | `/` | `/md0/cot` | `p` |
| user | 1000 | user | 1000 | `/home` | `/md0/cot` | `p` |

账户和组记录目前编译在 `mecocoa/userman.cpp` 中，不读取 `/etc/passwd`、`/etc/group` 或 shadow 文件，也没有运行时增删账户和修改密码接口。固定密码 `p` 用于这一阶段的功能接通。公开账户查询返回的密码字段为 `x`，不会返回认证口令。

root 执行 `su` 时可以直接选择目标账户；普通用户切换账户时需要输入 `p`。密码输入通过终端控制关闭回显，结束输入后恢复原设置。启动流程继续直接进入原有 root shell，尚未改为启动登录界面。

## 命令

| 命令 | 行为 |
| --- | --- |
| `id` | 显示当前 UID、主 GID 和完整组列表；主组只显示一次，没有名字时保留数字 |
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

`ProcessCredentials` 定义在 `include/userman.hpp`，包含数字 UID/GID 和附加组只读快照，不依赖账户文件、Taskman 数据结构或认证实现。ProcessBlock 私有保存凭据和对应 Mutex，外部模块通过 Userman 读取快照、初始化、继承或修改身份。读取完成后释放凭据锁，再进入文件系统、映射或其他子系统。

| 接口 | 行为 |
| --- | --- |
| `GetCredentials` | 返回同一次持锁期间取得的 UID/GID 和附加组快照 |
| `InitializeCredentials` | 仅用于尚未发布的进程，初始化不再获取目标进程锁 |
| `InheritCredentials` | 先读取父进程快照，再初始化尚未发布的子进程 |
| `SetUserID` | root 可以设置合法数字 UID；普通用户只允许保持当前 UID |
| `SetGroupID` | 根据当前 UID 判断权限；root 可以设置合法数字 GID，普通用户只允许保持当前 GID |
| `Login` | root 直接选择账户，其他调用者验证固定密码；成功后在同一把锁内提交目标 UID/GID 和完整账户组集合 |
| `SetGroups` | root 为当前进程替换整个附加组列表；先分配、排序、去重，提交时再次检查 UID，失败保留原列表 |
| `AccountGroups` | 查询目标账户的显式组成员关系，并加入指定基础 GID，返回排序去重的只读集合 |
| `HasGroup` | 匹配主 GID 或附加组，不查询账户表 |
| `MayControl` | 提供 root 或相同 UID 的进程操作身份规则；信号入口继续保留服务进程保护 |

GETI/SETI 保留现有 ABI，ID 有效范围仍为 `0..0x7FFFFFFF`。当前没有分别实现 real/effective/saved UID/GID，GETI 的有效身份查询与普通身份查询对应同一份凭据。单独 setuid 降权后不能直接 setuid 恢复 root；再次提升必须经过认证入口。setuid/setgid 与 chown 接受合法数字身份，不要求数字一定存在于账户表。

fork 和 EXEC 继承凭据快照，EXET 保留当前进程身份；主 ELF 与解释器执行授权使用同一份加载凭据。已经开始的授权操作使用其取得的快照，后续操作重新取得身份。MANA 的关机和重启操作要求 UID 0。

## 用户库和 syscall

query 使用 syscall_user_query_t，按头文件定义选择 UserByName、UserByID、GroupByName、GroupByID 或 Login，不凭文档手写枚举值。按名称查询时 key 为字符串指针，按数字查询时为 UID/GID；argument 为 syscall_account_buffer_t 指针，包含 data、size、uid、gid。调用者提供字符串缓冲，成功返回 0，size 写回所需字节数；缓冲不足返回 -ERANGE，记录不存在返回 -ENOENT。

用户查询的字符串依次为 name、password-placeholder、gecos、home、shell；组查询依次为 name、password-placeholder，均以 NUL 结束。内核不返回内部指针或真实口令。四个 libc 可重入查询在没有记录时返回 0 且 result 为空，其他失败返回正错误码。

Login 的 key 为用户名指针，argument 改为密码字符串指针。成功返回 0，并同时提交当前进程 UID/GID 和目标账户组集合；失败保留原身份和组集合。当前原型内置 root/user，非 root 调用者验证固定密码 p，root 免密选择账户。UserLogin 将负 errno 转换为 -1 并设置 errno。它不是强制密码登录入口，未来 login 程序不能直接依赖 root 免密路径执行认证。

`pwd.h` 提供 getpwnam_r/getpwuid_r 和 Mecocoa 扩展 UserLogin；`grp.h` 提供 getgrnam_r/getgrgid_r、setgroups/initgroups/getgrouplist，`unistd.h` 提供 getgroups。四个可重入查询使用调用者缓冲区，未找到记录时返回 0 且 result 为空，其他失败返回错误码。UserLogin 成功返回 0，失败返回 -1 并设置 errno。getgrnam_r/getgrgid_r 的 gr_mem 返回显式成员的用户名数组，以空指针结束；字符串和对齐后的指针数组全部放在调用者缓冲中，空间不足返回 ERANGE。

系统卷通过 `.mcca.root` 识别。该卷原本为默认 `0644` 的普通文件合成权限改为 `0755`，供 apps 程序和 ELF 解释器使用；后端原生 mode 仍优先，只读属性仍限制写权限，no_execute 仍禁止执行。其他数据卷保留原挂载默认权限。

## 附加组

附加组使用 `SharedPtr<const SupplementaryGroups>` 保存，内部由 `Vector<uint32>` 动态增长，不设置 16 个之类的固定数量上限。数量受接口返回值的 int 范围、地址与字节长度溢出检查和可分配内存限制；GID 有效范围为 `0..2147483647`，root 设置数字组不要求账户表中存在同名记录。集合发布前排序、去重，发布后只读；凭据快照和 fork/EXEC 共享集合，单个进程替换列表不影响父子进程已有的快照。替换时旧集合的最后引用在凭据锁外释放，不在锁内遍历、复制用户数组或释放大块列表。

内置新增 `shared` 组（GID 1001），显式成员为 user（UID 1000）。root 和 user 的主组仍分别为 0 和 1000；主组关系由账户主 GID 表示，不重复写入 gr_mem。初始开发用 root shell 保持现有启动流程，空附加组不影响 root 权限；`su user` 得到排序去重的 `1000,1001`，`su root` 得到 `0`。

| 接口或操作 | 规则 |
| --- | --- |
| `getgroups(0, nullptr)` | 返回当前保存的附加组条目数，不自动补入主 GID |
| `getgroups(size, list)` | 返回实际写入数；非零 size 不足返回 EINVAL；按本机 gid_t 宽度复制 |
| `setgroups(size, list)` | 仅 UID 0 可替换当前进程列表；重复项去重，0 个条目清空；非法 GID、拷贝或分配失败保留旧列表 |
| `getgrouplist(user, base, list, &count)` | 返回指定账户的显式附加组与 base 的并集；成功返回实际条目数，容量不足返回 -1、errno=ERANGE，并更新 count 为所需条目数 |
| `initgroups(user, base)` | 动态取得完整账户组列表后调用 setgroups；包含 base，不能绕过 root 授权 |
| `UserLogin` / `su` | 认证成功后同时替换 UID、主 GID 和目标账户组集合；不保留来源账户的附加组，umask 保留 |
| `setuid` / `setgid` | 保留附加组；普通用户仍不能通过属于某个附加组直接 setgid 到该组 |
| fork / EXEC / EXET | fork、EXEC 继承整个凭据快照，EXET 保留原进程身份和附加组 |
| `id` | groups 显示主 GID 与附加组的并集，主组不重复；未知 GID 保留数字 |

USER 在现有子操作之后追加 GetGroups、SetGroups、AccountGroups、GroupByNameMembers、GroupByIDMembers，原 syscall 编号和原子操作顺序不变，枚举项不手写编号。GetGroups 的 key 为容量、argument 为 gid_t 数组；SetGroups 的 key 为条目数、argument 为输入数组。AccountGroups 的 key 为用户名，argument 指向 `syscall_group_list_t { data, size, base, count }`：size 按条目计、count 回写所需数量，容量不足返回 -ERANGE。gid_t 当前是本机有符号机器字，内部 GID 是 uint32，x86/x64 分别转换复制，不能把两种数组直接强转复用。

原 GroupByName/GroupByID 继续返回 name、password-placeholder 两个 NUL 结尾字段；新增带 Members 的查询在这两个字段后追加成员名，各自以 NUL 结束，size 包含全部字符串。新版 libc 从该载荷构造 gr_mem，不向用户返回内核指针。MMU、noMMU 共用这些身份与组操作，沿用各平台现有的用户内存拷贝能力，不增加架构专属分支或补实现 noMMU 的 EXEC/EXET。

### 手动检查附加组

以 root 执行以下命令，RootFs 权限变化在本次启动有效。预期 user 能通过 shared 的组权限读取文件，普通 owner 对模式位的检查仍优先于组匹配。

```sh
mkdir /home/groupcheck
chown root:shared /home/groupcheck
chmod 0770 /home/groupcheck
echo shared-data > /home/groupcheck/readme
chown root:shared /home/groupcheck/readme
chmod 0640 /home/groupcheck/readme
su user
id
cat /home/groupcheck/readme
echo user-data > /home/groupcheck/owned
chown :shared /home/groupcheck/owned
chmod 2640 /home/groupcheck/owned
ls -l /home/groupcheck
chmod 0040 /home/groupcheck/owned
cat /home/groupcheck/owned
su root
id
```

user 的 id 应包含 `uid=1000(user) gid=1000(user) groups=1000(user),1001(shared)`；readme 可读，owned 的属组可改为 shared，设置 mode 2640 后 setgid 位保留；mode 0040 后 user 作为属主读取应失败，不能回退使用组读位。切回 root 后 groups 应只包含 root。API 层另需检查 getgroups 的容量查询和不足、超过 16 项及重复组排序去重、setgroups(0,nullptr) 清空、非 root setgroups（含清空）失败且旧组不变、initgroups 重新装载账户组、gr_mem 成员与小缓冲 ERANGE、fork/EXEC 继承及子进程替换不影响父进程；这些是待运行检查项。

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

账户文件读写、持久化密码、启动登录界面、账户工具自动创建家目录以及完整 real/effective/saved 身份模型尚待实现。未来可以由 Userman 工作线程处理账户文件更新和认证请求；凭据快照和文件权限检查继续在本地完成，不能让 VFS 授权依赖向 Userman 发起同步 IPC，也不能在持有凭据锁或 VFS 锁时等待该线程。
