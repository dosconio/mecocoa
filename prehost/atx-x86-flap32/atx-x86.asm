; ASCII AASM TAB4 LF
; Attribute: 
; LastCheck: 20240218
; AllAuthor: @ArinaMgk
; ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "osdev.a"
%include "assembl/ladder.a"

SegData EQU 8*3
SegCo32 EQU 8*4

ROOT_PAGING EQU 0x00100000

PERCORE_kernel_stack EQU 264
PERCORE_user_cr3     EQU 272

[BITS 32]

EXTERN interrupt_dispatcher
EXTERN PG_PUSH, PG_POP

; Call Gate Method
GLOBAL Handint_SYSCALL_Entry
EXTERN Handint_SYSCALL
EXTERN C_PCU_CORES_PERCORE
CF_DI			EQU 0 * 4
CF_SI			EQU 1 * 4
CF_BP			EQU 2 * 4
CF_SP			EQU 3 * 4
CF_BX			EQU 4 * 4
CF_DX			EQU 5 * 4
CF_CX			EQU 6 * 4
CF_AX			EQU 7 * 4
CF_FLAGS		EQU 8 * 4
CF_IP			EQU 9 * 4
CF_CS			EQU 10 * 4
CF_SP0			EQU 11 * 4
CF_SS0			EQU 12 * 4
CF_CR3			EQU 13 * 4
CF_DS			EQU 14 * 4
CF_ES			EQU 15 * 4
CF_FS			EQU 16 * 4
CF_GS			EQU 17 * 4
CF_TRANS_FRAME	EQU 18 * 4
CF_PERCORE		EQU 19 * 4

IF_DI			EQU 0 * 4
IF_SI			EQU 1 * 4
IF_BP			EQU 2 * 4
IF_SP			EQU 3 * 4
IF_BX			EQU 4 * 4
IF_DX			EQU 5 * 4
IF_CX			EQU 6 * 4
IF_AX			EQU 7 * 4

IF_VECTOR		EQU 8 * 4
IF_ERROR		EQU 9 * 4

IF_IP			EQU 10 * 4
IF_CS			EQU 11 * 4
IF_FLAGS		EQU 12 * 4
IF_SP0			EQU 13 * 4
IF_SS0			EQU 14 * 4

IF_CR3			EQU 15 * 4
IF_DS			EQU 16 * 4
IF_ES			EQU 17 * 4
IF_FS			EQU 18 * 4
IF_GS			EQU 19 * 4

IF_TRANS_FRAME	EQU 20 * 4
IF_PERCORE		EQU 21 * 4

INTFRAME_TOTAL_SIZE		EQU 22 * 4
IRQ_RING3_FRAME_DWORDS	EQU 15
IRQ_RING0_FRAME_DWORDS	EQU 13

CALLGATE_TOTAL_SIZE		EQU 20 * 4
RING3_FRAME_DWORDS		EQU 13
RING0_FRAME_DWORDS		EQU 11

%macro COPY_DWORDS 3
	MOV ESI, %1
	MOV EDI, %2
	MOV ECX, %3
	CLD
	REP MOVSD
%endmacro

%macro LOAD_KERNEL_SEGS 0
	MOV AX, SegData
	MOV DS, AX
	MOV ES, AX
	MOV FS, AX
	MOV GS, AX
%endmacro

%macro SAVE_SEGS_TO_FRAME 1
	XOR EAX, EAX
	MOV AX, DS
	MOV [%1 + CF_DS], EAX

	XOR EAX, EAX
	MOV AX, ES
	MOV [%1 + CF_ES], EAX

	XOR EAX, EAX
	MOV AX, FS
	MOV [%1 + CF_FS], EAX

	XOR EAX, EAX
	MOV AX, GS
	MOV [%1 + CF_GS], EAX
%endmacro

%macro RESTORE_SEGS_FROM_FRAME 1
	MOV EAX, [%1 + CF_GS]
	MOV GS, AX

	MOV EAX, [%1 + CF_FS]
	MOV FS, AX

	MOV EAX, [%1 + CF_ES]
	MOV ES, AX

	MOV EAX, [%1 + CF_DS]
	MOV DS, AX
