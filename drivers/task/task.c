


/*
F_TSSStruct3:
	; TSSStruct_Mecocoa
	RET 4*1
*/

/*
%macro TSSStruct_Mecocoa 0
;	;{} Solve issue and feedback to unisym.
;	PUSHAD
;	MOV EBP, ESP
;	MOV ECX, 512
;	CALL %%M_MemAllocForUser
;	MOV ESI, EAX; {ESI=HEADER_ADDR}
;	MOV [EBP+4*4], EAX; Return Value(EBX)
;	MOV EBX, EAX
;	MOV EAX, [EBP+9*4]
;	; DFLoad
;		PUSHAD
;		MOV EDI, RotReaddisk
;		CALL SegGate:0
;		SUB EBX, 512
;		MOV ECX, DWORD[EBX]
;		ADD ECX, 511
;		AND ECX, 0xFFFFFE00; shr9 & shl9
;		MOV DWORD[EBX], ECX
;		SUB ECX, 512
;		JZ %%F_ENDO
;			INC EAX
;			PUSH EAX
;			CALL %%M_MemAllocForUser
;		MOV EBX, EAX; ~= ADD EBX,512
;		POP EAX
;		SHL ECX, 9
;		%%READLOOP:
;			PUSH ECX
;			MOV EDI, RotReaddisk
;			CALL SegGate:0 
;			INC EAX
;			POP ECX
;			LOOP %%READLOOP
;			JMP %%F_ENDO
;		%%F_ENDO:
;		POPAD
;	MOV WORD[ESI+98], -1; LDT length 0
;	; CLEAR PAGEDIR USER PART; When switch takes or suspend a task, make a copy of User 512B PDir.
;		; MOV EBX, 0xFFFFF000
;		; XOR EDI, EDI
;		; MOV ECX, 512
;		; %%PD_USER_CLR:
;		; 	MOV DWORD[EBX+EDI*4], 0
;		; 	INC EDI
;		; LOOP %%PD_USER_CLR
;	; SETUP HEADER #00 PUSH(ES=HeaderSeg)
;		MOV EAX, ESI
;		MOV EBX, 256-1
;		MOV ECX, 0x0040F200; [Byt][32][KiSys][DATA][R3][RW/RE]
;		CALL %%F_GDTDptrStruct
;		MOV EBX, ESI
;		CALL %%F_LDTDptrAppend
;		OR CX, 3; SET RING3
;		PUSH CX; ES
;
;	; JUDGE MAGIC NUMBER
;		CMP WORD[ESI+10], 0x32DE; Demos 32
;		JZ %%NEXT1
;		%%ERR:
;		MOV DWORD[EBP+4*4], 1; 0
;		JMP %%Endo
;		%%NEXT1:
;
;	; SETUP LDT CODE SEGM
;		MOV EAX, [ESI+76]
;		ADD EAX, ESI
;		MOV EBX, [ESI+72]
;		DEC EBX
;		MOV ECX, 0x0040F800; [Byt][32][KiSys][CODE][R3][R/E]
;		CALL %%F_GDTDptrStruct
;		MOV EBX, ESI
;		CALL %%F_LDTDptrAppend
;		OR CX, 3; SET RING3
;		MOV WORD[ESI+76], CX; CS
;		POP CX
;		MOV WORD[ESI+72],CX
;
;	; SETUP LDT DATA SEGM
;		MOV EAX, [ESI+84]
;		ADD EAX, ESI
;		MOV EBX, [ESI+80]
;		DEC EBX
;		MOV ECX, 0x0040F200; [Byt][32][KiSys][DATA][R3][RW/RE]
;		CALL %%F_GDTDptrStruct
;		MOV EBX, ESI
;		CALL %%F_LDTDptrAppend
;		OR CX,3; SET RING3
;		MOV WORD[ESI+84],CX; DS
;	; SETUP LDT RONL SEGM
;		MOV EAX, [ESI+92]
;		ADD EAX, ESI
;		MOV EBX, [ESI+88]
;		DEC EBX
;		MOV ECX,0x0040F000; [Byt][32][KiSys][DATA][R3][R/E]
;		CALL %%F_GDTDptrStruct
;		MOV EBX, ESI
;		CALL %%F_LDTDptrAppend
;		OR CX, 3; SET RING3
;		MOV WORD[ESI+92], CX; GS
;	; SETUP LDT STACK 0~2 SEGM
;		XOR EDI, EDI
;		MOV ECX, 3
;		%%NEXT2:
;		PUSH ECX
;		MOV ECX, 0x1000; 4K
;		CALL %%M_MemAllocForUser
;		ADD EAX, ECX; STACK BASE FROM HIGH END
;		MOV EBX, 0xFFFFE; 4K
;		MOV ECX, 00C09600H; [4KB][32][KiSys][DATA][R0][RW^/RE^]
;		OR EDI, EDI
;		JZ %%NEXT3
;		PUSH ECX
;		PUSH EBP
;			MOV ECX, EDI
;			%%NEXT4:
;			MOV EBP, ESP
;			ADD DWORD[EBP], 2000H
;			LOOP %%NEXT4
;		POP EBP
;		POP ECX
;		%%NEXT3:
;		CALL %%F_GDTDptrStruct
;		MOV EBX, ESI
;		CALL %%F_LDTDptrAppend
;		OR ECX, EDI; ring
;			PUSH EDI; TSS to fill
;			SHL EDI, 3
;			ADD EDI, ESI
;			MOV DWORD[EDI+4], 0
;			MOV WORD[EDI+8], CX
;			POP EDI
;		INC EDI
;		POP ECX
;		LOOP %%NEXT2
;	; SETUP LDT STACK 3 SEGM
;		MOV EBX, 0x000_FFFFF
;		MOV ECX, [ESI+56]
;		ADD ECX, 0x1000-1; align
;		SHR ECX, 3*4
;		SUB EBX, ECX
;		SHL ECX, 3*4
;		CALL %%M_MemAllocForUser; ecx -> eax
;		ADD EAX, ECX
;		MOV ECX, 0x00C0F600; [4KB][32][KiSys][DATA][R3][RW^/RE^]
;		CALL %%F_GDTDptrStruct
;		MOV EBX, ESI
;		CALL %%F_LDTDptrAppend
;		OR CX, 3
;		MOV WORD[ESI+80], CX
;		MOV DWORD[ESI+56], 0; ESP
;	; Other TSS
;		MOV WORD[ESI], 0; Parent
;		MOV WORD[ESI+2], 0; AVAILABLE State
;		; EFLAGS
;		XOR EAX, EAX; RESET EFLAGS
;		PUSHFD
;		POP EAX
;		MOV DWORD[ESI+36], EAX
;		MOV WORD[ESI+100], 0; T
;		MOV WORD[ESI+102], 104-1; I/O REF BASE (NON-EXIST)
;		MOV WORD[ESI+88], 0; FS
;		MOV EAX, CR3
;		MOV DWORD[ESI+28], EAX; PDBR	
;		;{} One CR3 for only one task  ; CALL %%M_CopyPDir; eax=PhizikAddr
;	; Register LDT & TSS
;		SGDT [THISF_ADR+GDTDptr]
;		MOV EBX, DWORD[THISF_ADR+GDTDptr+2]
;		XOR EAX, EAX
;		MOV AX, [THISF_ADR+GDTDptr]
;		INC AX
;		ADD EBX, EAX
;		MOV WORD[ESI+96], AX; LDTSel
;		;
;		PUSH EBX
;		MOV EAX, ESI
;		ADD EAX, 104; LDT
;		XOR EBX, EBX
;		MOV BX, [ESI+98];94
;		MOV ECX, 0x00408200; [Byt][32][NaSys][DATA][R0][RW/RE]
;		CALL F_GDTDptrStruct_32
;		POP EBX
;		GDTDptrAppend EDX, EAX
;		;
;		PUSH EBX
;		MOV EAX, ESI; TSS
;		MOV EBX, 104-1
;		MOV ECX, 0x00408900; TSS RING0
;		CALL F_GDTDptrStruct_32
;		POP EBX
;		GDTDptrAppend EDX, EAX
;		ADD WORD[THISF_ADR+GDTDptr], 8*2
;		LGDT [THISF_ADR+GDTDptr]
;		MOV [ESI+48], ESI
;		JMP %%Endo
;	%%M_MemAlloc:
;		PUSH EDI
;		MOV EDI, RotMalloc
;		CALL SegGate:0
;		POP EDI
;		OR EAX, 0x80000000; Into Before Linear Address
;		RET
;	%%M_MemAllocForUser:
;		PUSH EDI
;		MOV EDI, RotMalloc
;		CALL SegGate:0
;		POP EDI
;		RET
;	%%F_LDTDptrAppend:
;		LDTDptrAppend
;		RET
;	%%F_GDTDptrStruct:
;		GDTDptrStruct EAX,EBX,ECX
;		RET
;	%%F_BitmapAllocBottom:
;		PUSH EBX
;		MOV EBX, MAP_LDDR
;		BitmapAllocBottomUser
;		POP EBX
;		RET
;	%%M_CopyPDir:; in(void)-out(eax=PhizikAddr)~pres(ds=es=SegGlb)
;		;{} Without Testing
;		PUSH ESI
;		PUSH EDI
;		PUSH EBX
;		PUSH ECX
;		CALL %%F_BitmapAllocBottom
;		MOV EBX, EAX
;		OR EBX, 0X00000007
;		MOV [0XFFFFFFF8],EBX
;		MOV ECX,1024/2
;		MOV ESI, 0XFFFFF000; ESI=CrtPDirLinearAddr
;		MOV EDI, 0XFFFFE000; EDI=NewPDirLinearAddr
;		XOR EBX,EBX
;		%%M_CopyPDir_Zero:
;			MOV [EDI], EBX
;			ADD ESI, 4
;			ADD EDI, 4
;			LOOP %%M_CopyPDir_Zero		
;		MOV ECX, 1024/2    ; ECX=NumofItems
;		CLD
;		REPE MOVSD 
;		POP ECX
;		POP EBX
;		POP EDI
;		POP ESI
;	%%Endo:
;	POPAD
%endmacro
*/


