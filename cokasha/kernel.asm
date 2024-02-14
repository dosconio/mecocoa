; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240208
; AllAuthor: @dosconio
; ModuTitle: Kernel: Loader of Environment, Library and Shell
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "pseudo.a"
%include "video.a"
%include "debug.a"
%include "demos.a"

GLOBAL HerMain

EXTERN ReadFileFFAT12
EXTERN LoadFileMELF

ADDR_STACKTOP EQU 0x4000

; {Option Switch: 1 or others}
IVT_TIMER_ENABLE EQU 1
MEM_PAGED_ENABLE EQU 1

[CPU 386]
DB "DOSCONIO"
[BITS 16]
; ---- ---- ---- ---- CODE ---- ---- ---- ----
section .text
HerMain:
; Initialize 16-Bit Real Mode
	DefineStack16 0, ADDR_STACKTOP
	XOR AX, AX
	MOV ES, AX
	MOV AX, CS
	MOV DS, AX
; Load IVT-Real
	MOV WORD [ES:80H*4+0], Ro_Print
	MOV WORD [ES:80H*4+2], CS
; Get and Check Memory Size (>= 4MB)
	;
; Load and Run Shell
	PUSH DS
	MOV SI, str_progload
	MOV AH, 0
	INT 80H
	MOV AX, 0x0200
	MOV ES, AX
	MOV DS, AX
	;;codefileLoad16 0,0,10
	;;CALL 0x0200:0x1
	POP DS



; Main
	MOV AX, 0
	MOV ES, AX
	PUSH WORD 0x9000; Buffer
	PUSH WORD str_shell16;{TODO} if not found
	PUSH WORD 0x9400
	CALL ReadFileFFAT12
	CALL LoadFileMELF; make use of the stack
	ADD SP, 6
	CALL AX
; Endo
	MOV SI, str_endenv
	MOV AH, 0
	INT 80H
	DbgStop
; ---- ---- ---- ---- RO16 ---- ---- ---- ----
%include "cokasha/rout16.asm"
; ---- ---- ---- ---- DATA ---- ---- ---- ----
section .data
str_progload:
	DB "Loading the shell of real-16"
	%ifdef _FLOPPY
		DB "(F)"
	%endif
	DB "...", 10, 13, 0
str_endenv:
	DB "End of the soft environment.", 10, 13, 0
str_test_filename:
	DB "TEST    TXT"
str_shell16:
	DB "SHL16   APP"

; ---- ---- ---- ---- KERNEL 32 ---- ---- ---- ----
ALIGN 32
[BITS 32]

; Endf