%endmacro

%macro SAVE_SEGS_TO_IFRAME 1
	XOR EAX, EAX
	MOV AX, DS
	MOV [%1 + IF_DS], EAX

	XOR EAX, EAX
	MOV AX, ES
	MOV [%1 + IF_ES], EAX

	XOR EAX, EAX
	MOV AX, FS
	MOV [%1 + IF_FS], EAX

	XOR EAX, EAX
	MOV AX, GS
	MOV [%1 + IF_GS], EAX
%endmacro

%macro RESTORE_SEGS_FROM_IFRAME 1
	MOV EAX, [%1 + IF_GS]
	MOV GS, AX

	MOV EAX, [%1 + IF_FS]
	MOV FS, AX

	MOV EAX, [%1 + IF_ES]
	MOV ES, AX

	MOV EAX, [%1 + IF_DS]
	MOV DS, AX
%endmacro


Handint_SYSCALL_Entry:
	CLI
	PUSHFD
	PUSHAD

	; After PUSHFD + PUSHAD:
	;
	; [ESP + 00] = DI
	; [ESP + 04] = SI
	; [ESP + 08] = BP
	; [ESP + 12] = SP saved by PUSHAD
	; [ESP + 16] = BX
	; [ESP + 20] = DX
	; [ESP + 24] = CX
	; [ESP + 28] = AX
	; [ESP + 32] = FLAGS
	; [ESP + 36] = return IP
	; [ESP + 40] = return CS
	; [ESP + 44] = old ESP, only when privilege changed
	; [ESP + 48] = old SS, only when privilege changed

	MOV EAX, [ESP + CF_CS]
	AND EAX, 3
	JZ .from_ring0


.from_ring3:
	; Save original user segment selectors on transition stack.
	XOR EAX, EAX
	MOV AX, DS
	PUSH EAX

	XOR EAX, EAX
	MOV AX, ES
	PUSH EAX

	XOR EAX, EAX
	MOV AX, FS
	PUSH EAX

	XOR EAX, EAX
	MOV AX, GS
	PUSH EAX

	; EBP = original 13-dword return frame on transition stack.
	LEA EBP, [ESP + 16]

	; EBX = user CR3.
	MOV EBX, CR3

	; Compute CPU id from transition stack page.
	MOV ECX, ESP
	AND ECX, 0xFFFFF000

	MOV EAX, 0xFFFFF000
	SUB EAX, ECX
	SHR EAX, 12
	MOV ESI, EAX

	; Access kernel high mapping while still under user CR3.
	MOV AX, SegData
	MOV DS, AX
	MOV ES, AX

	MOV EDI, [0x80000000 + C_PCU_CORES_PERCORE + ESI * 4]
	MOV EDX, EDI

	; Switch to root paging.
	MOV EAX, ROOT_PAGING
	CMP EBX, EAX
	JE .skip_root_cr3
	MOV CR3, EAX
.skip_root_cr3:

	; Switch to current thread's own kernel stack.
	MOV ESP, [EDX + PERCORE_kernel_stack]
	SUB ESP, CALLGATE_TOTAL_SIZE

	; Copy DI..SS0, 13 dwords.
	COPY_DWORDS EBP, ESP, RING3_FRAME_DWORDS

	; Fill full CallgateFrame metadata.
	MOV [ESP + CF_CR3], EBX
	MOV [ESP + CF_PERCORE], EDX
	MOV [ESP + CF_TRANS_FRAME], EBP

	MOV EAX, [EBP - 4]
	MOV [ESP + CF_DS], EAX

	MOV EAX, [EBP - 8]
	MOV [ESP + CF_ES], EAX

	MOV EAX, [EBP - 12]
	MOV [ESP + CF_FS], EAX

	MOV EAX, [EBP - 16]
	MOV [ESP + CF_GS], EAX

	; C handler should run with kernel data segments.
	LOAD_KERNEL_SEGS

	; EBP = full CallgateFrame*.
	; EAX = return mode, 3 means privilege-changing return frame.
	MOV EBP, ESP
	MOV EAX, 3
	JMP .call_handint_common


