global ffff
global aaaa
global bbbb
global cccc
extern dddd
extern dddd
[BITS 16]
section .data
	DB "DOSCON.IO"

section .text
	ffff:; will be at 0x6000, dosconio 2024Feb11
	MOV BYTE[ES:8], 'F'
	RET
	aaaa:
	MOV AX, 0xB800
	MOV ES, AX
	MOV BYTE[ES:0], 'A'
	bbbb:
	MOV AX, 0xB800
	MOV ES, AX
	MOV BYTE[ES:2], 'B'
	cccc:
	MOV AX, 0xB800
	MOV ES, AX
	MOV BYTE[ES:4], 'C'
	CALL DWORD dddd; caution "DWORD" dosconio 2024Feb11 1533
	CLI
	HLT

