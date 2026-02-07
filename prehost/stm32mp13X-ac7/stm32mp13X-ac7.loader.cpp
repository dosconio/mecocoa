// UTF-8 CPP TAB4 CRLF
// @ArinaMgk

// init hardwares and load kernel to DDR from SD card
// stm32mp13X-ac7.loader <=> U-Boot SPL
// stm32mp13X-ac7        <=> U-Boot Body and Linux kernel
// [hardwares]
// - LTDC
// - SDMMC
// - DDR
/*
#Opt Id		Name	Type	IP		Offset		Binary
P	0x1	fsbl-openbl	Binary	none	0x0			STM32PRGFW_UTIL_MP13xx_CP_Serial_Boot.stm32
P	0x3	fsbl-extfl	Binary	none	0x0			SD_Ext_Loader.bin
P	0x4	fsbl-app	Binary	mmc0	0x0000080	LOADER.stm32
P	0x5	fsbl-app	Binary	mmc0	0x0000500	KERNEL.stm32
 * */

#define BOARD_DETAIL "5DAE7" // STM32MP135-DAE7
#ifndef _STYLE_RUST
#define _STYLE_RUST
#endif
#include <cpp/MCU/ST/STM32MP13>
#include <cpp/string>
#include <c/driver/RCC/RCC-registers.hpp>
#include "openedv-loader/metutor.h"

using namespace uni;

#define setAF(x) setMode(GPIOMode::OUT_AF_PushPull, spd)._set_alternate(x)

bool exist_ddr = true;
bool exist_cache = true;
extern bool useDDR;

static bool init_ddr();
static bool init();

void hand() { LED.Toggle(); }

uint32 Buffer0[512/sizeof(uint32)];

fn main() -> int {
	if (!init()) loop;
	GPIOA[3].enInterrupt();

	Circle circ(Point(200, 200), 200);
	Rectangle scrn_rect(Point(0, 0), Size2(800, 480), Color::AliceBlue);

	LTDC[1].DrawRectangle(scrn_rect);

	SDCard1.Read(0x500, Buffer0);
	VConsole.OutFormat("Magic: 0x%[32H] (0x324D5453)\r\n", Buffer0[0]);
	VConsole.OutFormat("Binln: 0x%[32H]\r\n", Buffer0[0x48/4] - 512);
	VConsole.OutFormat("Entry: 0x%[32H]\r\n", Buffer0[0x50/4]);
	uint32 times = Buffer0[0x48/4] - 512;
	uint32* p = (uint32*)Buffer0[0x50/4];
	times = floorAlign(512, times);// floor eat first sector
	times /= 512;
	for1(i, times) {
		// VConsole.OutFormat("load %u -> %[32H]\r\n", 0x500 + i, p);
		SDCard1.Read(0x500 + i, p);
		cast<stduint>(p) += 512;
	}
	Handler_t kernel = (Handler_t)Buffer0[0x50/4];
	kernel();
	loop {
		LED.Toggle();
		SysDelay(250);
	}
}

static bool init() {
	auto spd = GPIOSpeed::Veryhigh;
	if (!init_specific() || !init_clock()) return false;
	// LED
	LED.setMode(GPIOMode::OUT_PushPull);
	// DDR
	if (exist_ddr && !init_ddr()) return false;
	// LCD
	LCD_BL.setMode(GPIOMode::OUT_PushPull).setPull(true);
	LCD_DE.setAF(11);
	LCD_CLK.setAF(13);
	LCD_HSYNC.setAF(13);
	LCD_VSYNC.setAF(11);
	for0a(i, LCDR) LCDR[i]->setAF(LCDR_AF[i]);
	for0a(i, LCDG) LCDG[i]->setAF(LCDG_AF[i]);
	for0a(i, LCDB) LCDB[i]->setAF(LCDB_AF[i]);
	LCD_BL = true;
	// default polarity D'DP and LTDC_PCPOLARITY_IPC
	auto& hpara = LTDC.refHorizontal(); {
		hpara.active_len = 800;
		hpara.back_porch = 88;
		hpara.front_porch = 40;
		hpara.sync_len = 48;
	}
	auto& vpara = LTDC.refVertical(); {
		vpara.active_len = 480;
		vpara.back_porch = 32;
		vpara.front_porch = 13;
		vpara.sync_len = 3;
	}
	if (LTDC.getFrequency() != 33e6) return false;// 33MHz
	LTDC.setMode(Color::Black);
	{
		LTDC_LAYER_t::LayerPara lpara{};
		LTDC_LAYER_t::layer_param_refer(&lpara);
		asrtret(LTDC[0].setMode(lpara));
	}
	// EXTI
	GPIOA[3].setMode(GPIORupt::Anyedge);// USART2_RX
	GPIOG[10].setMode(GPIOMode::OUT_PushPull);// FDCAN1_TX
	GPIOA[3].setInterrupt(hand);
	GPIOI[0].setMode(GPIOMode::INN);
	GPIOI[2].setMode(GPIOMode::OUT) = 1;
	// SDCard
	asrtret(SDCard1.setMode());

	return true;
}

