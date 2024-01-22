; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; Shell in 32-bit protected mode, with routines [Ring 3]

[BITS 32]
%include "debug.a"
%include "demos.a"
%include "osdev.a"
%include "routidx.a"
Dem2Prog entry,CODE,CodeEnd,256,DATA,DataEnd,RONL,RonlEnd
SegGate EQU 8*6+3
APIEndo:
[SECTION CODE ALIGN=16 VSTART=0]
rs EQU section.RONL.start
entry:
	PUSH EDX; ADDR
	MOV ESI, rs
	ADD ESI, EDX
	MOV EDI, RotPrint
	CALL SegGate:0
	XOR EDI, EDI
	CALL SegGate:0
	POP EDX

    JMP entry; for next calling
CodeEnd:
[SECTION DATA ALIGN=16 VSTART=0]
dd 0
DataEnd:
[SECTION RONL ALIGN=16 VSTART=0]
str0: db "[Shell] ",0
RonlEnd:
Enddf
