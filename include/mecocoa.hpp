#ifndef _STYLE_RUST
#define _STYLE_RUST
#endif
#if _MCCA == 0x8632
#define _HER_TIME_H
#endif

#include <c/consio.h>
#include <c/datime.h>
#include <cpp/interrupt>

use crate uni;
#if _MCCA == 0x8632
#include "../include/atx-x86-flap32.hpp"
#include "../prehost/atx-x86-flap32/multiboot2.h"
#elif _MCCA == 0x8664
#include "../include/atx-x64.hpp"
#elif (_MCCA & 0xFF00) == 0x1000// RV
#include <c/proctrl/RISCV/riscv.h>
#include "../include/qemuvirt-riscv.hpp"
#endif

#include "memoman.hpp"
#include "syscall.hpp"
#include "taskman.hpp"
void serv_task_loop();

#if _MCCA == 0x8632
// ---- [service] console
#include "console.hpp"
void serv_cons_loop();
void serv_conv_loop();

// ---- [service] fileman
#include "fileman.hpp"
void serv_dev_hd_loop();
void serv_file_loop();
#endif

// ---- handler ----
extern InterruptControl IC;

// ---- . ----

#define _GUI_DOUBLE_BUFFER 1// or the anime is disabled


