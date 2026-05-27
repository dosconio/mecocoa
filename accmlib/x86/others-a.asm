; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240505
; AllAuthor: @dosconio
; ModuTitle: User-lib of Mecocoa
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

[CPU 586]
;%include "mecocoa/kernel.inc"

GLOBAL _start, syscall, __sigrestorer
EXTERN main, _exit
EXTERN _preprocess


section .text

SegCall EQU 8*7

syscall:
	;PUSHAD
	PUSH EBX
	PUSH ECX
	PUSH EDX
	PUSH ESI
	PUSH EDI
	PUSH EBP
	MOV EAX, [ESP + 4*(1+6+0)]
	MOV ECX, [ESP + 4*(1+6+1)]
	MOV EDX, [ESP + 4*(1+6+2)]
	MOV EBX, [ESP + 4*(1+6+3)]
	CALL SegCall|3:0
	;POPAD;{TODO} PROC RETURN VALUE
	POP EBP
	POP EDI
	POP ESI
	POP EDX
	POP ECX
	POP EBX

RET

_start:
	xor	ebp, ebp		; Mark the end of stack frames
	pop	eax				; Get argc from stack, ESP now points to argv
	mov	ebx, esp		; Get argv pointer
	lea	ecx, [ebx + eax*4 + 4] ; Get envp pointer (argv + argc*4 + 4 for NULL)
	and	esp, -16		; Ensure 16-byte alignment
	push	ecx			; Third argument: envp
	push	ebx			; Second argument: argv
	push	eax			; First argument: argc

	call _preprocess

	call	main		; Call main(argc, argv, envp)
	add	esp, 16			; Clean up arguments and padding
	push	eax			; Push return value for exit
	call	_exit		; Call exit(status)
mov byte[0], 0

__sigrestorer:
	MOV EAX, 0x15 ; syscall_t::SIGR (0x15)
	CALL SegCall|3:0

