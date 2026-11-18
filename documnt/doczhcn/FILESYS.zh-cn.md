# 文件系统

## 查询文件属性

`stat(path, &info)` 按路径查询文件属性，`fstat(fd, &info)` 查询已打开文件描述符关联的 inode。成功返回 0，失败返回 -1 并设置 `errno`。

| 查询或操作 | 权限要求与结果 |
| --- | --- |
| `stat("/a/b", &info)` | 要求经过的目录允许搜索，无需目标文件的读、写或执行位 |
| 目标普通文件权限为 `0000` | 父目录可搜索且路径存在时，`stat` 可以成功 |
| 路径中的目录不允许搜索 | 非 root 查询返回 `EACCES`，UID 0 沿用普通权限位豁免 |
| 查询目录本身的属性 | 检查路径前缀的搜索权限，最终目录本身无需可读 |
| 读取文件内容 | 打开时需要文件读取权限，与查询属性分别检查 |
| 列出目录内容 | 需要能沿路径找到目录，并以可读方式打开该目录 |
| `fstat` 查询 `O_WRONLY` fd | 可以成功，无需将文件重新以只读方式打开 |
| 文件 unlink 后查询原 fd | fd 有效时仍可查询原 inode 的基本属性 |
| 卸载后查询仍有效的 fd | 使用文件引用保留的 inode 和挂载状态取得基本属性 |
| 只读或禁止执行挂载 | 挂载限制不阻止属性查询 |

`stat` 直接查询 VFS，不消耗临时文件描述符，也不会为了查询而打开、读取或截断文件。`fstat` 保留 inode 引用后读取属性快照；查询期间另一个线程关闭 fd，不会使已保留的 inode 失效。

## 返回字段

| 字段 | 含义与来源 |
| --- | --- |
| `st_size` | inode 记录的文件大小 |
| `st_mode` | 文件类型和权限位 |
| `st_uid` | inode 的属主 UID |
| `st_gid` | inode 的属组 GID |
| `st_mtime` | 后端提供的修改时间，沿用现有本地时间转换；无有效时间时为 0 |

VFS 通过后端 `permit` 读取原生权限，缺少的字段保留挂载或设备策略。UDF 使用有效的原生 UID/GID/mode；FAT 保留挂载合成的 UID/GID 和基本 mode，并用磁盘只读属性限制写权限，普通可写属性不会额外授予挂载默认值以外的写权限。管道、socket 和设备可以通过有效 fd 查询已有 inode 的基本属性，管道大小沿用 inode 记录值。

修改时间属于可选后端信息。后端不支持、已无法取得有效时间或时间查询失败时，基本属性查询仍可成功，`st_mtime` 为 0。当前后端路径查找和时间接口尚未完整区分设备 I/O 错误与其他失败。

## 常见错误

| errno | 含义 |
| --- | --- |
| `EBADF` | fd 无效、已关闭或没有关联 inode |
| `ENOENT` | 路径为空或目标不存在 |
| `EACCES` | 路径中的目录不允许搜索 |
| `ENOTDIR` | 路径前缀不是目录，或普通文件后带有 `/` |
| `ENAMETOOLONG` | 路径或单个分段超过当前支持的长度 |
| `ESTALE` | 相对路径使用的工作目录已失效 |
| `EFAULT` | 用户路径或结果缓冲区无法访问 |
| `EOVERFLOW` | 返回的 UID/GID 无法用用户态类型表示 |

## PROP 接口与兼容

`PROP(object, output, query)` 使用第三参数选择查询方式。`LegacyDescriptor` 查询 fd 并输出原 `file_proper_t`；`Descriptor` 查询 fd 并输出包含 UID/GID 的 `file_status_t`；`Path` 查询路径并输出同一扩展结果。旧调用第三参数为 0，对应 `LegacyDescriptor`，内核按原结果长度复制。

新版共享 `struct stat` 增加 `st_uid/st_gid`，应用与 libc 应使用匹配的头文件和结构布局。MMU 和 noMMU 共用属性查询规则；noMMU 对无效地址的处理能力受现有平台内存访问机制限制。

## 文件系统后端权限接口

`FilesysTrait::permit(void* handler, FilesysPermissionResult& result, bool set_or_get = false)` 为原生权限接口，false 表示读取，true 表示设置。调用方持有有效文件句柄，负责串行化后端访问，并在设置前检查调用者身份和挂载限制。

