; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; Kernel of Mecocoa, loaded in 0x1000
; - Provide Real Mode Interrupt Routines
; - CPL Support
; - Execute the potential AUTORUN Script

[CPU 386]

%include "pseudo.a"
%include "video.a"
%include "debug.a"
%include "demos.a"

File

; Set Stack, and Register the Interrupt Vector for 80H
XOR AX, AX
MOV ES, AX
MOV SS, AX
MOV SP, 0x1000
MOV WORD [ES:80H*4+0], Ro_Print
MOV WORD [ES:80H*4+2], DS

; Test the Interrupt 80H
MOV SI, str_ciallo
MOV AH, 0
INT 80H

; Load the main program
MOV SI, str_progload
MOV AH, 0
INT 80H
MOV AX, 0x0200
MOV ES, AX
MOV DS, AX
codefileLoad16 0,0,10
JMP 0x0200:0x1

DbgStop

; ---- ROUTINE 80H ----
;      ---- 00 Print String without attribute ----
CMP AH, 0
	JE Ro_Print
IRET

Ro_Print:
	PUSHAD
	MOV AX, 0xB800
	MOV ES, AX
	ConPrint SI, ~
	POPAD
IRET

; ---- DATA ----
str_ciallo:
	DB "Loading the kernel (Real Mode)...", 10, 13, 0
str_progload:
	DB "Loading the main program...", 10, 13, 0
Endf
