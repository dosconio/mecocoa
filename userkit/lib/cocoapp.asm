; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240505
; AllAuthor: @dosconio
; ModuTitle: User-lib of Mecocoa
; Copyright: Dosconio Mecocoa, BSD 3-Clause License
[CPU 586]
%include "mecocoa/kernel.inc"

GLOBAL _sysinit
GLOBAL _sysouts
GLOBAL _sysdelay
GLOBAL _sysquit

section .text

_sysinit:
	MOV EDI, RotInitialize
	CALL SegGate|3:0
RET

_sysouts:
	PUSH EBP
	MOV EBP, ESP
	PUSH ESI
	PUSH EDI
	MOV ESI, [EBP+4*2]
	MOV EDI, RotPrint
	CALL SegGate|3:0
	POP EDI
	POP ESI
	MOV ESP, EBP
	POP EBP
RET

_sysdelay:
	PUSH EBP
	MOV EBP, ESP
	MOV EDX, [EBP+4*2]
	MOV EDI, RotSysDelay
	CALL SegGate|3:0
	MOV ESP, EBP
	POP EBP	
RET

_sysquit:
	MOV EDI, RotTerminal
	CALL SegGate|3:0
RET

