; ASCII AASM TAB4 LF
; Attribute: 
; LastCheck: 20240218
; AllAuthor: @ArinaMgk
; ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "osdev.a"
%include "ladder.a"

SegData EQU 8*3
SegCo32 EQU 8*4

[BITS 32]

GLOBAL Handint_SYSCALL_Entry
EXTERN Handint_SYSCALL
Handint_SYSCALL_Entry:
	PUSHFD
	CLI
	PUSHAD
	CALL PG_PUSH; leave 20 bytes
	LEA EAX, [ESP + 20]
	PUSH EAX
	CALL Handint_SYSCALL
	ADD ESP, 4
	MOV [ESP + 48], EAX;// write back
	CALL PG_POP
	POPAD
	POPFD; STI
RETF

GLOBAL Handint_INTCALL_Entry
Handint_INTCALL_Entry:


;	stduint para[4];// a c d b
;	__asm("mov  %%eax, %0" : "=m"(para[0]));
;	__asm("mov  %%ecx, %0" : "=m"(para[1]));
;	__asm("mov  %%edx, %0" : "=m"(para[2]));
;	__asm("mov  %%ebx, %0" : "=m"(para[3]));
;	__asm("push %ebx;push %ecx;push %edx;push %ebp;push %esi;push %edi;");
;	__asm("call PG_PUSH");
;	// stduint ret = call_body((syscall_t)para[0], para[1], para[2], para[3]);
;	stduint ret;
;
;	__asm("call PG_POP");
;	__asm("mov  %0, %%eax" : : "m"(ret));
;	__asm("pop %edi");// popad
;	__asm("pop %esi");
;	__asm("pop %ebp");
;	__asm("pop %edx");
;	__asm("pop %ecx");
;	__asm("pop %ebx");
;	__asm("mov  %ebp, %esp");
;	__asm("pop  %ebp      ");
IRETD

[BITS 32]
; 0x20
GLOBAL Handint_PIT_Entry
EXTERN Handint_PIT
; 0x21
GLOBAL Handint_KBD_Entry
EXTERN Handint_KBD
; 0x24
GLOBAL Handint_COM1_Entry
EXTERN Handint_COM1
; 0x70
GLOBAL Handint_RTC_Entry
EXTERN Handint_RTC
; 0x74 Mouse
GLOBAL Handint_MOU_Entry
EXTERN Handint_MOU
; 0x76 0x77
GLOBAL Handint_HDD_Entry
EXTERN Handint_HDD

EXTERN PG_PUSH, PG_POP

GLOBAL RefreshGDT
RefreshGDT:
	LoadDataSegs SegData
	MOV EAX, SegCo32
	PUSH EAX
	MOV EAX, RefreshGDT_NEXT
	PUSH EAX
RETF
RefreshGDT_NEXT: RET

Handint_PIT_Entry:
	PUSHAD
	CALL PG_PUSH
	MOV AL, ' '
	OUT 0x20, AL
	CALL Handint_PIT
	CALL PG_POP
	POPAD
	IRETD
Handint_KBD_Entry:
	PUSHAD
	CALL PG_PUSH
	MOV AL, ' '
	OUT 0x20, AL
	CALL Handint_KBD
	CALL PG_POP
	POPAD
	IRETD
Handint_COM1_Entry:
	PUSHAD
	CALL PG_PUSH
	MOV AL, ' '
	OUT 0x20, AL
	CALL Handint_COM1
	CALL PG_POP
	POPAD
	IRETD

; ---- SLAVE PIC BELOW ----
Handint_RTC_Entry:
	PUSHAD
	CALL PG_PUSH
	CALL Handint_RTC
	CALL PG_POP
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	POPAD
	IRETD
Handint_MOU_Entry:
	PUSHAD
	CALL PG_PUSH
	CALL Handint_MOU
	CALL PG_POP
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	POPAD
	IRETD
Handint_HDD_Entry:
	PUSHAD
	CALL PG_PUSH
	CALL Handint_HDD
	CALL PG_POP
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	POPAD
	IRETD

