
; "libdbg" means a demonstration library but a library for debugging.

[CPU 386]
[BITS 16]

GLOBAL Dbg_Func
GLOBAL ReturnModeProt32

section .text
Dbg_Func:
	MOV SI, str_test_message
	MOV AH, 0
	INT 80H
    RET

ReturnModeProt32:
	;{TODO}
	JMP FAR [0x0500]

section .data
str_test_message:
	DB "TEST MEG!", 10, 13, 0

