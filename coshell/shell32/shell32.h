// LastCheck: 20240311
// AllAuthor: @dosconio
// ModuTitle: Shell Flat Prot-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#define wait() HALT()

#include "cpp/cinc"
#include <c/format/ELF.h>
#include <c/task.h>
#include <c/stdinc.h>
#include "../../include/console.h"
#include "cpp/cinc"

static void init(bool cls);

extern "C" {
	stduint MccaAlocGDT(void);
	stduint MccaLoadGDT(void);
}
