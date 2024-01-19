; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 32-bit protected mode

[BITS 32]
%include "debug.a"
%include "demos.a"
%include "osdev.a"
Dem2Prog entry,CODE,CodeEnd,256,DATA,DataEnd,RONL,RonlEnd
CallGate EQU 8*4+3
APIEndo:
[SECTION CODE ALIGN=16 VSTART=0]
rs EQU section.RONL.start
entry:
	PUSH EDX; ADDR
	MOV ESI,rs
	ADD ESI,EDX
	MOV EDI,1
	CALL CallGate:0
	XOR EDI,EDI
	CALL CallGate:0
	POP EDX

    JMP entry; for next calling
CodeEnd:
[SECTION DATA ALIGN=16 VSTART=0]
dd 0
DataEnd:
[SECTION RONL ALIGN=16 VSTART=0]
str0: db "[Hello] ",0
RonlEnd:
Enddf

