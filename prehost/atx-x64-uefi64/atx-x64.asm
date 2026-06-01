
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
Handint_SYSCALL_Entry:
	; x64 SYSCALL does not switch stacks for us.
	; SWAPGS gives access to the per-CPU PERCORE block:
	;   GS:128 -> PERCORE.kernel_rsp
	;   GS:136 -> PERCORE.scratch
	SWAPGS

	MOV [GS:136], RSP ; Save user RSP to PERCORE.scratch
	MOV RSP, [GS:128] ; Load kernel RSP from PERCORE.kernel_rsp

	; Build a syscall frame on the current thread's kernel entry stack.
	; The C handler treats this like a synthetic interrupt frame.
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

	; Save volatile registers around PG_PUSH
	PUSH RBP
	PUSH RDI
	PUSH RSI
	PUSH RDX
	PUSH R10
	PUSH R8
	PUSH R9
	PUSH RAX
	CALL PG_PUSH
		POP R12
		POP R11
	POP  RAX
	POP  R9
	POP  R8
	POP  R10
	POP  RDX
	POP  RSI
	POP  RDI
	POP  RBP
	PUSH R11
	PUSH R12

	; Dispatch to C handler
	MOV  RDI, RBP
	MOV  RCX, R10
	CALL Handint_SYSCALL
	MOV  R12, RAX

	; Restore CR3
	CALL PG_POP

	MOV  RSP, RBP
	MOV  RAX, R12

	; Restore context
	POP  RCX ; user RIP
	POP  RDX
	POP  RBX
	POP  RBP
	POP  RSI
	POP  RDI
	POP  R8
	POP  R9
	POP  R10
	POP  R11 ; user RFLAGS
	POP  R12
	POP  R13
	POP  R14
	POP  R15
	ADD  RSP, 8 ; Skip RAX slot
	POP  RSP ; Restore user RSP

	; Return to the user-side GS state before SYSRET.
	SWAPGS
O64 SYSRET


EXTERN interrupt_dispatcher

GLOBAL Handint_Common_Stub_64
Handint_Common_Stub_64:
	; Interrupt/exception gates from Ring 3 must pair with the SYSCALL path.
	; We enter the kernel with the kernel-side GS state and restore the
	; user-side GS state again before IRETQ back to Ring 3.
	TEST QWORD [RSP + 24], 3 ; From Ring 3?
	JZ .entry_gs_ready
	SWAPGS
.entry_gs_ready:
	PUSHA64
	CALL PG_PUSH
	MOV RDI, RSP ; Pass HardwareInterruptFrame* as the first and only argument (System V ABI convention)
	CALL interrupt_dispatcher
	CALL PG_POP
	POPA64
	TEST QWORD [RSP + 24], 3 ; Return to Ring 3?
	JZ .exit_gs_ready
	SWAPGS
.exit_gs_ready:
	ADD RSP, 16; Pop Vector ID and Error Code
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
