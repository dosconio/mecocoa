
#define statin static inline
#define _sign_entry() extern "C" void _start()
// #define _sign_entry() int main()
// #define _sign_entry() extern "C" void start()

use crate uni;

struct mec_gdt {
	_CPU_x86_descriptor null;
	_CPU_x86_descriptor data;
	_CPU_x86_descriptor code;
	_CPU_x86_descriptor rout;
};// on global linear area
struct mecocoa_global_t {
	dword syscall_id;
	stduint syspara_0;
	stduint syspara_1;
	stduint syspara_2;
	stduint syspara_3;
	stduint syspara_4;
	word ip_flap32;
	word cs_flap32;
	word gdt_len;
	mec_gdt* gdt_ptr;
	word ivt_len;
	dword ivt_ptr;
	// 0x10
	qword zero;
	dword current_screen_mode;
	Color console_backcolor;
	Color console_fontcolor;
	timeval_t system_time;
}*mecocoa_global;

