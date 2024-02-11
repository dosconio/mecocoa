; ASCII NASM TAB4 CRLF
; Attribute: CPU(x86)
; LastCheck: 20240208
; AllAuthor: @dosconio
; ModuTitle: 16-bit Routine for Kernel
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

; ---- ROUTINE 80H ----
CMP AH, 0
	JE Ro_Print
IRET
;      ---- 00 Print String without attribute ----

Ro_Print:
	PUSHAD
	MOV AX, 0xB800
	MOV ES, AX
	ConPrint SI, ~
	POPAD
IRET
