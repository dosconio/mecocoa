// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/task.h>
#include <cpp/interrupt>
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>
#include "../include/taskman.hpp"


#if (_MCCA & 0xFF00) == 0x8600
TSS_t* PCU_CORES_TSS[PCU_CORES_MAX] = {};
#endif

ProcessBlock* Taskman::pblocks[16] = {};
stduint Taskman::pnumber = 0;
stduint Taskman::PCU_CORES = 0;



bool Taskman::Append(ProcessBlock* task) {
	pblocks[pnumber++] = task;
	return pnumber - 1;
}

ProcessBlock* Taskman::Locate(stduint taskid) {
	return Taskman::pblocks[taskid];
}

#ifdef _ARC_x86 // x86:

//{TODEL}
void switch_halt() {
	if (ProcessBlock::cpu0_task == 0) {
		return;
	}
	ProcessBlock::cpu0_rest = ProcessBlock::cpu0_task;
	auto pb_src = TaskGet(ProcessBlock::cpu0_task);
	auto pb_des = TaskGet(0);
	// stduint esp = 0;
	// _ASM("movl %%esp, %0" : "=r"(esp));
	// ploginfo("switch halt %d->0, esp: %[32H]", ProcessBlock::cpu0_task, esp);
	if (pb_des->state == ProcessBlock::State::Uninit)
		pb_des->state = ProcessBlock::State::Ready;
	if ((pb_src->state == ProcessBlock::State::Running || pb_src->state == ProcessBlock::State::Pended) && (
		pb_des->state == ProcessBlock::State::Ready)) {
		if (pb_src->state == ProcessBlock::State::Running)
			pb_src->state = ProcessBlock::State::Ready;
		pb_des->state = ProcessBlock::State::Running;
	}
	else {
		plogerro("task halt error.");
	}
	ProcessBlock::cpu0_task = 0;// kernel
	task_switch_enable = true;
	//
	//stduint save = pb_src->TSS.PDBR;
	//pb_src->TSS.PDBR = getCR3();// CR3 will not save in TSS? -- phina, 20250728
	jmpTask(SegTSS0/*, T_pid2tss(ProcessBlock::cpu0_rest)*/);
	//pb_src->TSS.PDBR = save;
}


// by only PIT
void switch_task() {
	if (ProcessBlock::cpu0_task == TaskCount) return;
	using PBS = ProcessBlock::State;

	task_switch_enable = false;//{TODO} Lock
	stduint last_proc = ProcessBlock::cpu0_task;
	auto pb_src = TaskGet(ProcessBlock::cpu0_task);

	stduint cpu0_new = ProcessBlock::cpu0_task;
	do if (ProcessBlock::cpu0_rest) {
		cpu0_new = (ProcessBlock::cpu0_rest + 1) % Taskman::pnumber;
		ProcessBlock::cpu0_rest = 0;
		if (cpu0_new == 0) {
			cpu0_new++;
		}
	}
	else {
		++cpu0_new %= Taskman::pnumber;
	}
	while (cpu0_new == ProcessBlock::cpu0_task || (TaskGet(cpu0_new)->state != PBS::Ready && TaskGet(cpu0_new)->state != PBS::Uninit));
	ProcessBlock::cpu0_task = cpu0_new;
	auto pb_des = TaskGet(ProcessBlock::cpu0_task);

	if (pb_des->state == PBS::Uninit) pb_des->state = PBS::Ready;
	if ((pb_src->state == PBS::Running) && (pb_des->state == PBS::Ready)) {
		pb_src->state = PBS::Ready;
		pb_des->state = PBS::Running;
	}
	else {
		printlog(_LOG_FATAL, "task switch error (PID%u, State%u, PCnt%u).", pb_des->getID(), _IMM(pb_des->state), Taskman::pnumber);
	}
	task_switch_enable = true;//{TODO} Unlock
	jmpTask(T_pid2tss(ProcessBlock::cpu0_task));
}



#endif
