; CRLF

%macro rprint 1
	MOV SI, %1
	MOV AH, 0
	INT 80H
%endmacro
%macro xprint 1
	MOV EDI, RotPrint
	MOV ESI, (%1)+Linear
	CALL SegGate:0
%endmacro

%macro rprinti32 1
	MOV EDX, %1
	MOV AH, 1
	INT 80H
%endmacro
%macro xprinti32 0-1 $
	MOV EDI, RotEchoDword
	MOV EDX, %1
	CALL SegGate:0
%endmacro

%macro rcpgates 0-1 EDI; kernel copy gates
	%%lup:
	MOV [%1], EAX
	ADD EDI, 4
	MOV [%1], EDX
	ADD EDI, 4
	LOOP %%lup
%endmacro

;{TODO} Use dynamic but static loading method;
ADDR_KERNELBF EQU 0x0800
ADDR_IDT32    EQU 0x0800; - Interrupt Descriptor Table
ADDR_STATICLD EQU 0x1000
KPT_ADDR      EQU 0x1000; - Kernel(Shell) Page Table
PDT_ADDR      EQU 0x2000; - Shell Page Directory Table
ADDR_STACKTOP EQU 0x4000
ADDR_TSS16    EQU 0x4000
ADDR_TSS32    EQU 0x4080
ADDR_GDT      EQU 0x4100
SEGM_BITMAP   EQU 0x6000; - Memory Bit Map

GDTDptr   EQU 0x0504
IVTDptr   EQU 0x050A

Linear EQU 0x80000000

; ---- ---- Segment Selectors ---- ----

SegNull   EQU (8*0)
SegData   EQU (8*1)
SegCode   EQU (8*2)
SegStack  EQU (8*3)
SegVideo  EQU (8*4)
SegRot    EQU (8*5)
SegGate   EQU (8*6)
SegTSS    EQU (8*7)
SegShell  EQU (8*0x10); TSS
SegShellT EQU (8*0x11); LDT

;RotTerminal   EQU -1
RotPrint      EQU 0
RotEchoDword  EQU 1
RotMalloc     EQU 2
RotMfree      EQU 3
RotReaddisk   EQU 4

RotTerminal   EQU 0x0F
RotDptrStruct EQU 0x10