.from_ring0:
	; Ring0 call gate has no old SS:ESP.
	; Real ring0 return frame is only:
	; DI SI BP SP BX DX CX AX FLAGS IP CS

	MOV EBP, ESP

	; Build a full 20-dword CallgateFrame on current thread kernel stack.
	SUB ESP, CALLGATE_TOTAL_SIZE

	; Copy DI..CS, 11 dwords.
	COPY_DWORDS EBP, ESP, RING0_FRAME_DWORDS

	; Fill ring3-only fields with valid ring0 placeholders.
	MOV EAX, [EBP + CF_SP]
	MOV [ESP + CF_SP0], EAX
	MOV DWORD [ESP + CF_SS0], SegData

	; Fill metadata.
	MOV EAX, CR3
	MOV [ESP + CF_CR3], EAX

	SAVE_SEGS_TO_FRAME ESP

	MOV [ESP + CF_TRANS_FRAME], EBP
	MOV DWORD [ESP + CF_PERCORE], 0

	; C handler should run with kernel data segments.
	LOAD_KERNEL_SEGS

	; EBP = full CallgateFrame*.
	; EAX = return mode, 0 means same-ring return frame.
	MOV EBP, ESP
	XOR EAX, EAX
	JMP .call_handint_common


.call_handint_common:
	; Input:
	;	EAX = return mode
	;		0 = ring0 return
	;		3 = outer-ring return
	;	EBP = full CallgateFrame*
	;
	; Save mode and frame pointer on current thread kernel stack.
	; This survives normal C calls and scheduler continuations.

	PUSH EAX
	PUSH EBP

	PUSH EBP
	CALL Handint_SYSCALL
	ADD ESP, 4

	POP EBP
	MOV [EBP + CF_AX], EAX

	POP EAX
	TEST EAX, EAX
	JZ .return_ring0
	JMP .return_ring3


.return_ring3:
	; EBP = full CallgateFrame*.

	MOV EDX, [EBP + CF_TRANS_FRAME]
	MOV EBX, [EBP + CF_CR3]

	; Copy DI..SS0 back to transition return frame.
	COPY_DWORDS EBP, EDX, RING3_FRAME_DWORDS

	; Restore original outer-ring segment selectors.
	RESTORE_SEGS_FROM_FRAME EBP

	; Return through transition frame under original user CR3.
	MOV EAX, CR3
	CMP EAX, EBX
	MOV ESP, EDX
	JE .skip_user_cr3
	MOV CR3, EBX
.skip_user_cr3:

	; Transition stack layout here is:
	;   DI SI BP SP BX DX CX AX FLAGS IP CS SP0 SS0
	; Reorder only the tail into IRETD order before POPAD:
	;   DI SI BP SP BX DX CX AX IP CS FLAGS SP0 SS0
	; This avoids restoring IF with POPFD while still on the transition stack.
	MOV EAX, [ESP + 32]
	MOV EBX, [ESP + 36]
	MOV ECX, [ESP + 40]
	MOV [ESP + 32], EBX
	MOV [ESP + 36], ECX
	MOV [ESP + 40], EAX
	POPAD
	IRETD


.return_ring0:
	; EBP = full CallgateFrame*.

	MOV EDX, [EBP + CF_TRANS_FRAME]

	; Copy only DI..CS back to original ring0 return frame.
	COPY_DWORDS EBP, EDX, RING0_FRAME_DWORDS

	; Restore original ring0 segment selectors.
	RESTORE_SEGS_FROM_FRAME EBP

	; Return through original ring0 frame.
	MOV ESP, EDX

	POPAD
	POPFD
	RETF

GLOBAL Handint_INTCALL_Entry; {unchk}
Handint_INTCALL_Entry:
; TODO
IRETD


