// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

extern "C" stduint sys_kill(stduint pid, int sig, stduint tid);


#if _GUI_ENABLE
void _Comment(R1) serv_shell_process() {
	uni::Dnode* tty_target = 0;
	::uni::Witch::Form* pf_ptr = nullptr;
	ProcessBlock* p = nullptr;

	if (Consman::ento_gui) {
		Rectangle rect{ Point(250, 160), Size2(480, 320) };
		auto [pcon, pf, tty_no] = Consman::CreateVconsole(rect, "Terminal");
		pf_ptr = pf;
		tty_target = vttys[tty_no];
		p = Taskman::CreateFile(("/md0/cot"), RING_U, Taskman::CurrentPID());
	}

	if (p) {
		// Register the process globally to get a valid PID
		Taskman::Append(p);
		// Lock the process state during complex environment setup
		IC.enInterrupt(false);
		p->state = ProcessBlock::State::Hanging;

		auto focus_tty = p->focus_tty.Lock();
		*focus_tty = tty_target;
		if (*focus_tty) {
			p->Open("/dev/tty", O_RDWR); // O_RDONLY stdin
			p->Open("/dev/tty", O_RDWR); // O_WRONLY stdout
			p->Open("/dev/tty", O_RDWR); // O_WRONLY stderr
		}
		if (auto nod = *focus_tty) {
			auto pblock = (vtty_type_t*)nod->type;
			pblock->master_pid = p->pid;
			pblock->proc_group.Append(p->pid);
		}
		
		// Environment is ready, restore state and start the application thread
		p->state = ProcessBlock::State::Active;
		Taskman::AppendThread(p->main_thread);
		IC.enInterrupt(true);
	}

	if (pf_ptr) {
		auto current_pb = Taskman::CurrentPB();
		auto pforms = current_pb->pforms.Lock();
		if (pforms->Count() == 0) {
			pforms->Append(pf_ptr);
		} else {
			(*pforms)[0] = pf_ptr;
		}
	}

	extern const char key_map[256], key_map_shift[256];
	while (true) {
		bool processed_any = false;
		if (pf_ptr) {
			while (pf_ptr->msg_queue.Count()) {
				processed_any = true;
				SheetMessage smsg;
				pf_ptr->msg_queue.Dequeue(smsg);
				if (smsg.event == SheetEvent::onKeybd) {
					auto* key_event = (keyboard_event_t*)smsg.args;
					if (key_event->method == keyboard_event_t::method_t::keydown) {
						auto ascii_ch = (key_event->mod.l_shift || key_event->mod.r_shift ? key_map_shift : key_map)[key_event->keycode];
						if (ascii_ch) {
							// Check for Ctrl+C to trigger SIGINT
							if ((key_event->mod.l_ctrl || key_event->mod.r_ctrl) && !key_event->mod.l_shift && !key_event->mod.r_shift && ascii_ch == 'c') {
								if (tty_target && tty_target->type) {
									auto pblock = (vtty_type_t*)tty_target->type;
									for (stduint idx = 0; idx < pblock->proc_group.Count(); idx++) {
										stduint target_pid = pblock->proc_group[idx];
										if (target_pid >= TaskCount && target_pid != pblock->master_pid) {
											sys_kill(target_pid, SIGINT, 0);
										}
									}
								}
								continue;
							}
							// Translate Ctrl + letters to control characters (e.g., Ctrl+D -> 0x04)
							if ((key_event->mod.l_ctrl || key_event->mod.r_ctrl) && !key_event->mod.l_shift && !key_event->mod.r_shift) {
								if (ascii_ch >= 'a' && ascii_ch <= 'z') {
									ascii_ch = ascii_ch - 'a' + 1;
								}
							}
							if (auto* q = VTTY_INNQ(tty_target)) {
								q->OutChar(ascii_ch);
								Consman::WakeBlockedWaiters();
							}
						}
					}
				}
				else if (smsg.event == SheetEvent::onClick) {
					bool left_down = (smsg.args[2] & 0x10);
					if (smsg.args[3] == 1 && !left_down) {
						// Force kill all processes in the group
						if (auto nod = tty_target) {
							auto pblock = (vtty_type_t*)nod->type;
							stduint self_pid = Taskman::CurrentPID();

							// Copy the group to a local buffer to ensure stable iteration
							stduint pid_list[32];
							stduint count = pblock->proc_group.Count();
							if (count > 32) count = 32;
							for (stduint i = 0; i < count; i++) pid_list[i] = pblock->proc_group[i];

							for (stdsint i = count - 1; i >= 0; --i) {
								stduint pid = pid_list[i];

								// Protect system tasks but allow killing the master shell
								if ((pid >= TaskCount || pid == pblock->master_pid) && pid != self_pid) {
									ProcessBlock* pb_to_kill = ProcessBlock::AcquireByPID(pid);
									if (pb_to_kill) {
										bool should_kill = (pb_to_kill->state != ProcessBlock::State::Hanging);
										ProcessBlock::Release(pb_to_kill);
										if (should_kill) {
											sys_kill(pid, SIGKILL, 0);
										}
									}
								}
							}
						}
						goto shell_exit;
					}
				}
			}
		}

		if (auto nod = tty_target) {
			auto pblock = (vtty_type_t*)nod->type;
			if (p && pblock->proc_group.Count() == 0) {
				goto shell_exit;
			}
		}
		else {
			goto shell_exit;
		}
		
		if (!processed_any) {
			syscall(syscall_t::REST, 1, 16); // Sleep for 16ms (approx 1 frame @ 60Hz) to save CPU
		} else {
			syscall(syscall_t::REST, 0, 0);  // Yield immediately if we had messages to process
		}
	}

shell_exit:
	if (Consman::ento_gui) {
		auto pblock = (vtty_type_t*)tty_target->type;
		stduint self_pid = Taskman::CurrentPID();
		for (stdsint i = pblock->proc_group.Count() - 1; i >= 0; --i) {
			stduint pid = pblock->proc_group[i];
			if (pid == self_pid) continue;
			auto pb_to_kill = ProcessBlock::AcquireByPID(pid);
			if (pb_to_kill) {
				bool should_kill = (pb_to_kill->state != ProcessBlock::State::Hanging);
				ProcessBlock::Release(pb_to_kill);
				if (should_kill) {
					sys_kill(pid, SIGKILL, 0);
				}
			}
		}
	}

	Taskman::ExitCurrent(0);
	// Final exit for the shell process itself
	// stduint exit_para[2] = { Taskman::CurrentPID(), 0 };
	// syssend(Task_TaskMan, exit_para, sizeof(exit_para), _IMM(TaskmanMsg::EXIT));
	// Never reach here: the stack will be reclaimed by Taskman
	while (true);
}
#endif