`result.fields.uid/gid/mode` 在读取时表示哪些返回字段有效，在设置时表示要求修改哪些字段。`result.mode_mask` 表示支持或要求修改的 mode 位，范围为 `07777`，不包含文件类型。零 UID、零 GID、零 mode 均是有效值。返回 true 表示成功，false 时通过 `result.status` 区分 Unsupported、ReadOnly、InvalidArgument、IoError、NoMemory。

| 后端 | 读取原生权限 | 设置权限 |
| --- | --- | --- |
| FAT 普通文件 | 返回只读属性对应的统一写权限开关，`mode_mask` 为 `0222`；UID/GID 无效 | 可以保存磁盘只读属性位；要求仅选择 mode，`mode_mask` 为 `0222`，写权限同时全部打开或全部关闭 |
| FAT 目录、根目录和已 unlink 的句柄 | Unsupported | Unsupported |
| UDF | 读取磁盘 UID/GID 和转换后的 mode；未指定的 UID/GID 对应字段无效 | 当前后端返回 ReadOnly |
| ISO9660 | Unsupported，当前未解码 Rock Ridge 权限 | ReadOnly |
| RootFs | 读取内存 inode 的 UID/GID/mode | 修改指定字段，保存于内存，重启后不保留 |
| DevFs | Unsupported，权限来自 VFS 设备策略 | Unsupported |
| 设备树文件系统 | Unsupported，权限来自 VFS 策略 | ReadOnly |

例如，设置 FAT 普通文件只读，需要将 `fields.mode` 设为 true、`mode_mask` 设为 `0222`、`mode` 的这三个位全部清零。清除只读属性则将这三个位全部置位。FAT 无法分别保存 owner/group/other 的写权限，也不保存 Unix 读、执行位或属主属组，不能直接把完整 `0644` 当作可持久化的 FAT 权限提交。

VFS 在路径授权和属性查询时读取并合并原生权限。不支持原生权限时使用已有默认值；读取失败返回错误。权限修改在后端成功后更新缓存 inode，后续路径检查与 `stat` 使用相同元数据。

