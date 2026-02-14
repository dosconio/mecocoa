.section ".text.rupt"
.global _vectors

_vectors:
	# b _reset
	# .align 7 // 128B-Alignment
	# b _undefined
	# .align 7
	# b _softrupt
	# .align 7
	# b _prefetch
	# .align 7
	# b _data
	# .align 7
	# b _reserved
	# .align 7
	# b _irq
	# .align 7
	# b _fiq

.section ".text.boot"
.global _entry

_entry:
	// 1. enable BootCore Only
	mrs     x0, mpidr_el1
	and     x0, x0, #0xFF
	cbz     x0, master
	b       halt

master:
    // 2. set stack
    ldr     x0, =stack_top
    mov     sp, x0
    bl      main

halt:
	wfe
	b       halt

.section ".text"



