; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240501
; AllAuthor: @dosconio
; ModuTitle: Memory Management
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

[CPU 386]
%include "mecocoa/kernel.inc"

GLOBAL MemAllocSuperv
GLOBAL _MemAllocKernel


[BITS 16]; Real
MemAllocSuperv:; DS:Pres(0) CX(Size)->AX(PzikAddr@~0xFFFF)
	PUSH EBX
	OR CX, CX; if allocating 0 size.
	JZ R_MemAllocS_End
	MOVZX EAX, WORD[MemSptr]
	MOV EBX, EAX
	ADD BX, CX
	CMP BX, 0x8000
	JA R_MemAllocS_Next
	XOR EAX, EAX; fail
	JMP R_MemAllocS_End
	; register in Bitmap
	R_MemAllocS_Next: 
	MOV WORD[MemSptr], BX
	DEC EBX
	AND EBX, 0xFFFFF000
	R_MemAllocS_Loop:
		;CMP EBX, EAX
		;JB R_MemAllocS_End; E.G. 0x1000 < 0x1001
		;{TODO} CALL RR_PageRegister
		;CMP EBX, 0
		;JZ R_MemAllocS_End
		;SUB EBX, 0x1000
		;JMP R_MemAllocS_Loop
	R_MemAllocS_End:
	POP EBX
RET
