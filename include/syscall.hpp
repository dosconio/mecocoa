#ifndef SYSCALL_HPP_
#define SYSCALL_HPP_

enum
	#ifdef _INC_CPP
	class
	#endif
	syscall_t
	#ifdef _INC_CPP
	: stduint
	#endif
{
	OUTC = 0x00, // outstr (chr/str, len)->0  |.x86 x64 rv
	INNC = 0x01, // innstr (blocked)->ASCII>0 | x86 x64 rv
	EXIT = 0x02, // exit   (code)             | x86 x64 rv
	TIME = 0x03, // getsec (0sec/1ms)->second | x86 x64
	REST = 0x04, // halt   (unit, time)       | x86 x64
	COMM = 0x05, // syncom (mod, obj, &msg)   | x86 x64 rv
	// [Fileops]
	OPEN = 0x06, // open   (path,flags)>=0    | x86 x64
	CLOS = 0x07, // close  (fd)->0            | x86 x64
	READ = 0x08, // read   (fd, adr, len)->len| x86 x64
	WRIT = 0x09, // write  (fd, adr, len)->len| x86 x64
	DELF = 0x0A, // remove (pathname)->?      | x86 x64
	PORP = 0x0B, // proper (fd, &proper)->0   | x86
	ENUM = 0x0C, // enumer (fd,&kde,cnt)->cnt | x86
	// [Process]
	WAIT = 0x0D, // wait   (pid, &status)->pid| x86 x64    | pid 0 for any
	FORK = 0x0E, // fork   ()->pid            | x86 x64
	TMSG = 0x0F, // trymsg ()->(msg_unsovled) | x86 x64 rv
	EXEC = 0x10, // spawn  (path,argv,envp)->0| x86 x64
	EXET = 0x11, // exec   (path,argv,envp)->0| x86 x64
	PFUN = 0x12, // procfn () |(TODO) | priority ...
	// [Signals]
	SIGA = 0x13, // sigaction (sig,&new,&old) | x86
	KILL = 0x14, // kill      (pid, sig, tid) | x86
	SIGR = 0x15, // sigreturn ()              | -
	// [Fileops Extend]
	SETD = 0x16, // chdir  (path)->status     | x86
	GETD = 0x17, // getcwd (buf, size)->len   | x86
	MMAP = 0x18, // mmap   (size,fl,fd)->addr | x86
	UMAP = 0x19, // munmap (addr,size) ->0    | x86
	DUP2, // dup2   (oldfd, newfd)->newfd | x86
	PIPE, // pipe   (pipefd)->0           | x86

	// [Management]
	GET_CORE_ID, // getcid () | rv
	MANA, // manage (func, op1, op2)->?|(TODO)      | shutdown reboot...

	// [Threads]
	TNEW, // thread_create (entry, arg, stack_top)->tid  | x86 x64
	TEXI, // thread_exit   (exit_code)                   | x86 x64
	TJOI, // thread_join   (tid, &exit_code)->status     | x86 x64
	TGET, // thread_self   ()->tid                       | x86 x64
	TDET, // thread_detach (tid)->status                 | x86 x64
	TYLD, // thread_yield  ()->0                         | x86 x64
	FUTX, // futex         (addr, op, val)->status       | x86 x64

	// [Network]
	SOCK, // socket (domain, type, protocol)->fd        | x86 x64
	BIND, // bind   (fd, addr, len)->status             | x86 x64
	CONN, // conn   (fd, addr, len)->status             | x86 x64
	SEND, // send   (fd, payload, len)->len             | x86 x64
	RECV, // recv   (fd, payload, len)->len             | x86 x64
	ROUT, // route  (func, p1, p2)->status              | x86 x64
	FCTL, // fcntl  (fd, cmd, arg)->status             | x86 x64 rv
	SADR, // socket address (fd, query, func)->status   | x86 x64
	SOPT, // socket option (fd, query, func)->status    | x86 x64
	POLL, // poll   (fds, nfds, timeout)->ready count  | x86 x64
	LIST, // listen (fd, backlog)->status              | x86 x64

	DBUG = 0xFE, // sysinfo_classic to stdout(func)
	TEST = 0xFF, // getpid (T,E,S)->0 | x86

	// [Powercall] by Ring1
	POWERCALL_HELLO = 0x10000,// () -> 0
	POWERCALL_DEV_OPEN,// (node_id, class, flags) -> dev_handle
	POWERCALL_DEV_CLOSE,// (dev_handle, 0, 0) -> 0
	POWERCALL_DEV_PROPER,// (dev_handle, proper, args) -> status
	POWERCALL_DEV_READ,// (dev_handle, adr, len) -> len
	POWERCALL_DEV_WRITE,// (dev_handle, adr, len) -> len
	POWERCALL_DEV_CTRL,// (dev_handle, cmd, args) -> status
	POWERCALL_DEV_MMAP,// (dev_handle, args, 0) -> addr
	POWERCALL_DEV_UMAP,// (addr, size, flags) -> 0
	POWERCALL_DEV_IO_READ,// (dev_handle, args, 0) -> status
	POWERCALL_DEV_IO_WRITE,// (dev_handle, args, 0) -> 0
	POWERCALL_DEV_WAIT,// (dev_handle, timeout, flags) -> event
	POWERCALL_DEV_ACK,// (dev_handle, event, flags) -> 0
	POWERCALL_DEV_DMA_ALLOC,// (dev_handle, size, flags) -> dma_handle
	POWERCALL_DEV_DMA_FREE,// (dma_handle, 0, 0) -> 0
	POWERCALL_DEV_DMA_MAP,// (dma_handle, info, flags) -> 0
	POWERCALL_DEV_PUBLISH,// (dev_handle, cmd, args) -> status
};// . stand for well for multi-thread
// Locks usually end with `_lock;`

