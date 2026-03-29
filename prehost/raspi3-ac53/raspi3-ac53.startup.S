
.section ".text.boot"

.global _start

_start:
	// read cpu id, stop slave cores
	MRS     x1, mpidr_el1
	AND     x1, x1, #3
	CBZ     x1, 2f
	1: // cpu id == 1 or 2 or 3, sleep
	WFE
	B       1b
	2: // cpu id == 0
	// set top of stack and clear bss (stack grows to a lower address per AAPCS64)
	LDR     x1, =_bss_end
	MOV     sp, x1
	LDR     x1, =_bss_start
	LDR     w2, =_bss_size
	3:  cbz     w2, 4f
	STR     xzr, [x1], #8
	SUB     w2, w2, #1
	CBNZ    w2, 3b

	4:
	BL      _preprocess
	BL      main
B       1b // unreachable