GLOBAL Handint_Common_Stub
Handint_Common_Stub:
	PUSHAD

	; After IRQ_TRAMPOLINE + PUSHAD:
	;
	; [ESP + 00] = EDI
	; [ESP + 04] = ESI
	; [ESP + 08] = EBP
	; [ESP + 12] = ESP saved by PUSHAD
	; [ESP + 16] = EBX
	; [ESP + 20] = EDX
	; [ESP + 24] = ECX
	; [ESP + 28] = EAX
	; [ESP + 32] = interrupt vector
	; [ESP + 36] = error code
	; [ESP + 40] = interrupted EIP
	; [ESP + 44] = interrupted CS
	; [ESP + 48] = interrupted EFLAGS
	; [ESP + 52] = interrupted ESP, only when ring changed
	; [ESP + 56] = interrupted SS, only when ring changed

	MOV EAX, [ESP + IF_CS]
	AND EAX, 3
	JZ IRQ_FROM_RING0


IRQ_FROM_RING3:
	; Save user segment selectors on transition stack.
	XOR EAX, EAX
	MOV AX, DS
	PUSH EAX

	XOR EAX, EAX
	MOV AX, ES
	PUSH EAX

	XOR EAX, EAX
	MOV AX, FS
	PUSH EAX

	XOR EAX, EAX
	MOV AX, GS
	PUSH EAX

	; EBP points to original HardwareInterruptFrame on transition stack.
	LEA EBP, [ESP + 16]

	; EBX = interrupted user CR3.
	MOV EBX, CR3

	; Compute CPU id from transition stack page.
	MOV ECX, ESP
	AND ECX, 0xFFFFF000

	MOV EAX, 0xFFFFF000
	SUB EAX, ECX
	SHR EAX, 12
	MOV ESI, EAX

	; Access percore under current CR3.
	MOV AX, SegData
	MOV DS, AX
	MOV ES, AX

	MOV EDI, [0x80000000 + C_PCU_CORES_PERCORE + ESI * 4]
	MOV EDX, EDI

	; Switch to root paging.
	MOV EAX, ROOT_PAGING
	CMP EBX, EAX
	JE IRQ_SKIP_ROOT_CR3
	MOV CR3, EAX

IRQ_SKIP_ROOT_CR3:

	; Switch to current thread kernel stack.
	MOV ESP, [EDX + PERCORE_kernel_stack]
	SUB ESP, INTFRAME_TOTAL_SIZE

	; Copy EDI..HW_SS from transition stack to thread kernel stack.
	COPY_DWORDS EBP, ESP, IRQ_RING3_FRAME_DWORDS

	; Fill x86 metadata.
	MOV [ESP + IF_CR3], EBX
	MOV [ESP + IF_PERCORE], EDX
	MOV [ESP + IF_TRANS_FRAME], EBP

	MOV EAX, [EBP - 4]
	MOV [ESP + IF_DS], EAX

	MOV EAX, [EBP - 8]
	MOV [ESP + IF_ES], EAX

	MOV EAX, [EBP - 12]
	MOV [ESP + IF_FS], EAX

	MOV EAX, [EBP - 16]
	MOV [ESP + IF_GS], EAX

	LOAD_KERNEL_SEGS

	MOV EBP, ESP
	MOV EAX, 3
	JMP IRQ_CALL_INTERRUPT_COMMON


IRQ_FROM_RING0:
	; Normally ring0 interrupt already runs on kernel stack.
	; But if an exception happens before ring3 path switches away from
	; transition stack, CPU sees it as ring0 -> ring0 and will not switch stack.
	; Detect that case first.

	MOV EAX, ESP
	AND EAX, 0xFFFF0000
	CMP EAX, 0xFFFF0000
	JE IRQ_RING0_ON_TRANSITION_STACK

	JMP IRQ_FROM_RING0_NORMAL


IRQ_RING0_ON_TRANSITION_STACK:
	; Debug stop:
	; We are in ring0, but ESP is still in high transition-stack area.
	; Do not call C handler on transition stack.
	CLI
	HLT
	JMP IRQ_RING0_ON_TRANSITION_STACK


