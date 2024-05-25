// ASCII C++ TAB4 LF
// Attribute: 
// LastCheck: 20240524
// AllAuthor: @dosconio
// ModuTitle: Kit for Mecocoa subapps
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../inc/cocoapp.h"

extern int main(int, char **);

int entry(int argc, char **argv)
{
	exit(main(argc, argv));
	return 0;
}

