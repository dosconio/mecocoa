#include <c/task.h>
#include <c/datime.h>
#include <c/graphic/color.h>
#include <c/driver/keyboard.h>
#include <c/storage/harddisk.h>
#include <cpp/trait/BlockTrait.hpp>

#define statin static inline
#define _sign_entry() extern "C" void _start()

// ---- sysinfo

void kernel_fail(void* serious, ...);
rostr text_brand();

// ---- handler
extern "C" void Handint_COM1_Entry();
extern "C" void Handint_COM1();
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

// ----

#define mapglb(x) (*(usize*)&(x) |= 0x80000000)
#define mglb(x) (_IMM(x) | 0x80000000)
