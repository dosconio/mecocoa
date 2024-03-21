// LastCheck: 20240311
// AllAuthor: @dosconio
// ModuTitle: Shell Flat Prot-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#define wait() HALT()

extern "C" {
	#include <../c/x86/interface.x86.h>
	#include <../c/x86/x86.h>
	#include "../include/conio32.h"
}

static void init(bool cls);
