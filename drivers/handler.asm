; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240220
; AllAuthor: @dosconio
; ModuTitle: Handlers for interruptions from hard or soft sources
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "debug.a"

GLOBAL _Handexc_General
GLOBAL _Handint_General

GLOBAL _Handint_RTC

%include "cokasha/kernel.inc"

[CPU 386]

[BITS 32]

section .text

_Handint_RTC:
	PUSHAD
;	PUSH DS
;	MOV AX, SegData
;	MOV DS, EAX
;	;{ISSUE} Why these codes make interrupt only once? --@dosconio 2024/Jan/17
;		; PUSH WORD SegData
;		; POP DS
;	MOV ESI, THISF_ADR+msg_on_1s
;	MOV EDI, 1
;	;;CALL SegGate:0
;	;
;	CALL FAR [THISF_ADR+TSSCrt-4]
;	MOV AX, [THISF_ADR+TSSMax]
;	MOV BX, [THISF_ADR+TSSCrt]
;	ADD BX, 8*2
;	CMP BX, AX
;	JBE Timer_70HINTHandler_Next0
;	MOV BX, 8*0x11
;	Timer_70HINTHandler_Next0:
;	MOV WORD[THISF_ADR+TSSCrt], BX
;	;
;	POP DS
	MOV EDI, RotPrint
	MOV ESI, msg_on_1s+Linear
	CALL SegGate:0
	MOV AL, 0x20; EOI
	OUT 0xA0, AL; SLAVE
	OUT 0x20, AL; MASTER
	; OPEN NMI AFTER READ REG-C, OR ONLY INT ONCE
	MOV AL, 0X0C
	OUT 0X70, AL
	IN AL, 0x71
	POPAD
	IRETD

_Handexc_General:
	MOV EAX, SegData
	MOV DS, EAX
	MOV ESI, msg_general_exception+Linear
	MOV EDI, RotPrint
	CALL SegGate:0
	DbgStop

_Handint_General:
	PUSH EAX
	MOV AL, 0x20; EOI
	OUT 0xA0, AL; SLAVE
	OUT 0x20, AL; MASTER
	POP EAX
	IRETD

section .data
	msg_on_1s:
		DB "<Ring~> ",0
	msg_general_exception:
		DB "General Exception!",10,13,0

