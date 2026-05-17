#if (_MCCA & 0xFF00) == 0x8600
#if _MCCA == 0x8632 // ----- x86 Architecture -----
_PACKED(struct) HardwareInterruptFrame {
	// 5 registers pushed by PG_PUSH (20 bytes)
	stduint pg_ebp;
	stduint pg_esp_old;
	stduint pg_cr3;
	stduint pg_ss;
	stduint pg_ds;
	
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
};

#elif _MCCA == 0x8664 // ----- x64 Architecture -----
_PACKED(struct) HardwareInterruptFrame {
	// 2 registers pushed by PG_PUSH (16 bytes)
	uint64 pg_rsp;
	uint64 pg_cr3;
	
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
