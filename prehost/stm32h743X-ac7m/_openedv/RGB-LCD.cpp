// ASCII CPP-ISO11 TAB4 CRLF
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/LTDC>
#include <cpp/Device/FMC>
#include <c/data.h>
#include "RGB-LCD.hpp"
#include "SDRAM.hpp"
using namespace uni;



// ---- LTDC 引脚：LTDC 信号全部 AF14，背光 PB5 ----
void ltdc_gpio_config() {
	auto spd = GPIOSpeed::Veryhigh;
	// 背光 PB5：推挽输出 + 上拉，点亮
	GPIOB[5].setMode(GPIOMode::OUT_PushPull, spd).setPull(true);
	GPIOB[5] = true;
	// LTDC 数据/同步/时钟引脚（AF14）
	GPIOF[10].setMode(GPIOMode::OUT_AF_PushPull, spd)._set_alternate(14);
	GPIOG[6].setMode(GPIOMode::OUT_AF_PushPull, spd)._set_alternate(14);
	GPIOG[7].setMode(GPIOMode::OUT_AF_PushPull, spd)._set_alternate(14);
	GPIOG[11].setMode(GPIOMode::OUT_AF_PushPull, spd)._set_alternate(14);
	for (byte i = 9; i <= 15; i++)
		GPIOH[i].setMode(GPIOMode::OUT_AF_PushPull, spd)._set_alternate(14);
	static const byte PI_LTDC[] = { 0, 1, 2, 4, 5, 6, 7, 9, 10 };
	for0a(i, PI_LTDC)
		GPIOI[PI_LTDC[i]].setMode(GPIOMode::OUT_AF_PushPull, spd)._set_alternate(14);
}

// ---- LTDC 像素时钟：PLL3.R = 33MHz ----
// HSE=25MHz：VCO = 25/5*160 = 800MHz，R = 800/24 ≈ 33.3MHz
void ltdc_clock_config() {
	RCC[RCCReg::CR].setof(28, false);            // PLL3ON = 0
	RCC[RCCReg::PLLCKSELR].maset(20, 6, 5);      // DIVM3 = 5（PLLSRC 已由 RCC.setClock 置为 HSE）
	RCC[RCCReg::PLL3DIVR] = (160U - 1U) | ((2U - 1U) << 9U) | ((2U - 1U) << 16U) | ((24U - 1U) << 24U);// N3 P3 Q3 R3
	RCC[RCCReg::PLLCFGR].maset(10, 2, 2);        // PLL3RGE = 2（输入 4~8MHz：25/5=5MHz）
	RCC[RCCReg::PLLCFGR].rstof(9);               // PLL3VCOSEL = 0（WIDE）
	RCC[RCCReg::PLLCFGR].rstof(8);               // PLL3FRACEN = 0（整数模式）
	RCC[RCCReg::PLLCFGR].setof(22);              // DIVP3EN
	RCC[RCCReg::PLLCFGR].setof(23);              // DIVQ3EN
	RCC[RCCReg::PLLCFGR].setof(24);              // DIVR3EN
	RCC[RCCReg::CR].setof(28, true);             // PLL3ON = 1
	while (!RCC[RCCReg::CR].bitof(29));          // 等待 PLL3RDY
}

// 文本绘制：VideoControlInterface 的用户实现（用内置 16x8 点阵字体）
void LTDC_LAYER_t::DrawFont(const Point& disp, const DisplayFont& font, const String& str) const {
	const char* p = str.reference();
	stduint x = disp.x, y = disp.y;
	Color fg = font.forecolor;
	while (*p) {
		char ch = *p++;
		if (ch < 32 || ch > 126) ch = '?';
		ch -= 0x20;
		const uint16* datptr = (const uint16*)&_BITFONT_ASCII_16x8[(byte)ch];
		for (byte col = 0; col < 8; col++) {
			uint16 dat = datptr[col];
			for (byte row = 0; row < 16; row++) {
				if (dat & _IMM1S(row)) DrawPoint(Point(x + col, y + (row ^ 0b111)), fg);
			}
		}
		x += 8;
	}
}


