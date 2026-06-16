#if (_MCCA & 0xFF00) == 0x8600
#if _MCCA == 0x8632 // ----- x86 Architecture -----
_PACKED(struct) HardwareInterruptFrame {
	// 8 general registers pushed by PUSHAD (32 bytes)
	stduint pusha_edi;
	stduint pusha_esi;
	stduint pusha_ebp;
	stduint pusha_esp;
	stduint pusha_ebx;
	stduint pusha_edx;
	stduint pusha_ecx;
	stduint pusha_eax;
	
	// 2 parameters manually pushed (8 bytes)
	stduint interrupt_id; // Interrupt vector number (Vector)
	stduint error_code;   // Exception error code (0 if none)
	
	// 5 contexts automatically pushed by CPU
	stduint hw_eip;
	stduint hw_cs;
	stduint hw_eflags;
	stduint hw_esp;
	stduint hw_ss;

	// New x86 metadata, replacing old PG_PUSH fields
	stduint cr3;		// interrupted task CR3
	stduint ds;
	stduint es;
	stduint fs;
	stduint gs;
	
	// Internal return metadata.
	// Used by assembly to copy back to the original transition frame.
	stduint transition_frame;
	stduint percore_ptr;
};

#elif _MCCA == 0x8664 // ----- x64 Architecture -----
_PACKED(struct) HardwareInterruptFrame {
	// Pop reversed, push ordered (RAX at high address, R15 at low address)
	uint64 pusha_r15;
	uint64 pusha_r14;
	uint64 pusha_r13;
	uint64 pusha_r12;
	uint64 pusha_r11;
	uint64 pusha_r10;
	uint64 pusha_r9;
	uint64 pusha_r8;
	uint64 pusha_rdi;
	uint64 pusha_rsi;
	uint64 pusha_rbp;
	uint64 pusha_rbx;
	uint64 pusha_rdx;
	uint64 pusha_rcx;
	uint64 pusha_rax;
	
	// 2 parameters manually pushed (16 bytes)
	uint64 interrupt_id;
	uint64 error_code;
	
	// 5 contexts automatically pushed by CPU (40 bytes)
	uint64 hw_rip;
	uint64 hw_cs;
	uint64 hw_rflags;
	uint64 hw_rsp;
	uint64 hw_ss;
};
#endif
#endif
