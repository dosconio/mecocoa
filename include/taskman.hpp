
#ifndef TASKMAN_HPP_
#define TASKMAN_HPP_

#include <c/task.h>
#include <cpp/queue>
#include <c/system/paging.h>
#include <cpp/Device/_Timer.hpp>
#include <cpp/trait/BlockTrait.hpp>

#include "syscall.hpp"

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
		RUPT_KBD,
		RUPT_FLUSH,
	} type;
	union {
		struct MsgTimer timer;
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
extern TSS_t* PCU_CORES_TSS[PCU_CORES_MAX];

#if _MCCA == 0x8632
#define T_pid2tss(pid) (SegTSS0 + 16 * pid)
#define T_tss2pid(tssid) ((tssid - SegTSS0) / 16)
#endif
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
#if (_MCCA & 0xFF00) == 0x8600
class FileDescriptor;
class _Comment(Kernel) ProcessBlock {
public:
	alignas(16) NormalTaskContext context;// advanced TSS_t
	stduint pid;
	inline stduint getID() { return pid; }
public:
	stduint stack_size;
	byte* stack_lineaddr;
	byte* stack_levladdr;
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
		// Waiting,// call wait()
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
		POSIX,
		Win32,
	} interface_type = InterfaceType::POSIX;
public:
	uni::Paging paging;
	//{} Mempool heappool
public: // _Comment(Taskman);
	uni::Slice load_slices[8];// at most 8 slices, app-relative logical address
	
public:// old design: have not updated completely
	#if _MCCA == 0x8632

	descriptor_t LDT[0x100 / byteof(descriptor_t)];
	TSS_t TSS;// aka state-frame
	stduint kept_intermap[1];
	word focus_tty_id;
	stduint processor_id;// running on which cpu core
	stduint exit_status;

	// before syscall, for `fork`
	stduint before_syscall_eax;
	stduint before_syscall_ecx;
	stduint before_syscall_edx;
	stduint before_syscall_ebx;
	stduint before_syscall_ebp;
	stduint before_syscall_esi;
	stduint before_syscall_edi;
	stduint before_syscall_data_pointer;// esp
	stduint before_syscall_code_pointer;
	//

	// File
	FileDescriptor* pfiles[_TEMP 4];
	#endif
};
#endif

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
	static auto
		Create(void* entry, byte ring) -> ProcessBlock*;// newProcess
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




#if _MCCA == 0x8632
#include "fileman.hpp"

extern "C" bool task_switch_enable;

enum class TaskmanMsg {
	TEST,
	EXIT,
	FORK,
	WAIT,
	EXEC,
};

ProcessBlock* TaskRegister(void* entry, byte ring);

#endif
#if (_MCCA & 0xFF00) == 0x8600

ProcessBlock* TaskLoad(uni::BlockTrait* source, void* addr, byte ring);//{TODO} for existing R1

#endif
#if _MCCA == 0x8632


inline ProcessBlock* TaskGet(stduint taskid) { return Taskman::Locate(taskid); }

// return zero for success
int msg_send(ProcessBlock* fo, stduint to, _Comment(vaddr) CommMsg* msg);
int msg_recv(ProcessBlock* to, stduint fo, _Comment(vaddr) CommMsg* msg);

void rupt_proc(stduint pid, stduint rupt_no);

enum {
	Task_Kernel,
	Task_Con_Serv,
	Task_Hdd_Serv,
	Task_FileSys,
	Task_TaskMan,
	Task_Init,
	Task_AppC,
	//
	TaskCount
};



inline static stduint syssend(stduint to_whom, const void* msgaddr, stduint bytlen, stduint type = 0)
{
	struct CommMsg msg { nil };
	msg.data.address = _IMM(msgaddr);
	msg.data.length = bytlen;
	msg.type = type;
	return syscall(syscall_t::COMM, COMM_SEND, to_whom, &msg);
}
inline static stduint sysrecv(stduint fo_whom, void* msgaddr, stduint bytlen, stduint* type = NULL, stduint* src = NULL)
{
	struct CommMsg msg { nil };
	msg.data.address = _IMM(msgaddr);
	msg.data.length = bytlen;
	if (type) msg.type = *type;
	if (src) msg.src = *src;
	stduint ret = syscall(syscall_t::COMM, COMM_RECV, fo_whom, &msg);
	if (type) *type = msg.type;
	if (src) *src = msg.src;
	return ret;
}
#endif

#endif
