#ifndef _STYLE_RUST
#define _STYLE_RUST
#endif
#if _MCCA == 0x8632
#define _HER_TIME_H
#endif

#include <c/consio.h>
#include <c/datime.h>
#include <cpp/interrupt>
#include <cpp/vector>

use crate uni;
#if _MCCA == 0x8632
#include "../include/atx-x86-flap32.hpp"
#include "../prehost/atx-x86-flap32/multiboot2.h"

#elif _MCCA == 0x8664
#include "../include/atx-x64.hpp"

#elif (_MCCA & 0xFF00) == 0x1000// RV
#include <c/proctrl/RISCV/riscv.h>
#include "../include/qemuvirt-riscv.hpp"

#elif (_MCCA & 0xFF00) == 0x2000// ARM
#include <c/proctrl/ARM.h>
#endif
//

#include "memoman.hpp"
#include "syscall.hpp"
#include "taskman.hpp"
void serv_task_loop();
#include "console.hpp"
void serv_cons_loop();
void serv_conv_loop();

#if _MCCA == 0x8632

// ---- [service] fileman
#include "fileman.hpp"
void serv_dev_hd_loop();
void serv_file_loop();
#endif

// ---- handler ----
#if (_MCCA & 0xFF00) == 0x1000 || (_MCCA & 0xFF00) == 0x8600
extern InterruptControl IC;
#endif

struct RMOD_LIST {
	Handler_t init;
	rostr name;
	stduint keep[2];
};// unload irq dep ...

// ---- . ----

//
#define _GUI_ENABLE 1
// double buffer, or the anime is disabled
#define _GUI_DOUBLE_BUFFER 0
// print logo 🏳️‍⚧️
#define _GUI_LOGO 0

