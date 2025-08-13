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

	TEST = 0xFF,
};

#define mapglb(x) (*(usize*)&(x) |= 0x80000000)
#define mglb(x) (_IMM(x) | 0x80000000)

//{TODEL}
struct __attribute__((packed)) tmp48le_t { uint16 u_16fore; uint32 u_32back; };
struct __attribute__((packed)) tmp48be_t { uint32 u_32fore; uint16 u_16back; };
extern tmp48le_t tmp48_le;
extern tmp48be_t tmp48_be;

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

bool waitfor(int mask, int val, int timeout_second);

#define MAX_DRIVES 2 // only for primary IDE
#define NR_PART_PER_DRIVE    4 // 4 primary partitions per drive
#define NR_SUB_PER_PART     16 // 16 logical partitions per primary partition

#define NR_SUB_PER_DRIVE    (NR_SUB_PER_PART * NR_PART_PER_DRIVE)// 64
#define NR_PRIM_PER_DRIVE    (NR_PART_PER_DRIVE + 1) // 5

#define MAX_PRIM        (MAX_DRIVES * NR_PRIM_PER_DRIVE - 1) // 9. prim_dev ranges in hd[0-9] (h[0] h[1~4], h[5], h[6~9])
#define MAX_SUBPARTITIONS    (NR_SUB_PER_DRIVE * MAX_DRIVES)


void DEV_Init();
void serv_dev_hd_loop();
void serv_file_loop();

enum MajorDevice {
	DEV_NULL = 0,
	DEV_FLOPPY,
	DEV_CDROM,
	DEV_HDD,
	DEV_TTY,
	DEV_SCSI,
	//
	DEV_MAX
};
#define DEV_MAJOR_SHIFT     8
#define DEV_MAKE_DEV(a,b)   ((a << DEV_MAJOR_SHIFT) | b)
#define DEV_MAJOR(x)        ((x >> DEV_MAJOR_SHIFT) & 0xFF)
#define DEV_MINOR(x)        (x & 0xFF)
#define    MINOR_hd1a       0x10// should greater than MAX_PRIM
#define    MINOR_hd2a       (MINOR_hd1a+NR_SUB_PER_PART)

struct Harddisk_PATA_Paged : public Harddisk_PATA {
	Harddisk_PATA_Paged(byte id = 0, HarddiskType type = HarddiskType::ATA) : Harddisk_PATA(id, type) {}
	virtual bool Read(stduint BlockIden, void* Dest);
	virtual bool Write(stduint BlockIden, const void* Sors);
};
