; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240505
; AllAuthor: @dosconio
; ModuTitle: User-lib of Mecocoa
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

[CPU 586]
;%include "mecocoa/kernel.inc"

GLOBAL syscall

section .text

syscall:
	CALL 8*3|3:0
RET

