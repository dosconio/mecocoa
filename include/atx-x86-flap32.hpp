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
	EXIT = 0x02, // exit                       // (code)
	TIME = 0x03, // getsecond
	REST = 0x04, // halt
	COMM = 0x05, // communicate: send/receive
	OPEN = 0x06, // open   file
	CLOS = 0x07, // close  file                // (fd)->0
	READ = 0x08, // read   file                // (fd, addr, len)
	WRIT = 0x09, // write  file                // (fd, addr, len)
	DELF = 0x0A, // remove file

	TMSG = 0x0F, // try message

	TEST = 0xFF,
};

#define mapglb(x) (*(usize*)&(x) |= 0x80000000)
#define mglb(x) (_IMM(x) | 0x80000000)

extern bool opt_info;
extern bool opt_test;

extern bool ento_gui;

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

// ---- taskman
#include "taskman.hpp"

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

// ---- [service] console
#include "console.hpp"
void serv_cons_loop();

// ---- [service] fileman
#include "fileman.hpp"
void serv_dev_hd_loop();
void serv_file_loop();

