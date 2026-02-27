// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>


#if (_MCCA & 0xFF00) == 0x8600
TSS_t* PCU_CORES_TSS[PCU_CORES_MAX] = {};
#endif

Dchain Taskman::chain = { DnodeHeapFreeSimple };;// ordered by pid
stduint Taskman::min_available_pid;// in chain
Dnode* Taskman::min_available_left;;

stduint Taskman::PCU_CORES = 0;
stduint Taskman::pcurrent[PCU_CORES_MAX];


bool Taskman::Append(ProcessBlock* task) {
	task->pid = min_available_pid;
	auto nod = chain.Append(task, false, min_available_left);
	stduint las = min_available_pid;
	Dnode* lasn = nod;
	while (nod = nod->next) {
		stduint _new = ((ProcessBlock*)(nod->offs))->pid;
		if (_new == las + 1) {
			lasn = nod;
			las = _new;
		}
		else break;
	}
	min_available_pid = las + 1;
	return true;
}

ProcessBlock* Taskman::Locate(stduint taskid) {
	for (auto nod = chain.Root(); nod; nod = nod->next) {
		auto task = cast<ProcessBlock*>(nod->offs);
		if (task->pid == taskid) return task;
	}
	return nullptr;
}

#if _MCCA == 0x8664
extern NormalTaskContext task_b_ctx, task_kernel_ctx;
// auto Taskman::Schedule(void* timeout, ...)->decltype(Schedule(timeout))
auto Taskman::Schedule()->decltype(Schedule())
{
	stduint cnt = Taskman::chain.Count();
	if (cnt <= 1) return;
	stduint cpuid = _TEMP 0;
	auto cpu_old = Taskman::pcurrent[cpuid];
	auto cpu_new = Taskman::pcurrent[cpuid];
	cpu_new = (1 + cpu_new) % cnt;
	Taskman::pcurrent[cpuid] = cpu_new;
	SwitchTaskContext(&treat<ProcessBlock>(chain[cpu_new]->offs).context,
		&treat<ProcessBlock>(chain[cpu_old]->offs).context);
}
#endif

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

	Dnode* crt_node = nullptr;
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		if (((ProcessBlock*)(nod->offs))->pid == ProcessBlock::cpu0_task) {
			crt_node = nod;
			break;
		}
	}

	stduint cpu0_new = ProcessBlock::cpu0_task;
	do if (ProcessBlock::cpu0_rest) {
		crt_node = Taskman::chain.Root();
		while (crt_node && ((ProcessBlock*)(crt_node->offs))->pid != ProcessBlock::cpu0_rest) crt_node = crt_node->next;
		
		crt_node = crt_node ? crt_node->next : Taskman::chain.Root();
		if (!crt_node) crt_node = Taskman::chain.Root();

		cpu0_new = crt_node ? ((ProcessBlock*)(crt_node->offs))->pid : 0;
		ProcessBlock::cpu0_rest = 0;
		if (cpu0_new == 0) {
			crt_node = crt_node->next ? crt_node->next : Taskman::chain.Root();
			cpu0_new = crt_node ? ((ProcessBlock*)(crt_node->offs))->pid : 0;
		}
	}
	else {
		crt_node = crt_node ? crt_node->next : Taskman::chain.Root();
		if (!crt_node) crt_node = Taskman::chain.Root();
		cpu0_new = crt_node ? cast<ProcessBlock*>(crt_node->offs)->pid : 0;
	}
	while (cpu0_new == ProcessBlock::cpu0_task || (cast<ProcessBlock*>(crt_node->offs)->state != PBS::Ready && cast<ProcessBlock*>(crt_node->offs)->state != PBS::Uninit));
	ProcessBlock::cpu0_task = cpu0_new;
	auto pb_des = cast<ProcessBlock*>(crt_node->offs);

	if (pb_des->state == PBS::Uninit) pb_des->state = PBS::Ready;
	if ((pb_src->state == PBS::Running) && (pb_des->state == PBS::Ready)) {
		pb_src->state = PBS::Ready;
		pb_des->state = PBS::Running;
	}
	else {
		printlog(_LOG_FATAL, "task switch error (PID%u, State%u, PCnt%u).", pb_des->getID(), _IMM(pb_des->state), Taskman::chain.Count());
	}
	task_switch_enable = true;//{TODO} Unlock
	jmpTask(T_pid2tss(ProcessBlock::cpu0_task));
}



#endif
