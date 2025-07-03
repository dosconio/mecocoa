#include <c/datime.h>
#include <c/graphic/color.h>

#define statin static inline
#define _sign_entry() extern "C" void _start()
// #define _sign_entry() int main()
// #define _sign_entry() extern "C" void start()

use crate uni;

struct mec_gdt {
	descriptor_t null;
	descriptor_t data;
	descriptor_t code;
	gate_t rout;
	descriptor_t code16;
	descriptor_t tss;
	descriptor_t code_r3;
	descriptor_t data_r3;
};// on global linear area
struct mecocoa_global_t {
	dword syscall_id; // 0x00
	stduint syspara_0;// 0x04
	stduint syspara_1;// 0x08
	stduint syspara_2;// 0x0c
	stduint syspara_3;// 0x10
	stduint syspara_4;// 0x14 -> Extern Parameters { other_cnt; other_paras... }
	volatile timeval_t system_time;// 0x18
	word gdt_len;
	mec_gdt* gdt_ptr;
	word ivt_len;
	dword ivt_ptr;
	// 0x10
	qword zero;
	dword current_screen_mode;
	Color console_backcolor;
	Color console_fontcolor;
};
statin mecocoa_global_t* mecocoa_global{ (mecocoa_global_t*)0x500 };

enum {
	SegNull = 8 * 0,
	SegData = 8 * 1,
	SegCode = 8 * 2,
	SegCall = 8 * 3,
	SegCo16 = 8 * 4,
	SegTSS = 8 * 5,
	SegCodeR3 = 8 * 6,
	SegDataR3 = 8 * 7,
};

enum class syscall_t : stduint {
	OUTC = 0x00, // putchar
	INNC = 0x01, //{TODO} getchar (fmt, ...)
	EXIT = 0x02, // exit (code)
	TIME = 0x03, //{TODO} gettick (&us, optional &s)
	TEST = 0xFF,
};

#define mapglb(x) (*(usize*)&(x) |= 0x80000000)
#define mglb(x) (_IMM(x) | 0x80000000)

struct __attribute__((packed)) tmp48le_t { uint16 u_16fore; uint32 u_32back; };
struct __attribute__((packed)) tmp48be_t { uint32 u_32fore; uint16 u_16back; };

extern tmp48le_t tmp48_le;
extern tmp48be_t tmp48_be;

extern bool opt_info;
extern bool opt_test;

// handler
extern "C" void Handint_PIT();
extern "C" void Handint_RTC();
extern "C" void Handint_KBD();

// memoman
#include "memoman.hpp"

// [x86] segmman
void GDT_Init();
word GDT_GetNumber();
word GDT_Alloc();

// syscall
extern "C" void call_gate();
extern "C" void call_intr();
extern "C" void* call_gate_entry();
void syscall(syscall_t callid, stduint paracnt = 0, ...);

// taskman
word TaskRegister(void* entry);
