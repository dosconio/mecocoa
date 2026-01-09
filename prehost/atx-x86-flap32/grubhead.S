#define ASM_FILE        1
#include "multiboot2.h"

/*  C symbol format. HAVE_ASM_USCORE is defined by configure. */
#ifdef HAVE_ASM_USCORE
# define EXT_C(sym)                     _ ## sym
#else
# define EXT_C(sym)                     sym
#endif

/*  The size of our stack (16KB). */
#define STACK_SIZE                      0x4000
	.text
.global entry_address_tag_start
.global _start_eax, _start_ebx
multiboot_header:
	/*  Align 64 bits boundary. */
	.align  8
	/*  magic */
	.long   MULTIBOOT2_HEADER_MAGIC
	/*  ISA: i386 */
	.long   MULTIBOOT_ARCHITECTURE_I386
	/*  Header length. */
	.long   multiboot_header_end - multiboot_header
	/*  checksum */
	.long   -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT_ARCHITECTURE_I386 + (multiboot_header_end - multiboot_header))
entry_address_tag_start:        
	.align  8
	.short MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS
	.short MULTIBOOT_HEADER_TAG_OPTIONAL
	.long entry_address_tag_end - entry_address_tag_start
	/*  entry_addr */
	.long multiboot_entry
entry_address_tag_end:
end_tag_start:
	.align  8
	.short MULTIBOOT_HEADER_TAG_END
	.short 0
	.long end_tag_end - end_tag_start
end_tag_end:
multiboot_header_end:
multiboot_entry:
	movl    %eax, _start_eax
	movl    %ebx, _start_ebx
	/*  Initialize the stack pointer. */
	movl    $(stack + STACK_SIZE), %esp
	/*  Reset EFLAGS. */
	pushl   $0
	popf
	/*  Push the pointer to the Multiboot information structure. */
	pushl   %ebx
	/*  Push the magic value. */
	pushl   %eax
	/*  Now enter the C main function... */
	call    EXT_C(call_constructors)
	call    EXT_C(_start)
loop:   hlt
	jmp     loop
	.comm   stack, STACK_SIZE
_start_eax: .long 0
_start_ebx: .long 0

/* grub.cfg
 ...timeout=5
...
menuentry 'Mx86' {
	multiboot2 /boot/mx86.elf
	boot
}
*/
