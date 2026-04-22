
#ifndef TASKMAN_HPP_
#define TASKMAN_HPP_

#include <c/task.h>
#include <c/lock.h>
#include <cpp/queue>
#include <c/driver/mouse.h>
#include <c/system/paging.h>
#include <cpp/Device/_Timer.hpp>
#include <cpp/trait/BlockTrait.hpp>

#include "syscall.hpp"

enum {
	Task_Kernel,
	Task_TaskMan,
	Task_Console,
	Task_ConsoleVideo,// [inner of Task_Console] manage mice and GUI
	Task_FileSys,
	Task_Memdisk_Serv,
	Task_Hdd_Serv,
	Task_Init,
	Task_AppC,
	//
	TaskCount
};

void serv_sysmsg();

struct MsgTimer {
	stduint timeout;
	stduint iden;
	_tocall_ft hand;// Realtime Hook
};
struct MccaRectangle {
	stduint x, y, w, h;
	auto toRectangle() -> uni::Rectangle { return Rectangle(Point2(x, y), Size2(w, h)); }
};
struct SysMessage {
	enum Type {
		RUPT_xHCI,
		RUPT_TIMER,
		RUPT_MOUSE,
		RUPT_KBD,
		RUPT_FLUSH,
	} type;
	union {
		struct MsgTimer timer;
		MouseMessage mou_event;
		keyboard_event_t kbd_event;
		MccaRectangle rect;// RUPT_FLUSH
	} args;
};
extern uni::Queue<SysMessage> message_queue;

// >= 1
#ifndef PCU_CORES_MAX
#define PCU_CORES_MAX 16
#endif

#define HIGHER_STACK_SIZE 0x4000

#if (_MCCA & 0xFF00) == 0x8600
enum {
	RING_M = 0,
	RING_S = 1,
	RING_U = 3,
};
#elif (_MCCA & 0xFF00) == 0x1000// RISCV
enum {
	RING_M = 3,
	RING_S = 1,
	RING_U = 0,
};
#endif


#if (_MCCA & 0xFF00) == 0x8600
extern TSS_t* PCU_CORES_TSS[PCU_CORES_MAX];

#endif

// Message
static constexpr const stduint ANYPROC = (_IMM0);
static constexpr const stduint INTRUPT = (~_IMM0);
static constexpr const stduint COMM_RECV = 0b10;
static constexpr const stduint COMM_SEND = 0b01;

struct CommMsg {
	uni::Slice data;
	stduint type;
	stduint src;// use if type is HARDRUPT
};

// Process
#ifndef _ACCM
class ThreadBlock;
class FileDescriptor;
class CallgateFrame;

// ---- LOCK ---- //
struct Spinlock {
	byte locked = 0;
	stdsint cpu_id = 0;
	Spinlock() {}
	bool Acquire();
	void Release(bool old_if);
};
struct SpinlockLocal {
	bool old_if;
	Spinlock* spinlock;
	SpinlockLocal(Spinlock* spinl) : spinlock(spinl) {
		old_if = spinlock->Acquire();
	}
	/// @brief Drop the lock
	~SpinlockLocal() {
		auto sl = spinlock;
		spinlock = 0;
		if (sl) {
			sl->Release(old_if);
		}
	}
};

struct Mutex {
	Spinlock guard;
	byte locked = 0;
	uni::Queue<ThreadBlock*> wait_queue;
	Mutex() : wait_queue() {}
	void Acquire();
	void Release();
};
struct MutexLocal {
	Mutex* mutex;
	MutexLocal(Mutex* _mutex) : mutex(_mutex) {
		mutex->Acquire();
	}
	~MutexLocal() {
		mutex->Release();
	}
};

class _Comment(Kernel) ProcessBlock {
public:
	stduint pid;
	stduint parent_id;
	inline stduint getID() { return pid; }
	stduint ring;
	Mutex sys_lock{}; // Use Mutex to protect internal systems like Heap / FD
public:
	// Process Level Wait State
	enum class State : byte {
		Active = 0,
		Hanging,// aka Zobmie, call exit()
		Invalid,
	} state = State::Active;
	
	inline bool isWaiting();
	
