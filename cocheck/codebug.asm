
GLOBAL _dbgfn

_dbgfn:
	; - [EBP+4*0]=BP
	; - [EBP+4*1]=Return Address
	; - [EBP+4*2]=Address
	; - [EBP+4*3]=Segment(word-eff, dword-len)
	PUSH DWORD 8*(0x10); LDT
	PUSH DWORD 0
	PUSH EBP
	MOV EBP, ESP
	LLDT [EBP+4*2]
	MOV ESP, EBP
	POP EBP
JMP $
