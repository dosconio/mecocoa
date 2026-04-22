#ifndef MECOCOA_HPP_
#define MECOCOA_HPP_

#ifndef _STYLE_RUST
#define _STYLE_RUST
#endif
#define _HER_TIME_H
#define _TIME_H	1

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

#define SysTickFreq 100 // 100Hz

extern volatile stduint tick;

#include "memoman.hpp"
#include "syscall.hpp"
#include "taskman.hpp"
void serv_task_loop();
#include "console.hpp"
void serv_cons_loop();
void serv_graf_loop();
#include "filesys.hpp"
#include "fileman.hpp"
void serv_dev_mem_loop();
void serv_dev_hd_loop();
void serv_file_loop();


// ---- handler ----
#if (_MCCA & 0xFF00) == 0x1000 || (_MCCA & 0xFF00) == 0x8600
extern InterruptControl IC;
#endif

struct RMOD_LIST {
	Handler_t init;
	rostr name;
	stduint keep[2];
};// unload irq dep ...

void mecfetch();

// ---- . ----

//
#define _GUI_ENABLE 1
// double buffer, or the anime is disabled
#define _GUI_DOUBLE_BUFFER 1
// print logo 🏳️‍⚧️
#define _GUI_LOGO 1

#if (_MCCA & 0xFF00) == 0x1000
#undef _GUI_ENABLE
#define _GUI_ENABLE 0
#endif

#endif // MECOCOA_HPP_