UDF 位转换参考 [ECMA-167 对应的 Linux 定义](https://github.com/torvalds/linux/blob/master/fs/udf/ecma_167.h)，覆盖各类读写执行位以及 set-ID、sticky 标志。

## 修改权限和属主

libc 提供 `chmod(path, mode)`、`fchmod(fd, mode)`、`chown(path, owner, group)` 和 `fchown(fd, owner, group)`。成功返回 0，失败返回 -1 并设置 errno。路径形式检查目录搜索权限，fd 形式使用已经打开的 inode，不要求 fd 可写。

| 操作 | 授权规则 |
| --- | --- |
| `chmod/fchmod` | root 或文件属主可以修改 `0000..7777` 范围内的权限及特殊位，文件类型保持原值 |
| root 调用 `chown/fchown` | 可设置支持范围内的 UID/GID |
| 普通属主调用 `chown/fchown` | UID 只能保留原值，GID 可以保留原值或设为调用进程当前 GID；当前没有附加组 |
| owner 或 group 为 `-1` | 对应字段保持原值；二者均为 `-1` 时通过基本授权检查后直接成功 |
| 属主或属组修改 | 清除 setuid/setgid 位，指定字段和特殊位作为同一次后端请求提交 |
| 只读挂载 | 返回 `EROFS`，包括 root |
| 后端无法保存请求的权限 | 返回 `ENOTSUP`，不会仅修改缓存并报告成功 |

普通属主设置 mode 时，文件 GID 与调用进程当前 GID 不匹配会清除请求中的 setgid 位。现有 fd 和文件映射保留此前授予的访问能力，权限修改影响后续打开与路径操作。

| 后端 | `chmod/fchmod` | `chown/fchown` |
| --- | --- | --- |
| RootFs | 支持，结果保存于内存 | 支持，结果保存于内存 |
| FAT | 完整 Unix mode 当前返回 `ENOTSUP` | 返回 `ENOTSUP` |
| ISO9660、UDF、设备树 | 返回 `EROFS` | 返回 `EROFS` |
| DevFs | 返回 `ENOTSUP` | 返回 `ENOTSUP` |

命令 `chmod [--] OCTAL_MODE FILE...` 接受八进制权限，例如 `chmod 0755 /home`。命令 `chown [--] OWNER[:GROUP] FILE...` 接受用户名、组名及十进制数字身份，例如 `chown user:user /home`、`chown user:1000 /home`；`:GROUP` 形式只设置属组。全数字 ID 无需存在账户记录，未知名称或格式错误在修改任何文件前报错。命令支持多个路径，逐项输出失败及 errno，任一项失败时退出状态为 1。ls 长格式优先显示用户名和组名，查无记录或查询失败时回退为数字。

PROP 的查询类型追加 `ChangeModeDescriptor`、`ChangeModePath`、`ChangeOwnerDescriptor`、`ChangeOwnerPath`。第二参数指向 `file_permission_request_t` 输入，包含 mode、uid、gid，原有三种属性查询布局保持有效。

授权和 `-1` 语义参考 [POSIX chmod](https://man7.org/linux/man-pages/man3/chmod.3p.html) 与 [POSIX chown](https://man7.org/linux/man-pages/man3/chown.3p.html)。当前文件状态查询尚未提供 ctime。

## 创建属性与 umask

open 在 O_CREAT 时读取第三参数 mode，mkdir 使用传入的目录 mode。用户库、OPEN syscall 和 Fileman 将它传到 VFS；调用者必须提供 mode，不能再使用省略第三参数的 O_CREAT 调用。当前创建接口接受基础 rwx 位，初始权限为 `(mode & 0777) & ~umask`，特殊位不通过请求 mode 设置；setgid 父目录的继承由 VFS 单独处理。

进程默认 umask 为 0022，fork 和 EXEC 继承，EXET 与 su 保留。umask(mask) 返回旧值，只保留 mask 的 0777 位，不影响已有文件及 chmod。COTLAB 提供单独命令形式的内建 umask：无参数显示当前八进制掩码，带参数设置 000..777；不提供管道或重定向形式。

| 创建请求 | umask | RootFs 初始权限 |
| --- | --- | --- |
| 普通文件 0666 | 0022 | 0644 |
| 普通文件 0666 | 0077 | 0600 |
| 目录 0777 | 0022 | 0755 |
| 目录 0777 | 0077 | 0700 |

VFS 检查路径逐级搜索、父目录写入和搜索权限及只读挂载。RootFs 新对象的 UID 为创建者 UID，GID 通常为创建者主 GID；父目录有 setgid 时继承父目录 GID，新子目录同时继承 setgid 位。创建时先提交初始属性，再发布目录项，不通过创建后的 chmod/chown 补写。新对象的最终 mode 不反过来拒绝本次创建所获得的 fd，例如 O_WRONLY 创建 0400 文件仍可通过初始 fd 写入，关闭后再次打开则按 0400 检查。已存在文件忽略创建 mode，O_CREAT|O_EXCL 则返回 EEXIST。

`FilesysTrait::create(rostr fullpath, stduint flags, FilesysCreateParameter& parameter)` 将创建相关参数收拢为一个结构引用。成员按 handler、linkdest、attributes、exinfo 排列；exinfo 保留为可选通用扩展信息，不定义为父目录信息。

| 成员 | 约定 |
| --- | --- |
| handler | 调用前是父目录句柄；成功后替换为新对象句柄；失败时保留输入值 |
| linkdest | 可选链接目标，借用输入字符串，支持情况由后端决定 |
| attributes | 可选初始 UID/GID/mode，借用输入指针；需要保存的值由后端复制 |
| exinfo | 放在结构最后的可选通用扩展信息，具体含义和方向由后端约定；当前这些后端未使用 |

VFS 将父目录句柄放进 parameter.handler，将创建属性放进 parameter.attributes，成功后将 parameter.handler 保存到 inode，不再通过 void** 或 exinfo 传递父目录/结果句柄。FAT 在修改磁盘前分配输出句柄，失败路径释放该临时句柄且不改写输入 handler；这项约定不等于新增了 FAT 磁盘事务或崩溃恢复能力。

RootFs 接收并保存完整初始属性；FAT 不能保存逐文件 Unix 属性，保留挂载合成的 UID/GID/mode 与原生只读属性，不伪造逐文件权限缓存。只读文件系统仍拒绝创建。接口迁移同步覆盖 FAT、ISO9660/UDF、RootFs、DevFs、设备树和 x86 引导加载器中的 FAT 桩实现；这是原生 C++ 文件系统接口变化，不改变 OPEN/UMSK 的用户态 syscall ABI。

RootFs 支持内存文件和目录创建、文件读写、截断、稀疏写入零填充、普通文件 unlink，以及现有 VFS 规则下的改名。文件内容和元数据由 inode 所有的句柄保存，删除名称后打开的 fd/映射仍保留原对象，最后一个 inode 引用释放时回收内存；同名重建得到独立对象。目录删除、符号链接、覆盖目标的 rename 尚未增加。所有数据随本次启动结束而丢失。

这批修改需要重新构建内核、Unisym 文件系统代码、用户库、COTLAB 和应用（包括 Rust mcca-rt 的创建调用）；仅更新内核会使旧创建调用继续传入空 mode，不能据此验证新权限语义。MMU/noMMU 共用创建与掩码逻辑，noMMU 原本未实现的进程启动能力保持其现有范围。

### OPEN 参数与 Fileman 请求

OPEN 的三个参数依次为路径指针、flags、mode；编号仍以 include/syscall.hpp 为准。没有 O_CREAT 时第三参数忽略，可以传 0；有 O_CREAT 时必须传入 mode，0 表示请求 0000 权限，不能作为缺省权限标记。

用户库 open(path, flags, ...) 只在 O_CREAT 时读取可变参数 mode。当前 mode_t 为机器字宽，直接编写可变参数调用时应使用 mode_t 类型，例如 C++ 的 `mode_t(0666)` 或 C 的 `(mode_t)0666`。mkdir(path, mode) 经 O_CREAT|O_EXCL|O_DIRECTORY 传递目录 mode。fopen、shell 输出重定向和文件复制使用普通文件请求权限 0666，mkdir 通常请求 0777。

FilemanMsg::OPEN 的消息布局为四个机器字：flags、调用线程 TID、路径指针、mode。syscall 与 Devsman 的内部打开请求均使用此布局，mode 不是用户缓冲区指针。Fileman 按调用进程取得 UID/GID 和 umask 的一致快照；创建父目录的搜索、写入权限及只读挂载由 VFS 检查。

成功返回非负 fd，失败返回负 errno，用户库转换为 -1 并设置 errno。OPEN 第三参数已启用，依赖此前省略 mode 或传 0 表示默认权限的创建调用必须迁移。需要同步重建内核、Unisym、用户库、COTLAB 和应用；不能只替换内核后继续使用旧创建调用验证。

### UMSK 参数

UMSK 第一参数为新 mask，第二、第三参数不使用，建议传 0。保存值为 mask&0777，成功返回修改前的掩码；调用时没有当前进程则返回 -ESRCH。正常用户程序不需要特权即可修改自己的 umask。libc 声明为 `mode_t umask(mode_t mask)`，位于 sys/stat.h。

COTLAB 无参数显示 umask 时使用 umask(0) 后恢复旧值，是两个调用，不能当作多线程程序的原子查询接口。独立子程序调用 umask 不会修改父 shell。

建议在重新打包并启动的新内核下，以 root 创建独立测试目录，避免修改整个 /home 的属性。以下命令中的 permcheck 应使用本次启动尚未存在的目录名：

```sh
mkdir /home/permcheck
chown user:user /home/permcheck
chmod 0700 /home/permcheck
su user
cd /home/permcheck
whoami
id
umask
```

预期 whoami 为 user，UID/GID 为 1000，尚未调整过的默认 umask 为 0022。后续创建和权限拒绝检查都在这个普通用户 shell 中进行；root 会绕过普通 mode 检查，不能用 root 验证拒绝访问。

再执行：

```sh
umask 0077
echo private > private.txt
mkdir private-dir
umask 0022
echo shared > public.txt
mkdir public-dir
echo more >> private.txt
ls -l .
```

RootFs 上 private.txt 应为 user:user、0600，private-dir 应为 user:user、0700，public.txt 应为 user:user、0644，public-dir 应为 user:user、0755；改变 umask 并追加写入后，原有 private.txt 权限仍为 0600。进一步验证初始只读 mode 的可写创建 fd、关闭后重开授权、已有文件 O_EXCL、普通文件删除后持有 fd 的读取，以及 setgid 目录的组继承。以上为待运行验证项，不代表已完成运行测试。

普通用户下再执行 `chmod 0400 public.txt`、`echo denied > public.txt`、`cat public.txt`：重定向应报打开失败，cat 仍能读取原内容，说明已有文件没有被 O_CREAT 的 mode 重新授权或截断。之后用 `chmod 0600 public.txt` 恢复。再次执行 `mkdir private-dir` 应报已存在，原目录保持不变。上述失败是预期的负面检查。
