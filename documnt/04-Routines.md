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
| 06 | OPEN | sysopen<br>sys_createfil | (path, flags)→fd | 打开或创建文件 |
| 07 | CLOS | | (fd)→0 | 关闭文件 |
| 08 | READ | | (fd, adr, len)→len | 读文件或 tty |
| 09 | WRIT |  | (fd, adr, len)→len | 写文件或 tty |
| 0A | DELF | sys_removefil | (pathname)→0 | 删除文件 |
| 0B | PROP | | (fd/path, &result, query)→status | 查询文件属性，query 为 0 时保留旧 fd 结果布局 |
| 0C | ENUM | | (fd, &kde, cnt)→cnt | 枚举目录项 |
| 0D | WAIT | | (pid, &status)→pid | 等待进程，pid 传 0 表示任意 |
| 0E | FORK | | ()→pid | 创建进程 |
| 0F | TMSG | | ()→msg | 非阻塞取消息 |
| 10 | EXEC | spawnve<br>spawnl | (path, argv, envp)→0 | 创建并执行 |
| 11 | EXET | execv<br>execl | (path, argv, envp)→0 | 替换当前进程执行 |
| 12 | PFUN | | () | 进程属性，**TODO** |
| 13 | SIGA | | (sig, &new, &old) | 设置信号处理 |
| 14 | KILL | | (pid, sig, tid) | 发送信号 |
| 15 | SIGR | | () | 从信号处理返回 |
| 16 | SETD | | (path)→status | 切换工作目录 |
| 17 | GETD | | (buf, size)→len | 取当前工作目录 |
| 18 | MMAP | | (size, fl, fd)→addr | 映射内存。`fl != ~0`：原三参数语义；`fl == ~0`：第三参数指向固定大小 `syscall_mmap_request_t`，字段为 `address/prot/flags/fd/offset`，`size` 仍为第一参数；文件偏移以字节计，失败返回 `~0`。 |
| 19 | UMAP | | (addr, size)→0 | 解除映射 |
| 1A | SMAP | mprotect | (addr, size, prot)→0 | 更改映射保护 |
| 1B | DUP2 | | (oldfd, newfd)→newfd | 复制文件描述符 |
| 1C | PIPE | | (pipefd)→0 | 创建管道 |
| 1D | SEEK | | (fd, offset, whence)→pos | 移动读写位置 |
| 1E | GETI | get_core_id<br>getpid<br>pthread_self<br>getuid / geteuid<br>getgid / getegid | (type)→id | 查询当前核心、进程、线程或用户身份 |
| 1F | SETI | setuid<br>setgid | (type, id)→status | 设置当前进程的 UID/GID，单 root 阶段只允许设为 0 |
| 20 | MANA | sysshutdown<br>sysreboot | (func, op1, op2) | 关机与重启 |
| 21 | TNEW | | (entry, arg, stack_top)→tid | 创建线程 |
| 22 | TEXI | | (exit_code) | 结束线程 |
| 23 | TJOI | | (tid, &exit_code)→status | 等待线程 |
| 24 | TGET | | ()→tid | 取当前线程编号 |
| 25 | TDET | | (tid)→status | 分离线程 |
| 26 | TYLD | | ()→0 | 让出时间片 |
| 27 | FUTX | | (addr, op, val)→status | 快速用户态互斥 |
| 28 | SOCK | | (domain, type, protocol)→fd | 创建套接字 |
| 29 | BIND | | (fd, addr, len)→status | 绑定地址 |
| 2A | CONN | | (fd, addr, len)→status | 连接 |
| 2B | SEND | | (fd, payload, len)→len | 发送 |
| 2C | RECV | | (fd, payload, len)→len | 接收 |
| 2D | ROUT | | (func, p1, p2)→status | 路由表操作 |
| 2E | FCTL | | (fd, cmd, arg)→status | ioctl |
| 2F | SADR | | (fd, query, func)→status | 套接字地址 |
| 30 | SOPT | | (fd, query, func)→status | 套接字选项 |
| 31 | POLL | | (fds, nfds, timeout)→ready count | 轮询就绪 |
| 32 | LIST | | (fd, backlog)→status | 监听 |
| 33 | ACPT | | (fd, addr, addr_len)→fd | 接受连接 |
| 34 | SCLS | | (fd, how)→status | 关闭套接字方向 |
| 35 | IOCT | | (fd, request, arg)→status | 设备控制 |
| FE | DBUG | | (func) | 内核信息输出到 stdout |
| FF | TEST | | ('T','E','S')→pid | 输入 TES 则反馈成功，并标注是哪个进程导致的 |

`GETI` 与 `SETI` 的 `type` 使用 `syscall_id_type_t`。`GETI(Core)` 在 RISC-V 返回 hart ID，其余目标返回内核核心编号。旧版核心 ID syscall 的参数和 `MANA` 到 `IOCT` 的编号发生变化，使用这些调用的旧程序需要用新 ACCM 重新链接，直接调用 syscall 的程序也要更新编号和参数。ACCM 的 `get_core_id(stduint*)` 保持原接口。

Powercall 从 `0x10000` 起，由 Ring1 使用，编号见 `syscall.hpp` 的 `POWERCALL_*`。