IRQ_FROM_RING0_NORMAL:
	; Ring0 interrupt has no old ESP/SS in hardware frame.
	; Real frame length is EDI..EFLAGS = 13 dwords.

	MOV EBP, ESP

	SUB ESP, INTFRAME_TOTAL_SIZE

	; Copy EDI..EFLAGS.
	COPY_DWORDS EBP, ESP, IRQ_RING0_FRAME_DWORDS

	; Fill fake hw_esp / hw_ss for ring0 interrupt.
	MOV EAX, [EBP + IF_SP]
	MOV [ESP + IF_SP0], EAX
	MOV DWORD [ESP + IF_SS0], SegData

	; Fill metadata.
	MOV EAX, CR3
	MOV [ESP + IF_CR3], EAX

	SAVE_SEGS_TO_IFRAME ESP

	MOV [ESP + IF_TRANS_FRAME], EBP
	MOV DWORD [ESP + IF_PERCORE], 0

	LOAD_KERNEL_SEGS

	MOV EBP, ESP
	XOR EAX, EAX
	JMP IRQ_CALL_INTERRUPT_COMMON


IRQ_CALL_INTERRUPT_COMMON:
	; EAX = return mode:
	;   0 = ring0 return
	;   3 = ring3 return
	; EBP = HardwareInterruptFrame*

	PUSH EAX
	PUSH EBP

	PUSH EBP
	CALL interrupt_dispatcher
	ADD ESP, 4

	POP EBP
	POP EAX

	TEST EAX, EAX
	JZ IRQ_RETURN_RING0
	JMP IRQ_RETURN_RING3


IRQ_RETURN_RING3:
	; EBP = HardwareInterruptFrame* on thread kernel stack.

	MOV EDX, [EBP + IF_TRANS_FRAME]
	MOV EBX, [EBP + IF_CR3]

	; Copy modified frame back to transition stack.
	COPY_DWORDS EBP, EDX, IRQ_RING3_FRAME_DWORDS

	RESTORE_SEGS_FROM_IFRAME EBP

	MOV EAX, CR3
	CMP EAX, EBX
	MOV ESP, EDX
	JE IRQ_SKIP_USER_CR3
	MOV CR3, EBX

IRQ_SKIP_USER_CR3:
	POPAD
	ADD ESP, 8
	IRETD


IRQ_RETURN_RING0:
	; EBP = HardwareInterruptFrame* on current kernel stack.

	MOV EDX, [EBP + IF_TRANS_FRAME]

	; Copy modified frame back to original ring0 stack.
	COPY_DWORDS EBP, EDX, IRQ_RING0_FRAME_DWORDS

	RESTORE_SEGS_FROM_IFRAME EBP

	MOV ESP, EDX

	POPAD
	ADD ESP, 8
	IRETD








%macro IRQ_TRAMPOLINE 2
GLOBAL %1
%1:
	PUSH DWORD 0        ; Dummy Error Code
	PUSH DWORD %2       ; Interrupt ID
	JMP Handint_Common_Stub
%endmacro

; 0x20..0x27
IRQ_TRAMPOLINE Handint_PIT_Entry, 0x20
IRQ_TRAMPOLINE Handint_KBD_Entry, 0x21
IRQ_TRAMPOLINE Handint_CAS_Entry, 0x22
IRQ_TRAMPOLINE Handint_COM2_Entry, 0x23
IRQ_TRAMPOLINE Handint_COM1_Entry, 0x24
IRQ_TRAMPOLINE Handint_LPT2_Entry, 0x25
IRQ_TRAMPOLINE Handint_FLP_Entry, 0x26
IRQ_TRAMPOLINE Handint_LPT1_Entry, 0x27

; 0x70..0x77
IRQ_TRAMPOLINE Handint_RTC_Entry, 0x70
IRQ_TRAMPOLINE Handint_AHCI_Entry, 0x72
IRQ_TRAMPOLINE Handint_MOU_Entry, 0x74
IRQ_TRAMPOLINE Handint_HDD_Entry, 0x76
IRQ_TRAMPOLINE Handint_HDD1_Entry, 0x77
IRQ_TRAMPOLINE Handint_RESCHED_Entry, 0xF1
IRQ_TRAMPOLINE Handint_WAKE_Entry, 0xF2

GLOBAL RefreshGDT
RefreshGDT:
	LoadDataSegs SegData
	MOV EAX, SegCo32
	PUSH EAX
	MOV EAX, RefreshGDT_NEXT
	PUSH EAX
	RETF
RefreshGDT_NEXT: RET

LocalEOI:
