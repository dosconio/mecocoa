
[BITS 64]

EXTERN kernel_stack

EXTERN EnableSSE
EXTERN _ZN6Memory9clear_bssEv
EXTERN _preprocess

EXTERN mecocoa
EXTERN PG_PUSH, PG_POP

GLOBAL _entry

SegCo16 EQU 8*1
SegData EQU 8*3
SegCo32 EQU 8*4

%ifdef _UEFI
_entry:
    MOV  RSP, kernel_stack + 1024*1024 ; RSP = 16N
    PUSH RDI                            ; 1st push: Save argument
    PUSH RDI                            ; 2nd push: Alignment dummy (keep RSP = 16N)
    CALL EnableSSE                      ; Inside: RSP = 16N - 8 (Correct)
    CALL _ZN6Memory9clear_bssEv         ; Inside: RSP = 16N - 8 (Correct)
    CALL _preprocess                    ; Inside: RSP = 16N - 8 (Correct)
    POP  RAX                            ; Remove dummy
    POP  RDI                            ; Restore argument
    CALL mecocoa                        ; Inside: RSP = 16N - 8 (Correct)
    LUP: HLT
    JMP  LUP
%else
_entry:
	MOV  RSP, 0x7FF0
	CALL EnableSSE
	CALL _ZN6Memory9clear_bssEv; Memory::clear_bss
	CALL _preprocess
	CALL mecocoa
	LUP: HLT
	JMP  LUP
GLOBAL CallCo16
CallCo16:
	MOV WORD[0x500], DI
	SGDT [GDT_PTR]
	MOV  RAX, [GDT_PTR+2]
	MOV DWORD [RAX+SegData+4], 0x00CF9200; SegData
	MOV DWORD [RAX+SegCo16+4], 0x000F9A00; SegCo16
	MOV DWORD [RAX+SegCo32+4], 0x00CF9A00; SegCo32
	AND RAX, 0x3FFF_FFFF
	MOV [GDT_PTR+2], RAX
	LGDT [GDT_PTR]
	PUSH RCX
	PUSH RDX
	PUSH RBX
	PUSH RBP
	PUSH RSI
	PUSH RDI
	PUSH QWORD BackCo16
	; JUMP TO IA32-COMPATIBLE MODE
	PUSH SegCo32
	PUSH 0x8000
	O64 RETF; return to caller
BackCo16:
	MOV  RBX, [GDT_PTR+2]
	OR   RBX, 0xFFFFFFFF_C0000000
	MOV  [GDT_PTR+2], RBX
	LGDT [GDT_PTR]
	POP  RDI
	POP  RSI
	POP  RBP
	POP  RBX
	POP  RDX
	POP  RCX
	RET
HLT
GDT_PTR:
	DW 0
	DQ 0
%endif

GLOBAL setCSSS  ; void setCSSS(uint16 cs, uint16 ss);
setCSSS:
	PUSH RBP
	MOV  RBP, RSP
	MOV  SS, SI
	MOV  RAX, SetCSSS_NEXT
	PUSH RDI    ; CS
	PUSH RAX    ; RIP
	O64 RETF
	SetCSSS_NEXT:
		MOV RSP, RBP
		POP RBP
RET

GLOBAL setDSAll  ; void setDSAll(uint16 value);
setDSAll:
	MOV DS, DI
	MOV ES, DI
	MOV FS, DI
	MOV GS, DI
	RET

GLOBAL tryUD:
tryUD:
	UD2
	RET

%macro PUSHA64 0
	PUSH RAX
	PUSH RCX
	PUSH RDX
	PUSH RBX
	; PUSH RSP
	PUSH RBP
	PUSH RSI
	PUSH RDI
	PUSH R8
	PUSH R9
	PUSH R10
	PUSH R11
	PUSH R12
	PUSH R13
	PUSH R14
	PUSH R15
%endmacro
%macro POPA64 0
	POP  R15
	POP  R14
	POP  R13
	POP  R12
	POP  R11
	POP  R10
	POP  R9
	POP  R8
	POP  RDI
	POP  RSI
	POP  RBP
	; POP RSP
	POP  RBX
	POP  RDX
	POP  RCX
	POP  RAX
%endmacro

; SYSCALL convention: RCX=user_RIP, R11=user_RFLAGS, RSP=user_RSP (unchanged)
; Mecocoa ABI:  RAX=syscall#, RDI=p1, RSI=p2, RDX=p3
GLOBAL Handint_SYSCALL_Entry
EXTERN Handint_SYSCALL
ROOT_PAGING EQU 0xFFFFFFFFC0000508
Handint_SYSCALL_Entry:
	SWAPGS

	MOV [GS:136], RSP ; Save user RSP to PERCORE.scratch
	MOV RSP, [GS:128] ; Load jump board stack from PERCORE.kernel_rsp

	; Phase A: Switch CR3 and jump to real kernel stack
	PUSH R12 ; Save R12 on transition stack
	MOV R12, CR3
	MOV [GS:144], R12 ; Save user CR3 to PERCORE.user_cr3

	; Switch to unified kernel page table if needed.
	; R12 = current/user CR3.
	PUSH R11 ; Save original RFLAGS
	MOV R11, ROOT_PAGING
	MOV R11, [R11] ; R11 = kernel CR3
	CMP R12, R11
	JE SYSCALL_SKIP_KERNEL_CR3
	MOV CR3, R11
SYSCALL_SKIP_KERNEL_CR3:
	POP R11 ; Restore RFLAGS
	
	; Switch RSP to the real kernel stack
	MOV RSP, [GS:392] ; PERCORE.kernel_stack
	
	; [FIX] Save user CR3 on the real kernel stack to survive context switches
	PUSH QWORD [GS:144]
	
	; Restore R12
	MOV R12, [GS:128]
	MOV R12, [R12 - 8]

	; Build a syscall frame on the current thread's real kernel stack
	PUSH QWORD [GS:136] ; push user RSP as sp0
	PUSH RAX
	PUSH R15
	PUSH R14
	PUSH R13
	PUSH R12
	PUSH R11 ; original RFLAGS
	PUSH R10
	PUSH R9
	PUSH R8
	PUSH RDI
	PUSH RSI
	PUSH RBP
	PUSH RBX
	PUSH RDX
	PUSH RCX ; original RIP

	MOV RBP, RSP
	AND RSP, 0xFFFFFFFFFFFFFFF0

	; Save volatile registers
	PUSH RBP
	PUSH RDI
	PUSH RSI
	PUSH RDX
	PUSH R10
	PUSH R8
	PUSH R9
	PUSH RAX

	; Dispatch to C handler
	MOV  RDI, RBP
	MOV  RCX, R10
	CALL Handint_SYSCALL
	MOV  R10, RAX ; Save return value into R10 (safe across SYSRET)

	MOV  RSP, RBP

	; Restore context
	POP  RCX ; user RIP
	POP  RDX
	POP  RBX
	POP  RBP
	POP  RSI
	POP  RDI
	POP  R8
	POP  R9
	ADD  RSP, 8 ; Skip R10 slot, we already saved it
	POP  R11 ; user RFLAGS
	POP  R12
	POP  R13
	POP  R14
	POP  R15
	ADD  RSP, 8 ; Skip RAX slot
	
	; Now stack has: user RSP
	POP R8 ; R8 = user RSP
	
	; [FIX] Pop user CR3 from the real kernel stack
	POP R9 ; R9 = user CR3
	
	; Switch to transition stack for return
	MOV RSP, [GS:128]
	
	; Restore user CR3 if needed.
	; R9 = user CR3.
	MOV RAX, CR3
	CMP RAX, R9
	JE SYSCALL_SKIP_USER_CR3
	MOV CR3, R9
SYSCALL_SKIP_USER_CR3:
	; Restore RAX (return value)
	MOV RAX, R10
	
	; Restore user RSP
	MOV RSP, R8
	
	; Return to the user-side GS state before SYSRET.
	SWAPGS
O64 SYSRET


EXTERN interrupt_dispatcher

