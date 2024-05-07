; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240505
; AllAuthor: @dosconio
; ModuTitle: User-lib of Mecocoa
; Copyright: Dosconio Mecocoa, BSD 3-Clause License
[CPU 586]
%include "mecocoa/kernel.inc"

GLOBAL _sysouts
GLOBAL _sysquit

section .text

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

_sysquit:
	MOV EDI, RotTerminal
	CALL SegGate|3:0
RET

