
#ifndef TASKMAN_HPP_
#define TASKMAN_HPP_

#include "fileman.hpp"

extern "C" bool task_switch_enable;

struct CommMsg {
	Slice data;
	stduint type;
	stduint src;// use if type is HARDRUPT
};

class FileDescriptor;
// = TaskBlock = ThreadBlock
struct _Comment(Kernel) ProcessBlock {
	static stduint cpu_proc_number;
	static stduint cpu0_task;
	static stduint cpu0_rest;
	static void* table_ready;
	static void* table_pends;

	Paging paging;
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
	Slice load_slices[8];// at most 8 slices, app-relative logical address

	//{} threads

	// state
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

	enum BlockReason : byte {
		BR_None = 0,
		BR_Interrupt = 0b1,
		BR_SendMsg = 0b10,
		BR_RecvMsg = 0b100,
		BR_Waiting = 0b1000,
	} block_reason = BlockReason::BR_None;
	inline void Block(BlockReason reason) {
		state = State::Pended;
		block_reason = BlockReason(block_reason | reason);
	}
	inline void Unblock(BlockReason reason) {
		block_reason = BlockReason(block_reason & ~reason);
		if (block_reason == BlockReason::BR_None)
			state = State::Ready;
	}

	CommMsg* unsolved_msg;
	stduint send_to_whom;// 0 for none (cannot comm with base-kernel)
	stduint recv_fo_whom;// 0 for ANY
	stduint wait_rupt_no;// equals vector plus one. 0 for none, 1 for ZeroException...

	// if B->A, C->A. Then
	// A.qhead = B
	// B.qnext = C
	// C.qnext = none
	stduint queue_send_queuehead;// 0 for none
	stduint queue_send_queuenext;

	// File
	FileDescriptor* pfiles[_TEMP 4];



	bool is_suspend = false;// to Disk
	//{TODO} suspend information

	stduint getID();

};

enum class TaskmanMsg {
	TEST,
	EXIT,
	FORK,
	WAIT,
};

ProcessBlock* TaskRegister(void* entry, byte ring);
ProcessBlock* TaskLoad(BlockTrait* source, void* addr, byte ring);//{TODO} for existing R1

stduint TaskAdd(ProcessBlock* task);
ProcessBlock* TaskGet(stduint taskid);// get task block by its id

void switch_task();
void switch_halt();

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


#define ANYPROC (_IMM0)
#define INTRUPT (~_IMM0)

#define COMM_RECV 0b10
#define COMM_SEND 0b01
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
