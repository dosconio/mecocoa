
; "libdbg" means a demonstration library but a library for debugging.

GLOBAL Dbg_Func

section .text
Dbg_Func:
	MOV SI, str_test_message
	MOV AH, 0
	INT 80H
    RET

section .data
str_test_message:
	DB "TEST MEG!", 10, 13, 0

