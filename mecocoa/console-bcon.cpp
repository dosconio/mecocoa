// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - BareConsole
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

//// ---- ---- Bcon Impl ---- ---- ////
#ifdef _ARC_x86 // x86:

extern BareConsole Bcons[TTY_NUMBER];
extern Spinlock scheduler_lock;
static bool Bcons_CotAlive(ProcessBlock* p) {
	if (!p) return false;
	SpinlockLocal guard(&scheduler_lock);
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		if ((ProcessBlock*)nod->offs == p) {
			return p->state == ProcessBlock::State::Active;
		}
	}
	return false;
}
ProcessBlock* Bcons_EnsureCot(unsigned tty_no) {
	if (Consman::ento_gui || tty_no >= TTY_NUMBER) return nullptr;
	if (Bcons_CotAlive(Bcons_pcot[tty_no])) return Bcons_pcot[tty_no];
	Bcons_pcot[tty_no] = nullptr;

	auto tty_node = vttys[tty_no];
	if (!tty_node) return nullptr;

	ProcessBlock* p = Taskman::CreateFile("/md0/cot", RING_U, Task_Kernel);
	if (!p) return nullptr;

	{
		auto focus_tty = p->focus_tty.Lock();
		*focus_tty = tty_node;
		if (*focus_tty) {
			p->Open("/dev/tty", O_RDWR);
			p->Open("/dev/tty", O_RDWR);
			p->Open("/dev/tty", O_RDWR);
		}
	}

	Taskman::Append(p);
	Taskman::AppendThread(p->main_thread);

	if (tty_node->type) {
		auto pblock = (vtty_type_t*)tty_node->type;
		pblock->master_pid = p->pid;
		pblock->proc_group.Append(p->pid);
	}

	Bcons_pcot[tty_no] = p;
	return p;
}

void uni::BareConsole::doshow(void* _) {
	unsigned id = _IMM(_);
	if (id == Consman::current_screen_TTY) return;
	if (id >= TTY_NUMBER) {
		plogerro("TTY Id %u", id);
		return;
	}
	Bcons[Consman::current_screen_TTY].last_curposi = curget();
	Bcons[id].setStartLine(topline + crtline);
	Consman::current_screen_TTY = id;
	curset(Bcons[id].last_curposi);
	//
	for0(i, 4) Bcons[i].auto_incbegaddr = false;
	Bcons[id].auto_incbegaddr = true;
}

#endif
