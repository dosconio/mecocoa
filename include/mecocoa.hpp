#ifndef MECOCOA_HPP_
#define MECOCOA_HPP_

#if __has_include("autoconf.h")
#include "autoconf.h"

#else
#define CONFIG_SysTickFreq 100 // 100Hz
#define CONFIG_ECHO_LOGO 1 // print logo 🏳️‍⚧️
#define CONFIG_ENABLE_MMU 1

#endif

// _GUI_ENABLE
#ifdef CONFIG_ENABLE_GRAPHIC
#define _GUI_ENABLE CONFIG_ENABLE_GRAPHIC
#else
#define _GUI_ENABLE 0
#endif

// double buffer, or the anime is disabled
#define _GUI_DOUBLE_BUFFER 1

//
#define _GUI_FREETYPE 1
//
#define _SYS_MULTICORE 1

#ifdef  _MCU_STM32H7x
#define _MCCA_LITE // another addition for debug but _DEBUG
#undef  CONFIG_ENABLE_MMU
#define CONFIG_ENABLE_MMU 0
#undef  _GUI_DOUBLE_BUFFER
#define _GUI_DOUBLE_BUFFER 0 //{}
#undef  _GUI_FREETYPE
#define _GUI_FREETYPE 0 //{}
#undef  CONFIG_FILESYS_ISO9660
#define CONFIG_FILESYS_ISO9660 0
#undef  CONFIG_FILESYS_UDF
#define CONFIG_FILESYS_UDF 0
#endif
#define Systime SysTimer

#if (_MCCA & 0xFF00) == 0x1000
#undef _GUI_ENABLE
#define _GUI_ENABLE 0
#endif

// _MCCA MAGIC byte3: 0x10..0x19 Cortex-A, 0x1A Cortex-M, 0x1B Cortex-R, 0x00 means no MAGIC
#if defined(_MCCA) && ((_MCCA >> 24) == 0x1A)
	#define _ARCH_ARM_ProfileM
#elif defined(_MCCA) && ((_MCCA >> 24) >= 0x10) && ((_MCCA >> 24) <= 0x1B)
	#define _ARCH_ARM_ProfileA
#endif

#if 1
#define KASSERT(x) do { if (!(x)) plogerro("assert: %s", #x); } while (0)
#else
#define KASSERT(x) ((void)0)
#endif
#ifdef _UEFI
#define _UEFI_AUFDBG 1
#endif

//

#ifndef _STYLE_RUST
#define _STYLE_RUST
#endif
#include <c/stdinc.h>
#ifndef _DEV_KEIL
#define _HER_TIME_H
#define _TIME_H	1
#endif

#include <c/consio.h>
#include <c/datime.h>
#include <cpp/interrupt>
#include <cpp/vector>

#include <c/ISO_IEC_STD/signal.h>

use crate uni;
#if _MCCA == 0x8632
#include "../include/archits/atx-x86-flap32.hpp"
#include "../prehost/atx-x86-flap32/multiboot2.h"

#elif _MCCA == 0x8664
#include "../include/archits/atx-x64.hpp"

#elif (_MCCA & 0xFF00) == 0x1000// RV
#include <c/proctrl/RISCV/riscv.h>
#include "../include/archits/qemuvirt-riscv.hpp"

#elif (_MCCA & 0xFF00) == 0x2000// ARM
#include <c/proctrl/ARM.h>
#endif

#if !CONFIG_ENABLE_MMU
#define mglb(x) (x)
#endif
//

extern volatile stduint tick;

#include "memoman.hpp"
#include "syscall.hpp"
#include "taskman.hpp"
void serv_task_loop();
#include "console.hpp"
void serv_cons_loop();
void serv_shell_process();
void serv_graf_loop();
#include "filesys.hpp"
#include "fileman.hpp"
void serv_netw_loop();
void serv_dev_audio_loop();
void serv_file_loop();
void serv_devs_loop();
#include "devsman.hpp"
#include "virtual.hpp"


// ---- handler ----
extern InterruptControl IC;

struct RMOD_LIST {
	Handler_t init = nullptr;
	rostr name = nullptr;
	stduint keep[2] = {};
};// unload irq dep ...

void mecfetch();

// ---- . ----

#if defined(_ARCH_ARM_ProfileM)
void mpumap_task(ThreadBlock* tb);// MPU regions 1 and 2 follow the running task
#endif

extern "C" void register_interrupt_handler(stduint irq_id, Handler_t handler);

//
#endif // MECOCOA_HPP_

