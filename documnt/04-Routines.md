---
dg-publish: true
her-note: false
---

20250707のAzusa(ArinaMgk) decided:
- Kernel  Ring0
- Service Ring1 (aka. Driver)
- Module  Ring2
- Subapp  Ring3


## Routines for Flap-32

Gate Segment `8*03` and Interrupt 81H.

Rule
- Input
	- EAX syscall-id
	- ECX para 1
	- EDX para 2
	- EBX para 3
- Output
	- EAX

`enum class syscall_t`

| No. | Identification | Function | IO | Description |
| --- | --- | --- | --- | --- |
| 00 | OUTC | sysouts | (chr/str, len)→0 | 输出到当前 tty |
| 01 | INNC | sysinnc | (blk, &keycode, &props)→ascii | 取一个输入事件：返回字符，键码与状态由指针带回；props bit0 为 1 表示松开 |
| 02 | EXIT | | (code) | 结束进程 |
| 03 | TIME | syssecond<br>sysdate<br>systime | (0/1/0x10/0x11)→val | 0: 运行秒数<br>1: 运行毫秒数<br>0x10: BCD 日期 0xYYYYMMDD<br>0x11: BCD 时间 0x00HHMMSS |
| 04 | REST | sysrest | (unit, time) | 停机直到下一次中断 |
| 05 | COMM | syscomm | (mod, obj, &msg) | 同步（阻塞）收发消息 |
| 06 | OPEN | open<br>sysopen<br>sys_createfil | (path, flags, mode)→fd | 打开或创建文件，O_CREAT 时使用 mode |
| 07 | CLOS | | (fd)→0 | 关闭文件 |
| 08 | READ | | (fd, adr, len)→len | 读文件或 tty |
| 09 | WRIT |  | (fd, adr, len)→len | 写文件或 tty |
| 0A | DELF | sys_removefil | (pathname)→0 | 删除文件 |
| 0B | MOVE | rename | (oldpath, newpath, unused)→status | 同一挂载内改名；成功为 0，失败为负 errno |
| 0C | PROP | | (fd/path, &result, query)→status | 查询文件属性，query 为 0 时保留旧 fd 结果布局 |
| 0D | ENUM | | (fd, &kde, cnt)→cnt | 枚举目录项 |
| 0E | WAIT | | (pid, &status)→pid | 等待进程，pid 传 0 表示任意 |
| 0F | FORK | | ()→pid | 创建进程 |
| 10 | TMSG | | ()→msg | 非阻塞取消息 |
| 11 | EXEC | spawnve<br>spawnl | (path, argv, envp)→0 | 创建并执行 |
| 12 | EXET | execv<br>execl | (path, argv, envp)→0 | 替换当前进程执行 |
| 13 | PFUN | | () | 进程属性，**TODO** |
| 14 | SIGA | | (sig, &new, &old) | 设置信号处理 |
| 15 | KILL | | (pid, sig, tid) | 发送信号 |
| 16 | SIGR | | () | 从信号处理返回 |
| 17 | SETD | | (path)→status | 切换工作目录 |
| 18 | GETD | | (buf, size)→len | 取当前工作目录 |
| 19 | MMAP | | (size, fl, fd)→addr | 映射内存。`fl != ~0`：原三参数语义；`fl == ~0`：第三参数指向固定大小 `syscall_mmap_request_t`，字段为 `address/prot/flags/fd/offset`，`size` 仍为第一参数；文件偏移以字节计，失败返回 `~0`。 |
| 1A | UMAP | | (addr, size)→0 | 解除映射 |
| 1B | SMAP | mprotect | (addr, size, prot)→0 | 更改映射保护 |
| 1C | DUP2 | | (oldfd, newfd)→newfd | 复制文件描述符 |
| 1D | PIPE | | (pipefd)→0 | 创建管道 |
| 1E | SEEK | | (fd, offset, whence)→pos | 移动读写位置 |
| 1F | GETI | get_core_id<br>getpid<br>pthread_self<br>getuid / geteuid<br>getgid / getegid | (type)→id | 查询当前核心、进程、线程或用户身份 |
| 20 | SETI | setuid<br>setgid | (type, id)→status | 设置当前进程的 UID/GID；root 可设置合法数字身份，普通用户只能保持当前值 |
| 21 | MANA | sysshutdown<br>sysreboot | (func, op1, op2) | 关机与重启，要求当前 UID 为 0 |
| 22 | TNEW | | (entry, arg, stack_top)→tid | 创建线程 |
| 23 | TEXI | | (exit_code) | 结束线程 |
| 24 | TJOI | | (tid, &exit_code)→status | 等待线程 |
| 25 | TGET | | ()→tid | 取当前线程编号 |
| 26 | TDET | | (tid)→status | 分离线程 |
| 27 | TYLD | | ()→0 | 让出时间片 |
| 28 | FUTX | | (addr, op, val)→status | 快速用户态互斥 |
| 29 | SOCK | | (domain, type, protocol)→fd | 创建套接字 |
| 2A | BIND | | (fd, addr, len)→status | 绑定地址 |
| 2B | CONN | | (fd, addr, len)→status | 连接 |
| 2C | SEND | | (fd, payload, len)→len | 发送 |
| 2D | RECV | | (fd, payload, len)→len | 接收 |
| 2E | ROUT | | (func, p1, p2)→status | 路由表操作 |
| 2F | FCTL | | (fd, cmd, arg)→status | ioctl |
| 30 | SADR | | (fd, query, func)→status | 套接字地址 |
| 31 | SOPT | | (fd, query, func)→status | 套接字选项 |
| 32 | POLL | | (fds, nfds, timeout)→ready count | 轮询就绪 |
| 33 | LIST | | (fd, backlog)→status | 监听 |
| 34 | ACPT | | (fd, addr, addr_len)→fd | 接受连接 |
| 35 | SCLS | | (fd, how)→status | 关闭套接字方向 |
| 36 | IOCT | | (fd, request, arg)→status | 设备控制 |
| 37 | USER | getpwnam_r / getpwuid_r<br>getgrnam_r / getgrgid_r<br>getgroups / setgroups<br>getgrouplist / initgroups<br>UserLogin | (query, key, argument)→status | 查询公开账户及组成员、读写附加组，或认证并切换当前进程身份 |
| 38 | UMSK | umask | (mask, unused, unused)→old_mask | 原子替换当前进程创建掩码，保存 mask 的 0777 位并返回旧值 |
| FE | DBUG | | (func) | 内核信息输出到 stdout |
| FF | TEST | | ('T','E','S')→pid | 输入 TES 则反馈成功，并标注是哪个进程导致的 |

`GETI` 与 `SETI` 的 `type` 使用 `syscall_id_type_t`。`GETI(Core)` 在 RISC-V 返回 hart ID，其余目标返回内核核心编号。MOVE 插入 DELF 后，PROP 到 UMSK 的编号顺延；旧用户程序需要用匹配的 ACCM 重新链接，直接调用 syscall 的程序也要同步编号。ACCM 的 `get_core_id(stduint*)` 保持原接口。

Powercall 从 `0x10000` 起，由 Ring1 使用，编号见 `syscall.hpp` 的 `POWERCALL_*`。