enum
	#ifdef _INC_CPP
	class
	#endif
	syscall_linux_t
	#ifdef _INC_CPP
	: stduint
	#endif
{
	_
};

#define IRQ_SYSCALL 0x81// leave 0x80 for unix-like syscall

#ifndef _ACCM
#if (_MCCA & 0xFF00) == 0x8600

_ESYM_C void Handint_SYSCALL_Entry();
_ESYM_C void Handint_INTCALL_Entry();
_ESYM_C stduint Handint_SYSCALL(CallgateFrame* frame);

#elif (_MCCA & 0xFF00) == 0x1000

struct NormalTaskContext;
void syscall(NormalTaskContext* cxt);


#endif
#else
#endif
struct file_proper_t {
	stduint size = 0;					// File size in bytes
	stduint mode = 0;					// File mode (type and permissions)
};

struct dirent_t {
	stduint is_dir = 0;					// Directory flag (0: file, 1: directory)
	char name[64] = {};					// File name
};

struct syscall_net_send_t {
	const void* payload = nullptr;
	stduint length = 0;
	const void* address = nullptr;
	stduint address_length = 0;
};

struct syscall_net_recv_t {
	void* payload = nullptr;
	stduint capacity = 0;
	void* address = nullptr;
	stduint* address_length = nullptr;
};

struct syscall_net_socket_address_t {
	void* address = nullptr;
	stduint* address_length = nullptr;
};

struct syscall_net_socket_option_t {
	stduint level = 0;
	stduint option_name = 0;
	void* option_value = nullptr;
	stduint option_length = 0;
	stduint* result_length = nullptr;
};

struct syscall_pollfd_t {
	int fd = 0;
	sint16 events = 0;
	sint16 revents = 0;
};

enum class syscall_net_socket_address_func_t : stduint {
	Local = 0,
	Peer,
};

enum class syscall_net_socket_option_func_t : stduint {
	Set = 0,
	Get,
};

constexpr stduint syscall_net_io_flag_wait = 0x0001u;
constexpr stduint syscall_net_msg_flag_dontwait = 0x0040u;
constexpr stduint syscall_net_socket_level_socket = 1u;
constexpr stduint syscall_net_socket_option_reuse_address = 2u;
constexpr sint16 syscall_poll_in = 0x0001;
constexpr sint16 syscall_poll_out = 0x0004;
constexpr sint16 syscall_poll_error = 0x0008;
constexpr sint16 syscall_poll_hangup = 0x0010;
constexpr sint16 syscall_poll_invalid = 0x0020;

enum class syscall_net_route_func_t : stduint {
	IPv4Default = 0,
	IPv4InterfaceCount,
	IPv4Interface,
	IPv4ArpCacheCount,
	IPv4ArpCacheEntry,
};

constexpr uint16 syscall_net_route_flag_up = 0x0001u;
constexpr uint16 syscall_net_route_flag_gateway = 0x0002u;

struct syscall_net_route_ipv4_t {
	uint8 destination[4] = {};
	uint8 netmask[4] = {};
	uint8 gateway[4] = {};
	uint16 flags = 0;
	uint16 link_index = 0;
};

struct syscall_net_interface_ipv4_t {
	uint8 address[4] = {};
	uint8 netmask[4] = {};
	uint8 hardware[6] = {};
	uint16 flags = 0;
	uint16 mtu = 0;
	uint16 link_index = 0;
	uint16 link_state = 0;
	char name[32] = {};
};

struct syscall_net_arp_ipv4_t {
	uint8 address[4] = {};
	uint8 hardware[6] = {};
	uint16 flags = 0;
	uint16 entry_index = 0;
};

_ESYM_C stduint syscall(syscall_t callid, stduint p1 = 0, stduint p2 = 0, stduint p3 = 0);// MCCA 4 PARA SYSC

struct Syscall {
	static void Initialize();
};

#endif
