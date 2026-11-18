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

命令 `chmod [--] OCTAL_MODE FILE...` 接受八进制权限，例如 `chmod 0755 /home`。命令 `chown [--] UID[:GID] FILE...` 接受数字身份，例如 `chown 1000:1000 /home`；`:GID` 形式只设置属组。命令支持多个路径，逐项输出失败及 errno，任一项失败时退出状态为 1。当前命令使用数字身份和八进制 mode。

PROP 的查询类型追加 `ChangeModeDescriptor`、`ChangeModePath`、`ChangeOwnerDescriptor`、`ChangeOwnerPath`。第二参数指向 `file_permission_request_t` 输入，包含 mode、uid、gid，原有三种属性查询布局保持有效。

授权和 `-1` 语义参考 [POSIX chmod](https://man7.org/linux/man-pages/man3/chmod.3p.html) 与 [POSIX chown](https://man7.org/linux/man-pages/man3/chown.3p.html)。当前文件状态查询尚未提供 ctime。
