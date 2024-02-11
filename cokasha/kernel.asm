; ASCII NASM TAB4 CRLF
; Attribute: CPU(x86)
; LastCheck: 20240208
; AllAuthor: @dosconio
; ModuTitle: Kernel: Loader of Environment, Library and Shell
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "pseudo.a"
%include "video.a"
%include "debug.a"
%include "demos.a"

Fent
[BITS 16]
; ---- ---- ---- ---- CODE ---- ---- ---- ----
DefineStack16 0x4000/16, 0
XOR AX, AX
MOV ES, AX
MOV AX, CS
MOV DS, AX

; Load IVT-Real
MOV WORD [ES:80H*4+0], Ro_Print
MOV WORD [ES:80H*4+2], CS

; Get and Check Memory Size (>= 4MB)

PUSH DS

; Load the main program
MOV SI, str_progload
MOV AH, 0
INT 80H
MOV AX, 0x0200
MOV ES, AX
MOV DS, AX
codefileLoad16 0,0,10

; Run the main program
CALL 0x0200:0x1

POP DS
MOV WORD [ES:80H*4+0], Ro_Print
MOV WORD [ES:80H*4+2], DS

; End
MOV SI, str_endenv
MOV AH, 0
INT 80H
DbgStop
; ---- ---- ---- ---- RO16 ---- ---- ---- ----
%include "cokasha/rout16.asm"
; ---- ---- ---- ---- DATA ---- ---- ---- ----
str_progload:
	DB "Loading the shell of real-16...", 10, 13, 0
str_endenv:
	DB "End of the soft environment.", 10, 13, 0

; ---- ---- ---- ---- KERNEL 32 ---- ---- ---- ----
ALIGN 32
[BITS 32]

Endf