	stduint exit_status;
public: // _Comment(Threads)
	// Currently maintaining a main thread for default usages (and fallback),
	// further multi-thread implementations can maintain a vector / chain here.
	ThreadBlock* main_thread = nullptr;
	ThreadBlock* thread_list_head = nullptr;
public: // _Comment(Interface);
	enum class InterfaceType : byte {
		MCCA4,// x86 will only support MCCA4
		Linux,
		Win32,
	} interface_type = InterfaceType::MCCA4;
public:
	uni::Paging paging;
	uni::Paging* paging_redirect = nullptr;
	stduint heaptop = 0;
	stduint heapbtm = 0;// norm: max seg + 0x10000
public: // _Comment(Taskman);
	uni::Slice load_slices[8];// at most 8 slices, app-relative logical address
public: // _Comment(Console);
	Dnode* focus_tty = nullptr;
	SheetTrait* pforms[_TEMP 4] = {};// should registered in global_layman
public: // _Comment(Fileman);
	FileDescriptor* pfiles[_TEMP 4];
	struct vnode* cwd = 0;  // 
	struct vnode* root = 0; // for chroot
	//
	ProcessBlock() {}
	auto Open(rostr pathname, int flags) -> stdsint;
	auto Rdwt(bool wr_type, stduint fid, Slice slice) -> stduint;
	auto Close(int fid) -> bool;
	auto Seek(int fd, stdsint off, int whence) -> stdsint;
};

class ThreadBlock {
public:
    alignas(16) NormalTaskContext context;// advanced TSS_t
    stduint tid;
    ProcessBlock* parent_process; 
	ThreadBlock* process_thread_next = nullptr;
	volatile stduint just_schedule;// in fact a bool
public:
	stduint stack_size;
	byte* stack_lineaddr;// [linear] ring3 bottom of stack
	// stack_levladdr: use phyzik. Because 0xFFFFFFFFC0001000ull or 0xFFC01000 is mapped to this.
	byte* stack_levladdr;// [phyzik] ring0, may be 0 or same with stack_lineaddr for ring0 task 
	
	sint8 priority = 0; // -16..-1 (Realtime RT) and 0..15 (Timeslice)
	uint8 time_slice = 0; // execution time left for timeslice mode
	bool is_expired = false; // true if task has expended its timeslice in the current epoch
public _Comment(State):
	enum class State : byte {
		Running = 0,
		Ready,
		Pended,
		Uninit,
		Exited,
		Hanging,
		Invalid,
	} state = State::Uninit;
	inline bool isWaiting() {
		return state == State::Pended &&
			(_IMM(block_reason) & _IMM(BlockReason::BR_Waiting));
	}
	enum BlockReason : uint32 {
		BR_None = 0,
		BR_SendMsg = 0b10,
		BR_RecvMsg = 0b100,
		BR_Waiting = 0b1000,
	} block_reason = BlockReason::BR_None;
	void Block(BlockReason reason);
	void Unblock(BlockReason reason);
	ThreadBlock* queue_state_prev = nullptr, * queue_state_next = nullptr;// for ready queue
	//
	stduint processor_id;// running on which cpu core
	stduint exit_status;
public: // _Comment(Syscomm)
	CommMsg* unsolved_msg = nullptr;
	ThreadBlock* send_to_whom = nullptr;// nullptr for none (cannot comm with base-kernel)
	ThreadBlock* recv_fo_whom = nullptr;// nullptr for ANY
	stduint wait_rupt_no = 0;// equals vector plus one. 0 for none, 1 for ZeroException...
	// if B->A, C->A. Then: A.qhead = B, B.qnext = C, C.qnext = none
	ThreadBlock* queue_send_queuehead = nullptr;// nullptr for none
	ThreadBlock* queue_send_queuenext = nullptr;
public:
	inline stduint getID() const { return tid; }
};

inline bool ProcessBlock::isWaiting() {
	return main_thread &&
		(_IMM(main_thread->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Waiting));
}

class Taskman {
	static const stduint DEFAULT_STACK_SIZE = 0x1000;

public:
	static stduint PCU_CORES;
	static ThreadBlock* current_thread[PCU_CORES_MAX];
	static ThreadBlock* idle_thread[PCU_CORES_MAX];
	static ThreadBlock* volatile switching_out_threads[PCU_CORES_MAX];
public:// Gen.2
	static Dchain chain;// [ArrayT] ordered by pid
	static stduint min_available_pid;// in chain
	static Dnode* min_available_left;// in chain
	
	static stduint min_available_tid;// in threads
public:// for local core
	static stduint getID() { return _TEMP 0; }// get core id
	static stduint  CurrentTID() { return current_thread[getID()]->tid; }
	static stduint  CurrentPID() { return current_thread[getID()]->parent_process->pid; }
public:
	static Dchain thchain;
	static Dnode* min_available_thleft;
public:
	static auto
		Initialize(stduint cpuid = 0) -> void;
	static auto
		Append(ProcessBlock* task) -> bool;
	static auto
		AppendThread(ThreadBlock* task) -> bool;
	static auto
		Locate(stduint taskid) -> ProcessBlock*;
	static auto
		LocateThread(stduint tid) -> ThreadBlock*;
public:
	// ring:
	// - Intel: 0 1 2 3
	// - RISCV: Mach3 Supe1 User0
	static auto// newProcess (BIN)
		Create(void* entry, byte ring, bool append = true) -> ProcessBlock*;
	static auto _TODO// newProcess (auto check)
		CreateFormat(BlockTrait* source, byte ring) -> ProcessBlock*;
	static auto// newProcess (ELF)
		CreateELF(BlockTrait* source, byte ring) -> ProcessBlock*;
	static auto// newProcess
		CreateFork(ProcessBlock* parent, const CallgateFrame* frame) -> ProcessBlock*;
	static auto// newProcess from file
		CreateFile(const char* path, byte ring, stduint parent) -> ProcessBlock*;

	static auto
		ExitCurrent(stduint code) -> bool;// call by syscall but taskman
public:// taskman
	static void Idle();
	static bool
		Exit(ProcessBlock* pb, stdsint exit_code);
	static auto
		Wait(ProcessBlock* pb) -> stdsint;
	static auto
		Exec(stduint parent, rostr usr_fullpath, void* usr_argstack, stduint stacklen) -> ProcessBlock*;
	static auto// aka POSIX-EXECV
		Exet(stduint parent, rostr usr_fullpath, void* usr_argstack, stduint stacklen) -> ProcessBlock*;
public:// schedule
	static auto Schedule(bool omit_slice = false) -> void;// Timer using
	struct ReadyQueue {
		ThreadBlock* head, * tail;
	};
	static ReadyQueue priority_queues[32];
	static ReadyQueue expired_queues[32];
	static unsigned int ready_bitmap;
	static unsigned int expired_bitmap;

	static void EnqueueReady(ThreadBlock* pb);
	static void DequeueReady(ThreadBlock* pb);
	static void EnqueueExpired(ThreadBlock* pb);
	static ThreadBlock* PickNext();
	static void SleepAndRelease(Spinlock* lk);
public:
	static auto// return a all-zero ProcessBlock
		AllocateTask() -> ProcessBlock*;
	static auto
		AllocateThread() -> ThreadBlock*;
	static void
		DumpTask(ProcessBlock*);
};
#endif

enum class TaskmanMsg : stduint {
	TEST,
	EXIT,
	FORK,
	WAIT,
	EXEC,
	EXET,
};


#if _MCCA == 0x8632
#include "fileman.hpp"
#endif


#ifndef _ACCM

// return zero for success
int msg_send(ThreadBlock* fo, stduint to, _Comment(vaddr) CommMsg* msg);
int msg_recv(ThreadBlock* to, stduint fo, _Comment(vaddr) CommMsg* msg);

void rupt_proc(stduint tid, stduint rupt_no);

inline static stduint syssend(stduint to_whom, const void* msgaddr, stduint bytlen, stduint type = 0)
{
	struct CommMsg msg = { };
	msg.data.address = _IMM(msgaddr);
	msg.data.length = bytlen;
	msg.type = type;
	return syscall(syscall_t::COMM, COMM_SEND, to_whom, _IMM(&msg));
}
inline static stduint sysrecv(stduint fo_whom, void* msgaddr, stduint bytlen, stduint* type = NULL, stduint* src = NULL)
{
	struct CommMsg msg = { };
	msg.data.address = _IMM(msgaddr);
	msg.data.length = bytlen;
	if (type) msg.type = *type;
	if (src) msg.src = *src;
	stduint ret = syscall(syscall_t::COMM, COMM_RECV, fo_whom, _IMM(&msg));
	if (type) *type = msg.type;
	if (src) *src = msg.src;
	return ret;
}
inline static stduint syssdrv(stduint whom, void* msgaddr, stduint bytlen, stduint* type = NULL)
{
	auto a = syssend(whom, msgaddr, bytlen, type ? *type : 0);
	return sysrecv(whom, msgaddr, bytlen, type);
}
#endif

#endif
