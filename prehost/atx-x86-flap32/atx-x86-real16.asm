GLOBAL SwitchReal16

;{TODO} UNISYM BIOS Real16 and its ld. Then we can call it. (`00080000`~`0009FFFF`)
;{TODO} Link this below 0x10000

;{UNFINISHED}
SwitchReal16:
	[BITS 32]
	; flap32 -> real16
	PUSHAD
	JMP DWORD 8*4:PointReal16;{} can do this directly
	[BITS 16]
	PointReal16:
		MOV EAX, CR0
		AND EAX, 0x7FFFFFFE
		MOV CR0, EAX
		JMP WORD 0:PointReal16_switch_CS
	PointReal16_switch_CS:
		MOV AX, 0
		MOV SS, AX
		MOV DS, AX
		MOV ES, AX
		MOV FS, AX
		MOV GS, AX

	MOV EAX, CR0
	OR EAX, 0x80000001
	MOV CR0, EAX
	JMP WORD 8*2:PointBack32
	[BITS 32]
	PointBack32:
		MOV EAX, 8*1
		MOV DS, AX
		MOV ES, AX
		MOV FS, AX
		MOV GS, AX
		MOV SS, AX
		POPAD
	RET
