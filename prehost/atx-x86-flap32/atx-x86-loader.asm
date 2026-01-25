; ASCII AASM TAB4 LF
; Attribute: 
; LastCheck: 20240218
; AllAuthor: @ArinaMgk
; ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

[BITS 32]

EXTERN GDT64
EXTERN entry_kernel

SegData EQU 1
SegCo64 EQU 5
SegCount EQU 6

[section .text]

GLOBAL B32_LoadMod64
B32_LoadMod64:
	POP EAX; no return
	MOV EAX, [entry_kernel]
	MOV [KernelJump], EAX
	MOV DWORD[GdtPtr64+2], GDT64
	DB  0x66
	LGDT [GdtPtr64]
	MOV	AX,	8*SegData
	MOV	DS,	AX
	MOV	ES,	AX
	MOV	FS,	AX
	MOV	GS,	AX
	MOV	SS,	AX
	; Open PAE
	MOV EAX, CR4
	BTS EAX, 5; CR4.PAE
	MOV CR4, EAX
	; Paging
	MOV EAX, 0x200000
	MOV CR3, EAX
	; Set IA32 EFER.LME
	MOV ECX, 0xC0000080
	RDMSR
	BTS EAX, 8
	WRMSR
	; Enter Compatibility Mode until long-jmp
	MOV EAX, CR0
	BTS EAX, 0
	BTS EAX, 31
	MOV CR0, EAX
	;
	MOV EAX, 'FINA'
	JMP FAR [KernelJump]

GLOBAL B32_LoadKer32
B32_LoadKer32:
	POP EAX; no return
	MOV EDX, [entry_kernel]
	MOV EAX, 'FINA'
	JMP EDX

[section .data]

GdtPtr64:
	DW  8*SegCount-1
	DQ  0
KernelJump:
	DD  0
	DW  8*SegCo64
