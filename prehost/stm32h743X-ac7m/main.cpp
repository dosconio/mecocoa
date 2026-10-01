#include "../../include/mecocoa.hpp"
#include <cpp/MCU/ST/STM32H7>
#include "../../depends/desktop.hpp"
#include "_openedv/RGB-LCD.hpp"

extern "C" char _IDN_BOARD[16] {"STM32H743IIT6"};

_ESYM_C void mecocoa();
extern OstreamTrait* con0_out;

void _idle() {
	GPIN& LEDB = GPIOB[ 0];
	LEDB.setMode(GPIOMode::OUT);
	while (true) {
		LEDB.Toggle();
		for(volatile unsigned i{0}; i < 1000000; i++){}
	}
}

int main()
{
	con0_out = &XART1;
	if (!RCC.setClock(SysclkSource::HSE)) erro();
	XART1.setMode(115200);
	mecocoa();
	
	mempool0.dump_available();
	
	_idle();
	erro();
}

void erro(const char* str) {
	GPIN& LEDR = GPIOB[ 1];
	LEDR.setMode(GPIOMode::OUT);
	while (true) {
		LEDR.Toggle();
		for(volatile unsigned i{0}; i < 1000000; i++){}
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

ThreadBlock* PCU_CORES_current_thread[PCU_CORES_MAX];

stduint Taskman::getID() { return _TEMP 0; } // H743 only

void ThreadBlock::Block(BlockReason reason) {}
void ThreadBlock::Unblock(BlockReason reason) {}

void Taskman::SleepAndRelease(Spinlock* lk) {}
	
SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;
	
Dnode* VTTY_Append(Console_t* con) {return 0;}
ProcessBlock* ProcessBlock::AcquireActiveByPID(stduint pid) {return 0;}
bool Devsman::RegisterDriverStarter(const char* driver_name, DriverStartRoutine starter){return false;}
void ProcessBlock::Release(ProcessBlock* pb) {}
Mutex console_waiters_mutex;
Dchain vttys = { 0 };
Spinlock scheduler_lock;
Dchain Taskman::chain = {nullptr};
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule()) { }
extern "C" stduint sys_kill(stduint pid, int sig, stduint tid){return 0;}
DeviceNode* Devsman::Root(){return 0;}

byte FILE_ENDO, FILE_ENTO;
	