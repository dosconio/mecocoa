#ifndef _STYLE_RUST
#define _STYLE_RUST
#endif

#include <c/consio.h>
#include <c/datime.h>

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