static bool init_ddr() {
	RCC.CSI.enAble(true);
	RCC.enSyscfg(true);
	MCE.enClock();
	TZC.enClock();
	TZC[TZCReg::GATE_KEEPER] = 0;
	TZC[TZCReg::REG_ID_ACCESSO] = 0xFFFFFFFF;// Allow DDR Region0 R/W  non secure for all IDs
	TZC[TZCReg::REG_ATTRIBUTESO] = 0xC0000001;
	TZC[TZCReg::GATE_KEEPER] |= 1;// Enable the access in secure Mode. filter 0 request close
	// Enable ETZPC & BACKUP SRAM for security
	ETZPC.enClock();
	BKPSRAM.enClock();
	BSEC[BSECReg::BSEC_DENABLE] = 0x47f;
	if (!DDR.setMode(DDRType::DDR3, DDR_t::DDRSize_256M)) return false;
	// Check DDR Write/Read
	{
		Letvar(tmpp, uint32*, 0xC0000000);
		tmpp[0] = 0x55AAAA55;
		return *tmpp == 0x55AAAA55;
	}
}

bool init_clock() {
	using namespace RCCReg;
	RCC.Sysclock.getCoreFrequency();
	SysTick::enClock(1000);
	RCC.canMode();
	PWR.setDBP(true);
	// AKA __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);
	RCC[BDCR].maset(_IMM(_BDCR::LSEDRV), 2, 0b10);
	// OSC PART:  Order: HSE HSI CSI LSI LSE PLL
	uni::RCC.HSE.setMode(false);// no-bypass
	uni::RCC.HSI.setMode(0, 0);// DIV1 and Cali(0x0)
	uni::RCC.CSI.setMode(0x10);
	uni::RCC.LSI.setMode();
	uni::RCC.LSE.setMode(false);// no-bypass

	if (!RCC.PLL1.setModePLL12(PLLMode::Fractional, PLL1Source::HSE,
		.m=3, .n=81, .p=1, .q=2, .r=2, .frac=0x800)) erro();

	if (!RCC.PLL2.setModePLL12(PLLMode::Fractional, PLL2Source::HSE,
		//.m=3, .n=66, .p=2, .q=2, .r=1, .frac=0x1400)) erro();
		//.m=3, .n=50, .p=2, .q=2, .r=1, .frac=0)) erro();// for DDR 400M
		//.m=3, .n=62, .p=2, .q=2, .r=1, .frac=4096)) erro();// for DDR 500M
		.m=3, .n=66, .p=2, .q=2, .r=1, .frac=5120)) erro();// for 533M

	bool if_range3 = true;
	if (!RCC.PLL3.setModePLL34(PLLMode::Fractional, PLL3Source::HSE, if_range3,
		.m=2, .n=34, .p=2, .q=17, .r=2, .frac=0x1a04)) erro();

	bool if_range4 = true;
	if (!RCC.PLL4.setModePLL34(PLLMode::Fractional, PLL4Source::HSE, if_range4,
		.m=2, .n=38, .p=12, .q=14, .r=6, .frac=4096)) erro();// 33M for LTDC (.Q)
		//.m=2, .n=35, .p=12, .q=14, .r=6, .frac=0)) erro();   // 30M for LTDC

	bool state = true;

	// BUS PART:  Order: MPU ACLK HCLK PCLK4 PCLK5 PCLK1 PCLK2 PCLK3 PCLK6
	asserv(state) = RCC.Sysclock.setMode(SysclkSource::PLL1, 1);// PLL1 DIV2
	else erro();
	asserv(state) = RCC.AXIS.setMode(AxisSource::PLL2, 1);// PLL2 DIV1
	else erro();
	asserv(state) = RCC.MLAHB.setMode(MLAHBSource::PLL3, 0);// PLL3 DIV1
	else erro();
	// APBs
	RCC.APB4.setMode(1);// PCLK4 RCC_APB4_DIV2
	RCC.APB5.setMode(2);// PCLK5 RCC_APB5_DIV4
	RCC.APB1.setMode(1);// PCLK1 RCC_APB1_DIV2
	RCC.APB2.setMode(1);// PCLK2 RCC_APB2_DIV2
	RCC.APB3.setMode(1);// PCLK3 RCC_APB3_DIV2
	RCC.APB6.setMode(1);// PCLK6 RCC_APB6_DIV2
	// {unchk}
	bool compensate = false;
	// Note : The activation of the I/O Compensation Cell is recommended with communication  interfaces
	// (GPIO, SPI, FMC, XSPI ...)  when  operating at [HIGH frequencies] (refer to product datasheet)
	// The I/O Compensation Cell activation  procedure requires :
	// - The activation of the CSI clock
	// - The activation of the SYSCFG clock
	// - Enabling the I/O Compensation Cell : setting bit[0] of register SYSCFG_CCCSR
	// To do this please enable the block
	if (compensate) {
		//{} __HAL_RCC_CSI_ENABLE() ;
		//{} __HAL_RCC_SYSCFG_CLK_ENABLE() ;
		//{} HAL_EnableCompensationCell();
	}
	return true;
}

_ESYM_C void Error_Handler(void) { erro(); }
_ESYM_C void assert_failed(void) { erro(); }

// Global Data
VideoConsole VConsole(&LTDC[1], Rectangle(Point(0, 0), Size2(800,480)));

void LTDC_LAYER_t::DrawFont(const Point& disp, const DisplayFont& font) const {}
void outtxt(const char* str, stduint len) {
	for0(i, len) VConsole.OutChar(str[i]);
}
void erro(const char*) {
	loop{
		LED.Toggle();
		SysDelay(2000);
	}
}
