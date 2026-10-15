#include "../../include/mecocoa.hpp"
#include <c/prochip/CortexM7.h>
#include <cpp/MCU/ST/STM32H7>
#include "../../depends/desktop.hpp"
#include "_openedv/RGB-LCD.hpp"

/*
make -f accmlib/accma32.clang.make all
make -f subapps/Makefile.clang.a32 all
*/

extern "C" char _IDN_BOARD[16]{ "STM32H743IIT6" };

_ESYM_C void mecocoa();

void _idle() {
	GPIN& LEDB = GPIOB[ 0];
	LEDB.setMode(GPIOMode::OUT);
	while (true) {
		// LEDB.Toggle();
		SysDelay_ms(2000, true);
	}
}

void _test() {
	GPIN& LEDR = GPIOB[ 1];
	LEDR.setMode(GPIOMode::OUT);
	while (true) {
		LEDR.Toggle();
		auto th = Taskman::CurrentTB();
		bool state_rupt = IC.TryMaskInterrupt();
		th->Block(ThreadBlock::BlockReason::BR_Resting);
		Systimex::AppendThreadWake(100, th->tid);// 100 ticks = 1s @ CONFIG_SysTickFreq
		if (state_rupt) IC.enInterrupt(true);
		Taskman::Schedule(true);
	}
}


void key_isr_up() { erro(); }

// FPCCR.ASPEN=0: the hardware never stacks S0-S15, the frame is the plain 8 words.
_ESYM_C stduint* _arm_frame(stduint lr, stduint sp) {
	return (stduint*)sp;
}

_ESYM_C void MemSetTrapReport(void* d, int c, size_t n, int which) {
	stduint ra = 0;
	_ASM volatile("mov %0, lr" : "=r"(ra));
	XART1.OutFormat("MTRAP[%d] d=%08X c=%02X n=%u lr=%08X\r\n", which, (unsigned)d, (unsigned)(c & 0xFF), (unsigned)n, (unsigned)ra);
}

static bool _fault_reported = false;// a fault inside the reporting path itself cannot be resumed

// A fault must report itself: no debugger available.
static void _fault_report(rostr tag, stduint lr, stduint psp, stduint msp) {
	if (_fault_reported) erro();// a second fault means the first one could not be resumed
	_fault_reported = true;
	stduint* frame = _arm_frame(lr, (lr & 0x4) ? psp : msp);// bit2: the frame is on the PSP
	XART1.OutFormat("FAULT %s cfsr=%08X hfsr=%08X bfar=%08X mmfar=%08X\r\n",
		tag, SCB->CFSR, SCB->HFSR, SCB->BFAR, SCB->MMFAR);
	XART1.OutFormat("  pc=%08X lr=%08X xpsr=%08X psp=%08X msp=%08X exc=%08X\r\n",
		frame[6], frame[5], frame[7], psp, msp, lr);
	XART1.OutFormat("  r0=%08X r1=%08X r2=%08X r3=%08X r12=%08X\r\n",
		frame[0], frame[1], frame[2], frame[3], frame[4]);
	XART1.OutFormat("  shcsr=%08X icsr=%08X\r\n", (unsigned)SCB->SHCSR, (unsigned)SCB->ICSR);
	{// the thread the fault belongs to: reading stack buffers here would fault again, the guard region covers the canary
		auto* tb = Taskman::CurrentTB();
		if (tb) {
			XART1.OutFormat("  ctx sp=%08X svc=%08X exc=%08X tid=%u tb=%08X\r\n",
				(unsigned)_IMM(tb->context.sp), (unsigned)_IMM(tb->context.SP_svc),
				(unsigned)_IMM(tb->context.exc_return), (unsigned)tb->tid, (unsigned)_IMM(tb));
			XART1.OutFormat("  stk line=%08X levl=%08X size=%u\r\n",
				(unsigned)_IMM(tb->stack_lineaddr), (unsigned)_IMM(tb->stack_levladdr),
				(unsigned)tb->stack_size);
		}
	}
	if (SCB->CFSR & ((1u << 3) | (1u << 4) | (1u << 12))) {
		for (stduint i = 0; i < 8; i++) {// which region denied the access: dump the whole table
			MPU->RNR = i;
			__DSB();// RBAR/RASR read back the region selected by RNR: let the RNR write land first
			XART1.OutFormat("  mpu%u base=%08X attr=%08X\r\n", (unsigned)i, (unsigned)(MPU->RBAR & ~0x1Fu), (unsigned)MPU->RASR);
		}
		erro();// MSTKERR/MUNSTKERR/BFSTKERR: nothing was stacked, nothing to resume
	}
	// a fault inside a loaded image is turned into an exit syscall, so only that task dies
	if (lr & 0x4) {
		auto pb = Taskman::CurrentPB();
		if (pb && pb->load_slices[0].length) {
			frame[0] = _IMM(syscall_t::EXIT);// r0: call id
			frame[1] = 128 + SIGSEGV;// r1: exit code
			frame[2] = frame[3] = 0;
			frame[6] = (pb->load_slices[0].address + pb->load_slices[0].length - 8) | 1;// PC: the stub at the end of the image block
			return;
		}
	}
	erro();
}
#define _FAULT_HANDLER(name, tag) \
	_ESYM_C void name() { \
		stduint lr, psp, msp; \
		_ASM volatile("mov %0, lr \n mrs %1, psp \n mrs %2, msp" : "=r"(lr), "=r"(psp), "=r"(msp)); \
		_fault_report(tag, lr, psp, msp); \
	}
_FAULT_HANDLER(HardFault_Handler, "HardFault")
_FAULT_HANDLER(MemManage_Handler, "MemManage")
_FAULT_HANDLER(BusFault_Handler, "BusFault")
_FAULT_HANDLER(UsageFault_Handler, "UsageFault")

// The weak handlers of startup.S land here: name the exception instead of spinning silently.
_ESYM_C void _default_report(stduint lr, stduint ipsr) {
	stduint psp, msp;
	_ASM volatile("mrs %0, psp \n mrs %1, msp" : "=r"(psp), "=r"(msp));
	stduint exc = ipsr & 0x1FF;
	XART1.OutFormat("DEFAULT exc=%u irq=%d\r\n", exc, (int)exc - 16);
	_fault_report("Default", lr, psp, msp);
}

// Nonzero on a syscall return: the kernel stack pointer the SVC vector shim rewinds to.
_ESYM_C stduint C_SvcReturn = 0;

// Finish a syscall. The caller frame was saved on the thread while the body ran, so it is
// rebuilt here and the hardware pops it: the result cannot be lost between registers.
// Returns the EXC_RETURN to use.
_ESYM_C stduint _svc_return_to_user(stduint result, stduint psp) {
	auto* tb = Taskman::CurrentTB();
	if (!tb) return 0xFFFFFFFD;
	auto& c = tb->context;
	stduint* saved = (stduint*)((byte*)&c + offsetof(NormalTaskContext, SP_fiq));// free on M-profile
	stduint* frame = (stduint*)psp;// the live PSP: the hardware just stacked the trampoline's frame here, so rebuild the caller frame in its place
	frame[0] = result;
	for0(i, 8) if (i) frame[i] = saved[i];
	_ASM volatile("msr psp, %0" :: "r"((stduint)frame));// the hardware unstacks this frame
	C_SvcReturn = _IMM(tb->stack_levladdr) + tb->stack_size;// the shim rewinds the kernel stack after its epilogue
	stduint ctl = 0;
	_ASM volatile("mrs %0, control" : "=r"(ctl));
	const stduint priv = (tb->parent_process && tb->parent_process->ring == RING_U) ? 1u : 0u;
	_ASM volatile("msr control, %0" :: "r"((ctl & ~1u) | priv));// the caller level is applied in handler mode
	_ASM volatile("isb");
	return 0xFFFFFFFD;// a basic frame on the PSP: S0-S15 are scratch under the C ABI
}

// The caller PC to resume at is kept with the saved frame, so no accessor is needed here.

alignas(8) byte _boot_stack[0x8000];
bool inited = false;
int main()
{
	con0_out = &XART1;
	if (!RCC.setClock(SysclkSource::HSE)) erro();
	XART1.setMode(115200);
	SysTick::enClock(CONFIG_SysTickFreq);
	SCB->FPCCR = SCB->FPCCR & ~(1u << 31);// ASPEN=0: the asm, handler.cpp and _arm_frame assume the plain 8-word frame, S0-S15 are caller-saved scratch
	GPIN& KEYU = GPIOA[ 0];// Up
	KEYU.setMode(GPIOMode::IN_Pull).setPull(false);
	KEYU.setMode(GPIORupt::Posedge);
	KEYU.setInterrupt(key_isr_up);
	KEYU.enInterrupt();
	IC.enInterrupt(false);
	_ASM volatile("msr psp, %0" :: "r"((stduint)(_boot_stack + sizeof(_boot_stack))));
	_ASM volatile("mrs r0, control \n orr r0, r0, #2 \n msr control, r0 \n isb" ::: "r0");
	mecocoa();
	inited = true;
	
	Taskman::Create((void*)&_test, RING_M);
	// mempool0.dump_available();
	

	Taskman::Schedule(true);
	
	_idle();
	erro();
}

void erro(const char* str) {
	GPIN& LEDR = GPIOB[ 1];
	LEDR.setMode(GPIOMode::OUT);
	while (true) {
		LEDR.Toggle();
		for(volatile unsigned i{0}; i < 4000000; i++){}
	}
}

// Handler

_ESYM_C void SysTick_Handler();
_ESYM_C void SysTick_Handler_Mcca() {
	SysTick_Handler();
	if (inited) {
		tick++;
		Systimex::CollectExpired();
		Taskman::Schedule();
	}
}

// Graphic

void ltdc_gpio_config();
void ltdc_clock_config();
VideoDevice* InitClassicVideo(const uni::FramebufferInfo& info);
void RgbLcd_ConfigPanel(stduint hsw, stduint hbp, stduint hact, stduint hfp, stduint vsw, stduint vbp, stduint vact, stduint vfp);
VideoDevice* RgbLcd_ScreenInit(pureptr_t fb_addr);
extern byte _BUF_cursor[byteof(Cursor)];
#define LCD_FRAME_BUF_ADDR FMC_SDRAM_BANK1_BASE
bool Consman::Initialize() {
	new (&message_queue_conv) SpinlockBlock<uni::Queue<SysMessage>>(64);
	// register the serial port as the first virtual TTY for /dev/tty
	//
	bool state = false;
	// ltdc_init();
	// 800x480：hsw48 hbp88 hfp40 / vsw3 vbp32 vfp13
	RgbLcd_ConfigPanel(48, 88, 800, 40, 3, 32, 480, 13);
	VideoDevice* screen = RgbLcd_ScreenInit((pureptr_t)LCD_FRAME_BUF_ADDR);
	if (!screen) {
		erro("!screen");
	}
	Consman::ento_gui = _TEMP true;
	
	sys_framebuffer = screen->GetFramebuffer();
	const auto screen_size = sys_framebuffer.screen_size;
	const Rectangle screen0_win{ Point(0,0), screen_size, Color::Black };
	// TEMP BEGIN
	Consman::AdoptVideoDevice(screen);
	
	Cursor::global_cursor = new (_BUF_cursor)Cursor{ &global_layman.Lock()->getVCI() };
	Cursor::global_cursor->setSheet(*global_layman.Lock(), Point{ 300, 200 });
	
	auto desktop = new Desktop(Desktop::kDefaultBgColor);
	global_desktop = desktop;
	desktop->Initialize(*global_layman.Lock(), screen0_win, Desktop::kDefaultBgColor);
	global_layman.Lock()->Append(desktop);
	
	// TEMP single layout
	{
		auto layman = global_layman.Lock();
		layman->AddDirty(screen0_win);
		layman->is_dirty = true;
		layman->UpdateForce(nullptr, screen0_win);
		if (Consman::real_pvci && layman->sheet_buffer) {
			Consman::real_pvci->DrawPoints(screen0_win, layman->sheet_buffer);
		}
	}
	return true;
}

//
// stduint Taskman::getID() { return _TEMP 0; } // H743 only


extern "C" stduint sys_kill(stduint pid, int sig, stduint tid){return 0;}

// Nonzero while an SVCall is handled: such a tick must not switch tasks or re-map the MPU.
_ESYM_C int _in_svc_handler() {
	return (SCB->SHCSR & (1u << 7)) ? 1 : 0;// SVCALLACT
}
//DeviceNode* Devsman::Root(){return 0;}

void CleanupPwcallThreadInterrupts(stduint tid){}
void CleanupPwcallProcessHandles(stduint pid) {}
extern "C" stduint sys_sigaction(int, const struct _POSIX_sigaction*, struct _POSIX_sigaction*){return ~_IMM0;}
void sysinfo_classic(OstreamTrait& com1, byte func) {}

void Coreman::Initialize() {}
bool Virtman::Initialize() { return false; }


byte FILE_ENDO, FILE_ENTO;
	