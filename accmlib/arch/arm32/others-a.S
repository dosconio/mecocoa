// ASCII ASM TAB4 LF
// ModuTitle: User library - ARM32 syscall entry and process startup

	.syntax unified
	.thumb
	.text

	.global syscall_bridge
	.thumb_func
syscall_bridge:
	svc #0
	bx lr

	.global _start
	.thumb_func
_start:
	bl main
	mov r1, r0
	movs r0, #2// syscall_t::EXIT
	movs r2, #0
	movs r3, #0
	bl syscall_bridge
	b .
