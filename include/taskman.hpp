
#ifndef TASKMAN_HPP_
#define TASKMAN_HPP_

#include <c/task.h>
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
	Task_Hdd_Serv,
	Task_FileSys,
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
class FileDescriptor;
class CallgateFrame;
class _Comment(Kernel) ProcessBlock {
public:
	alignas(16) NormalTaskContext context;// advanced TSS_t
	stduint pid;
	stduint parent_id;
	inline stduint getID() { return pid; }
	stduint ring;
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
		Hanging,// aka Zobmie, call exit()
		Invalid,
	} state = State::Uninit;
	enum BlockReason : uint32 {
		BR_None = 0,
		BR_SendMsg = 0b10,
		BR_RecvMsg = 0b100,
		BR_Waiting = 0b1000,
	} block_reason = BlockReason::BR_None;
	void Block(BlockReason reason);
	void Unblock(BlockReason reason);
	ProcessBlock* queue_state_prev = nullptr, * queue_state_next = nullptr;// for ready queue
	//
	stduint processor_id;// running on which cpu core
	stduint exit_status;
public: // _Comment(Syscomm)
	CommMsg* unsolved_msg = nullptr;
	ProcessBlock* send_to_whom = nullptr;// nullptr for none (cannot comm with base-kernel)
	ProcessBlock* recv_fo_whom = nullptr;// nullptr for ANY
	stduint wait_rupt_no = 0;// equals vector plus one. 0 for none, 1 for ZeroException...
	// if B->A, C->A. Then: A.qhead = B, B.qnext = C, C.qnext = none
	ProcessBlock* queue_send_queuehead = nullptr;// nullptr for none
	ProcessBlock* queue_send_queuenext = nullptr;
public: // _Comment(Thread)
public: // _Comment(Interface);
	enum class InterfaceType : byte {
		MCCA4,
		POSIX,
		Win32,
	} interface_type = InterfaceType::MCCA4;
public:
	uni::Paging paging;
	stduint heaptop = 0;
	stduint heapbtm = 0;// norm: max seg + 0x10000
public: // _Comment(Taskman);
	uni::Slice load_slices[8];// at most 8 slices, app-relative logical address
public: // _Comment(Console);
	Dnode* focus_tty = nullptr;
	SheetTrait* pforms[_TEMP 4] = {};// should registered in global_layman
public: // _Comment(Fileman);
	FileDescriptor* pfiles[_TEMP 4];
};
#endif

#ifndef _ACCM
class Taskman {
	static const stduint DEFAULT_STACK_SIZE = 0x1000;

public:
	static stduint PCU_CORES;
	static stduint pcurrent[PCU_CORES_MAX];
public:// Gen.2
	// Gen.1: fixed ProcessBlock* pblocks[16] and stduint pnumber
	static Dchain chain;// [ArrayT] ordered by pid
	static stduint min_available_pid;// in chain
	static Dnode* min_available_left;// in chain
public:// for local core
	static stduint getID() { return _TEMP 0; }// get core id
	static stduint& CurrentPID() { return pcurrent[getID()]; }// get core id
public:
	static auto
		Initialize(stduint cpuid = 0) -> void;
	static auto
		Append(ProcessBlock* task) -> bool;
	static auto
		Locate(stduint taskid) -> ProcessBlock*;
public:
	// ring:
	// - Intel: 0 1 2 3
	// - RISCV: Mach3 Supe1 User0
	static auto// newProcess (BIN)
		Create(void* entry, byte ring) -> ProcessBlock*;
	static auto// newProcess (ELF)
		CreateELF(BlockTrait* source, byte ring) -> ProcessBlock*;
	static auto// newProcess
		CreateFork(ProcessBlock* parent, const CallgateFrame* frame) -> ProcessBlock*;
	
	static auto
		ExitCurrent(stduint code) -> bool;
public:// schedule
	static auto Schedule(bool omit_slice = false) -> void;// Timer using
	struct ReadyQueue {
		ProcessBlock* head, * tail;
		// 0..15 (aka -16 .. -1): Realtime (B-Type), schedule higher priority strictly first. Default: 4 slices
		// 16..31 (aka 0 .. 15): Timeslice (A-Type), Active/Expired queues mechanism for guaranteed epoch access.
		// Slice scale: 0~3 => 4 | 4~7 => 3 | 8~11 => 2 | 12~15 => 1
	};
	static ReadyQueue priority_queues[32];
	static ReadyQueue expired_queues[32];
	static unsigned int ready_bitmap;
	static unsigned int expired_bitmap;

	static void EnqueueReady(ProcessBlock* pb);
	static void DequeueReady(ProcessBlock* pb);
	static void EnqueueExpired(ProcessBlock* pb);
	static ProcessBlock* PickNext();
public:
	static auto// return a all-zero ProcessBlock
		AllocateTask() -> ProcessBlock*;
};
#endif

enum class TaskmanMsg : stduint {
	TEST,
	EXIT,
	FORK,
	WAIT,
	EXEC,
};



#if _MCCA == 0x8632
#include "fileman.hpp"

extern "C" bool task_switch_enable;
#endif


#ifndef _ACCM
inline ProcessBlock* TaskGet(stduint taskid) { return Taskman::Locate(taskid); }

// return zero for success
int msg_send(ProcessBlock* fo, stduint to, _Comment(vaddr) CommMsg* msg);
int msg_recv(ProcessBlock* to, stduint fo, _Comment(vaddr) CommMsg* msg);

void rupt_proc(stduint pid, stduint rupt_no);

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