GLOBAL Handint_Common_Stub_64
Handint_Common_Stub_64:
	TEST QWORD [RSP + 24], 3 ; From Ring 3?
	JZ IRQ64_FROM_RING0
	
	; From Ring 3
	SWAPGS
	
	; Save RAX so we can use it as a pointer to the transition stack
	MOV [GS:136], RAX 
	
	; Save current/user CR3
	MOV RAX, CR3
	MOV [GS:144], RAX
	; Switch CR3 to unified kernel CR3 if needed.
	MOV RAX, ROOT_PAGING
	MOV RAX, [RAX] ; RAX = kernel CR3
	CMP [GS:144], RAX
	JE IRQ64_SKIP_KERNEL_CR3

	MOV CR3, RAX

IRQ64_SKIP_KERNEL_CR3:
	
	; RSP points to the 7 words pushed by CPU/stub on the transition stack.
	MOV RAX, RSP 
	
	; Switch RSP to the real kernel stack
	MOV RSP, [GS:392]
	
	; Copy the 7 words from transition stack to real stack
	PUSH QWORD [RAX + 48] ; SS
	PUSH QWORD [RAX + 40] ; RSP
	PUSH QWORD [RAX + 32] ; RFLAGS
	PUSH QWORD [RAX + 24] ; CS
	PUSH QWORD [RAX + 16] ; RIP
	PUSH QWORD [RAX + 8]  ; ErrorCode
	PUSH QWORD [RAX + 0]  ; IntNo
	
	; Restore RAX
	MOV RAX, [GS:136]
	JMP IRQ64_DO_PUSHA

IRQ64_FROM_RING0:
	; From Ring 0: already on the correct real kernel stack and CR3.

IRQ64_DO_PUSHA:
	PUSHA64
	MOV RDI, RSP ; Pass HardwareInterruptFrame*
	
	; Align stack to 16 bytes (System V ABI)
	MOV RAX, RSP
	AND RSP, -16
	SUB RSP, 8
	PUSH RAX
	
	CALL interrupt_dispatcher
	
	POP RSP
	POPA64
	
	TEST QWORD [RSP + 24], 3 ; Returning to Ring 3?
	JZ IRQ64_EXIT_RING0
	
	; Returning to Ring 3
	MOV [GS:136], RAX ; Save RAX
	
	; Calculate transition stack address for the 7 words
	MOV RAX, [GS:128]
	SUB RAX, 56
	
	; Copy 7 words from real stack to transition stack
	PUSH R11
	MOV R11, [RSP + 8]  ; IntNo
	MOV [RAX + 0], R11
	MOV R11, [RSP + 16] ; ErrorCode
	MOV [RAX + 8], R11
	MOV R11, [RSP + 24] ; RIP
	MOV [RAX + 16], R11
	MOV R11, [RSP + 32] ; CS
	MOV [RAX + 24], R11
	MOV R11, [RSP + 40] ; RFLAGS
	MOV [RAX + 32], R11
	MOV R11, [RSP + 48] ; RSP
	MOV [RAX + 40], R11
	MOV R11, [RSP + 56] ; SS
	MOV [RAX + 48], R11
	POP R11
	
	; Switch RSP to transition stack
	MOV RSP, RAX
	
	; Restore user CR3 if needed.
	MOV RAX, CR3
	CMP RAX, [GS:144]
	JE IRQ64_SKIP_USER_CR3

	MOV RAX, [GS:144]
	MOV CR3, RAX

IRQ64_SKIP_USER_CR3:
	; Restore RAX
	MOV RAX, [GS:136]
	SWAPGS

IRQ64_EXIT_RING0:
	ADD RSP, 16 ; Skip IntNo and ErrorCode
	IRETQ

%macro IRQ_TRAMPOLINE_64 2
GLOBAL %1
%1:
	PUSH QWORD 0        ; Dummy Error Code
	PUSH QWORD %2       ; Interrupt ID
	JMP Handint_Common_Stub_64
%endmacro

; XHCI Interrupt
IRQ_TRAMPOLINE_64 Handint_XHCI_Entry, 0x40

; LAPIC Timer Interrupt
IRQ_TRAMPOLINE_64 Handint_LAPICT_Entry, 0x41

; COM1 Interrupt
IRQ_TRAMPOLINE_64 Handint_COM1_Entry, 0x24

; PS/2 Keyboard Interrupt
IRQ_TRAMPOLINE_64 Handint_KBD_Entry, 0x21

; PS/2 Mouse Interrupt
IRQ_TRAMPOLINE_64 Handint_MOU_Entry, 0x74
