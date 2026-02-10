#include <c/task.h>
#include <c/datime.h>
#include <c/graphic/color.h>
#include <c/driver/keyboard.h>
#include <c/storage/harddisk.h>
#include <cpp/trait/BlockTrait.hpp>

#define statin static inline
#define _sign_entry() extern "C" void _start()

use crate uni;

// ---- sysinfo

int* kernel_fail(loglevel_t serious);
rostr text_brand();

// ---- handler
extern "C" void Handint_PIT_Entry();
extern "C" void Handint_PIT();
extern "C" void Handint_RTC_Entry();
extern "C" void Handint_RTC();
extern "C" void Handint_KBD_Entry();
extern "C" void Handint_KBD();
extern "C" void Handint_MOU_Entry();
extern "C" void Handint_MOU();
extern "C" void Handint_HDD_Entry();
extern "C" void Handint_HDD();

// ---- memoman
#include "memoman.hpp"
#define PAGE_SIZE 0x1000 // lev-0-page

// ---- syscall
#include "syscall.hpp"

// ---- taskman
#include "taskman.hpp"
void serv_task_loop();

// ---- [service] console
#include "console.hpp"
void serv_cons_loop();

// ---- [service] fileman
#include "fileman.hpp"
void serv_dev_hd_loop();
void serv_file_loop();

// ----

#define mapglb(x) (*(usize*)&(x) |= 0x80000000)
#define mglb(x) (_IMM(x) | 0x80000000)
