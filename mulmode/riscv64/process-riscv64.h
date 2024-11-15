// ASCII C/C++ TAB4 CRLF
// LastCheck: 20241017
// AllAuthor: @dosconio
// ModuTitle: Process
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef PROC_H
#define PROC_H

#include "arc-r64.h"

#define NPROC (16)

// Saved registers for kernel context switches.
struct context {
	uint64 ra;
	uint64 sp;
	// : callee-saved
	uint64 s0;
	uint64 s1;
	uint64 s2;
	uint64 s3;
	uint64 s4;
	uint64 s5;
	uint64 s6;
	uint64 s7;
	uint64 s8;
	uint64 s9;
	uint64 s10;
	uint64 s11;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
	enum procstate state; // Process state
	int pid; // Process ID
	uint64 ustack; // Virtual address of user stack
	uint64 kstack; // Virtual address of kernel stack
	struct trapframe* trapframe; // data page for trampoline.S
	struct context context; // swtch() here to run process
	//{TODO} uC LAB1: 
};

//{TODO} uC LAB1

struct proc* curr_proc();
void exit(int);
void proc_init();
void scheduler() __attribute__((noreturn));
void sched();
void yield();
struct proc* allocproc();
// swtch.S
void swtch(struct context*, struct context*);

#endif // PROC_H

