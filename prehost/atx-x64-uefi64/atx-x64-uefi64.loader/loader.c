// ASCII clang TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: UEFI Loader
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#undef ABS
#undef MIN
#undef MAX
#include <c/stdinc.h>

#include <Uefi.h>
#include <Library/UefiLib.h>

unsigned f();

// extern "C"
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* SystemTable) {
	Print((CHAR16*)u"Ciallo, Mecocoa~ %u\n", f());// SystemTable->ConOut->OutputString
	
	while (1) {

	}
	return 0;
}

/*
[EDK2/Conf/tools_def.txt]
2904| DEBUG_CLANGDWARF_X64_CC_FLAGS         = DEF(CLANG38_ALL_CC_FLAGS) -m64 "-DEFIAPI=__attribute__((ms_abi))" -mno-red-zone -mcmodel=small -fpie -Oz -flto DEF(CLANG38_X64_TARGET) -g  -I$(uincpath)
*/
