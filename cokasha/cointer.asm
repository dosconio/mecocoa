; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(ELF)
; LastCheck: 20240219
; AllAuthor: @dosconio
; ModuTitle: Kernel: Loader of Environment, Library and Shell
; Copyright: Dosconio Mecocoa, BSD 3-Clause License
; NegaiMap : { USYM --> Mecocoa --> USYM --> Any OS }

GLOBAL _MccaLoadGDT
GLOBAL _MccaAlocGDT

[BITS 32]
[CPU 386]

_MccaLoadGDT:
	; load GDT and return the numbers of GDT
	MOVZX EAX, WORD[0x80000500+0x4]
	INC EAX
	SHR EAX, 3 ; 
	LGDT [0x80000500+0x4]
RET

_MccaAlocGDT:
	; return the numbers of GDT
	ADD WORD[0x80000500+0x4], 8
JMP _MccaLoadGDT


	



