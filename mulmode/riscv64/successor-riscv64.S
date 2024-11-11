// ASCII GNU-ASM TAB4 LF
// LastCheck: 20240523
// AllAuthor: @dosconio
// ModuTitle: Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

// succeed from RustSBI
	.section .text.entry
	.globl _entry
_entry:
	la sp, boot_stack_top
	call main

	.section .bss.stack
	.globl boot_stack
boot_stack:
	.space 4096 * 16
	.globl boot_stack_top
boot_stack_top:
