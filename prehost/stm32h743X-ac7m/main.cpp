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

// The FP state (S0-S15) sits below the basic frame when the exception entry stacked it, and bit4 of the EXC_RETURN does not report that here. Take the base whose xPSR is a real one.
_ESYM_C stduint* _arm_frame(stduint lr, stduint sp) {
	stduint* base = (stduint*)sp;
	stduint* alt = (stduint*)(sp + 0x48);
	auto ok = [](stduint* f) {
		const stduint xpsr = f[7];
		if (!(xpsr & 0x01000000)) return false;// the Thumb bit must be set
		return (xpsr & 0x1FF) <= 0xF0;// a thread-mode or regular exception frame
	};
	if (ok(base)) return base;
	if (ok(alt)) return alt;
	return (lr & 0x10) ? alt : base;
}

// A fault must report itself: no debugger available.
static void _fault_report(rostr tag, stduint lr, stduint psp, stduint msp) {
	stduint* frame = _arm_frame(lr, (lr & 0x8) ? psp : msp);// bit3: the frame is on the PSP
	XART1.OutFormat("FAULT %s cfsr=%08X hfsr=%08X bfar=%08X mmfar=%08X\r\n",
		tag, SCB->CFSR, SCB->HFSR, SCB->BFAR, SCB->MMFAR);
	XART1.OutFormat("  pc=%08X lr=%08X xpsr=%08X psp=%08X msp=%08X exc=%08X\r\n",
		frame[6], frame[5], frame[7], psp, msp, lr);
	XART1.OutFormat("  r0=%08X r1=%08X r2=%08X r3=%08X r12=%08X\r\n",
		frame[0], frame[1], frame[2], frame[3], frame[4]);
	// a fault inside a loaded image is turned into an exit syscall, so only that task dies
	if (lr & 0x8) {
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
_ESYM_C stduint _svc_return_to_user(stduint result) {
	auto* tb = Taskman::CurrentTB();
	if (!tb) return 0xFFFFFFFD;
	auto& c = tb->context;
	stduint* saved = (stduint*)((byte*)&c + offsetof(NormalTaskContext, SP_fiq));// free on M-profile
	const stduint frame_size = (_IMM(c.LR_irq) & 1) ? 0x68 : 0x20;// S0-S15 sit below the basic frame
	stduint* frame = (stduint*)(_IMM(c.SP_irq) + frame_size - 0x20);// the caller SP, less the basic frame
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

static stduint _user_sd_tries = 0;

static void _user_load_from_sd(void*, ...) {
	vfs_dentry* d = Filesys::Index("/mnt/sd0.0/testpie");
	if (!d || !d->d_inode) {
		if (_user_sd_tries++ == 10) {
			XART1.OutFormat("USER no elf on /mnt/sd0.0, still waiting\r\n");
		}
		Systimex::AppendDeferredCallback(CONFIG_SysTickFreq, 0, (_tocall_ft)_user_load_from_sd);
		return;
	}
	stduint size = d->d_inode->i_size;
	ProcessBlock* upb = Taskman::CreateFile("/mnt/sd0.0/testpie", RING_U, Task_Init);
	XART1.OutFormat("USER sd size=%u pb=%08X ccr=%08X\r\n",
		(unsigned)size, (unsigned)_IMM(upb), (unsigned)SCB->CCR);// CCR bit16: D-Cache, bit17: I-Cache
	if (!upb) erro("USER load fail");
	{// give the user process its console and the three standard fds
		auto focus_tty = upb->focus_tty.Lock();
		*focus_tty = vttys.Root();
		if (*focus_tty) {
			upb->Open("/dev/tty", O_RDWR);
			upb->Open("/dev/tty", O_RDWR);
			upb->Open("/dev/tty", O_RDWR);
		}
		else plogerro("[USER] no virtual console to attach");
	}
}

alignas(8) static byte _boot_stack[0x8000];
bool inited = false;
int main()
{
	con0_out = &XART1;
	if (!RCC.setClock(SysclkSource::HSE)) erro();
	XART1.setMode(115200);
	SysTick::enClock(CONFIG_SysTickFreq);
	SCB->FPCCR = SCB->FPCCR & ~(3u << 30);// FPCCR: ASPEN/LSPEN off, the FP state is stacked on every exception
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
	// M2: the user program is read from the card, the embedded image stays as a fallback
	Systimex::AppendDeferredCallback(CONFIG_SysTickFreq, 0, (_tocall_ft)_user_load_from_sd);
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

class LcdPanelDevice : public VideoDevice {
	LTDC_LAYER_t* layer;
	FramebufferInfo fb_info;
public:
	LcdPanelDevice(LTDC_LAYER_t& lyr, const FramebufferInfo& info) : layer(&lyr), fb_info(info) {}

	virtual const FramebufferInfo& GetFramebuffer() const override { return fb_info; }

	virtual void SetCursor(const Point& disp) const override { layer->SetCursor(disp); }
	virtual Point GetCursor() const override { return layer->GetCursor(); }
	virtual void DrawPoint(const Point& disp, Color color) const override { layer->DrawPoint(disp, color); }
	virtual void DrawRectangle(const Rectangle& rect) const override { layer->DrawRectangle(rect); }
	virtual void DrawFont(const Point& disp, const DisplayFont& font, const String& str) const override { layer->DrawFont(disp, font, str); }
	virtual Color GetColor(Point p) const override { return layer->GetColor(p); }
};

void ltdc_gpio_config();
void ltdc_clock_config();
VideoDevice* InitClassicVideo(const uni::FramebufferInfo& info);
extern byte _BUF_cursor[byteof(Cursor)];
#define LCD_FRAME_BUF_ADDR FMC_SDRAM_BANK1_BASE
bool Consman::Initialize() {
	new (&message_queue_conv) SpinlockBlock<uni::Queue<SysMessage>>(64);
	// register the serial port as the first virtual TTY for /dev/tty
	VTTY_Append(&XART1);
	//
	bool state = false;
	// ltdc_init();
	// 800x480：hsw48 hbp88 hfp40 / vsw3 vbp32 vfp13
	ltdc_gpio_config();
	ltdc_clock_config();
	auto& h = LTDC.refHorizontal();
	h.sync_len = 48; h.back_porch = 88; h.active_len = 800; h.front_porch = 40;
	auto& v = LTDC.refVertical();
	v.sync_len = 3; v.back_porch = 32; v.active_len = 480; v.front_porch = 13;
	LTDC.setMode(Color::Black);
	LTDC_LAYER_t::LayerPara lpara{};
	LTDC_LAYER_t::layer_param_refer(&lpara);
	lpara.roleaddr = (pureptr_t)LCD_FRAME_BUF_ADDR;
	LTDC[1].setMode(lpara);
	Consman::ento_gui = _TEMP true;
	
	const auto screen_size = Size2(h.active_len, v.active_len);
	const Rectangle screen0_win{ Point(0,0), screen_size, Color::Black };
	// TEMP BEGIN
	sys_framebuffer.physical_range = Slice{ (stduint)LCD_FRAME_BUF_ADDR, (stduint)h.active_len * v.active_len * 2 };
	sys_framebuffer.screen_size = screen_size;
	sys_framebuffer.pitch = h.active_len * 2;
	sys_framebuffer.bpp = 16;
	sys_framebuffer.format = PixelFormat::RGB565;
	//{} R_CLASSIC_VIDEO_INIT();
	// Register 'classic-video' and let its starter convert sys_framebuffer into a VideoDevice.
	//{}DeviceNode* fb_node = Devsman::RegisterPlatformDevice("video-classic", "classic-video-driver", &sys_framebuffer);
	//{}VideoDevice* screen = fb_node && fb_node->fields.binding.driver_data ?
	//{}	static_cast<VideoDevice*>(fb_node->fields.binding.driver_data) : nullptr;
	VideoDevice* screen = new LcdPanelDevice(LTDC[1], sys_framebuffer);
	if (!screen) {
		erro("!screen");
	}
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
//DeviceNode* Devsman::Root(){return 0;}

void CleanupPwcallThreadInterrupts(stduint tid){}
void CleanupPwcallProcessHandles(stduint pid) {}
extern "C" stduint sys_sigaction(int, const struct _POSIX_sigaction*, struct _POSIX_sigaction*){return ~_IMM0;}
void sysinfo_classic(OstreamTrait& com1, byte func) {}

void Coreman::Initialize() {}
bool Virtman::Initialize() { return false; }


byte FILE_ENDO, FILE_ENTO;
	