#include <c/task.h>
#include <c/datime.h>
#include <c/graphic/color.h>
#include <c/driver/keyboard.h>
#include <c/storage/harddisk.h>
#include <cpp/trait/BlockTrait.hpp>

#define statin static inline
#define _sign_entry() extern "C" void _start()
// #define _sign_entry() int main()
// #define _sign_entry() extern "C" void start()

use crate uni;

#define bda ((BIOS_DataArea*)0x400)

struct mec_gdt {
	descriptor_t null;
	descriptor_t data;
	descriptor_t code;
	gate_t rout;
	descriptor_t code16;
	descriptor_t tss;
	// descriptor_t code_r3;
	// descriptor_t data_r3;
};// on global linear area
struct mecocoa_global_t {
	volatile timeval_t system_time;// 0x18
	word gdt_len;
	mec_gdt* gdt_ptr;
	dword current_screen_mode;
	Color console_backcolor;
	Color console_fontcolor;
};
statin mecocoa_global_t* mecocoa_global{ (mecocoa_global_t*)0x500 };

enum {
	SegNull = 8 * 0,
	SegData = 8 * 1,// R3 is OK
	SegCode = 8 * 2,
	SegCall = 8 * 3,
	SegCo16 = 8 * 4,
	SegTSS = 8 * 5,
	// SegCodeR3 = 8 * 6,
	// SegDataR3 = 8 * 7,
};

enum class syscall_t : stduint {
	OUTC = 0x00, // putchar
	INNC = 0x01, //{TODO} getchar (block_mode)
	EXIT = 0x02, // exit (code)
	TIME = 0x03, // getsecond
	REST = 0x04, // halt
	COMM = 0x05, // communicate: send or receive
	OPEN = 0x06, // open file

	TEST = 0xFF,
};

#define mapglb(x) (*(usize*)&(x) |= 0x80000000)
#define mglb(x) (_IMM(x) | 0x80000000)

extern bool opt_info;
extern bool opt_test;



// ---- handler
extern "C" void Handint_PIT_Entry();
extern "C" void Handint_PIT();
extern "C" void Handint_RTC_Entry();
extern "C" void Handint_RTC();
extern "C" void Handint_KBD_Entry();
extern "C" void Handint_KBD();
extern "C" void Handint_HDD_Entry();
extern "C" void Handint_HDD();
extern OstreamTrait* kbd_out;

// ---- memoman
#include "memoman.hpp"

// ---- syscall
extern "C" void call_gate();
extern "C" void call_intr();
extern "C" void* call_gate_entry();
stduint syscall(syscall_t callid, ...);

#define COMM_RECV 0b10
#define COMM_SEND 0b01

// ---- taskman
#include "taskman.hpp"

// ---- [service] console

extern BareConsole* BCONS0;

struct MccaTTYCon : public BareConsole {
	static byte current_screen_TTY;
	static void cons_init();
	static void serv_cons_loop();
	static void current_switch(byte id);
	//
	bool last_e0 = false;
	//
	MccaTTYCon(stduint columns, stduint lines_total, stduint topln) : BareConsole(columns, lines_total, 0xB8000, topln) {}
	//[TEMP] no output buffer, user library can make it in their level.
	char inn_buf[64]; char* _Comment(W) p = inn_buf; char* _Comment(R) q = 0;
	inline bool is_full() const { return q && p == q; }
	inline bool is_empty() const { return q == nullptr; }
	int get_inn() {
		if (is_empty()) return -1;
		int ret = (unsigned)*q++;
		if (q >= inn_buf + byteof(inn_buf)) q = inn_buf;
		if (p == q) q = nullptr, p = inn_buf;
		return ret;
	}
	bool put_inn(char c) {
		if (is_full()) return false;
		if (q == nullptr) q = p;
		*p++ = c;
		if (p >= inn_buf + byteof(inn_buf)) p = inn_buf;
		return true;
	}
	//
	bool last_E0 = false;
};
extern MccaTTYCon* ttycons[4];

// ---- [service] fileman
#include "fileman.hpp"

