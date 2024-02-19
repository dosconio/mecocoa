// ASCII C++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - Programming by ELF32-C++ directly
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

extern "C" void start() {
	char * const vdobf = (char*)0xB8000;
	for (int i = 0; i < 12; i++)
	{
		vdobf[i * 2] = "Hello World!"[i];
	}
	while (1);
}
