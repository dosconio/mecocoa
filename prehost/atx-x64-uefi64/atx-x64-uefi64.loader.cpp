typedef unsigned short CHAR16;
typedef unsigned long long EFI_STATUS;
typedef void *EFI_HANDLE;

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (*EFI_TEXT_STRING)(
  struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  CHAR16                                   *String);

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
	void* dummy;
	EFI_TEXT_STRING  OutputString;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
	char                             dummy[52];
	EFI_HANDLE                       ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
} EFI_SYSTEM_TABLE;

extern "C"
EFI_STATUS EfiMain(EFI_HANDLE        ImageHandle,
	EFI_SYSTEM_TABLE* SystemTable) {
	SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16*)L"Ciallo, Mecocoa~\n");
	while (1);
	return 0;
}

/* AASM
bits 64
section .text

global EfiMain
EfiMain:
	sub  rsp, 0x28
	mov  rcx, [rdx + 64]
	lea  rdx, [rel .msg]
	call [rcx + 8]

.fin: jmp .fin

section .rdata
align 8
.msg:
	dw __utf16__("Hello, world!"), 0

*/
