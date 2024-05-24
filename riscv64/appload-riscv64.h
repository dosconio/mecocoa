// ASCII C/C++ TAB4 LF
// LastCheck: 20240523
// AllAuthor: @dosconio
// ModuTitle: APP Loader
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef _MCCA_AppLoader
#define _MCCA_AppLoader

#include <c/proctrl/RISCV/riscv64.h>// style(gene-3)
#include "../mecocoa/kernel.h"

void loader_init();
int run_next_app();

#define BASE_ADDRESS (0x80400000)
#define MAX_APP_SIZE (0x20000)
#define USER_STACK_SIZE PAGE_SIZE
#define TRAP_PAGE_SIZE PAGE_SIZE

#endif
