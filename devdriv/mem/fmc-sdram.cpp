// UTF-8 CPP-ISO11 TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: FMC SDRAM
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x)
#include "../../prehost/stm32h743X-ac7m/_openedv/unicfg.hpp"
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/FMC>

_ESYM_C void R_FMC_SDRAM_INIT();

__attribute__((section(".init.rmod")))
extern const RMOD_LIST RMOD_LIST_FMC_SDRAM{
	R_FMC_SDRAM_INIT,
	"FMC_SDRAM",
};

// SDRAM initialization: controller setup, clock enable, precharge, auto-refresh, mode register, and refresh rate.
void sdram_init() {
	if (!ConfigDeviceGPIN(DeviceType::FmcSDRAM)) return;

	FMC.SDRAM.setMode(BoardConfig::Sdram::ControllerConfig, BoardConfig::Sdram::Timing);

	SDRAMCommand cmd;
	cmd.target = BoardConfig::Sdram::CommandTarget;
	// 1) Enable the SDRAM clock.
	cmd.mode = SDRAMCmd::ClockConfigEnable;
	cmd.autoRefreshNumber = 1;
	cmd.modeRegister = 0;
	FMC.SDRAM.setCommand(cmd);
	SysDelay_us(BoardConfig::Sdram::PowerUpDelayUs);
	// 2) Precharge all banks.
	cmd.mode = SDRAMCmd::PALL;
	FMC.SDRAM.setCommand(cmd);
	// 3) Perform auto-refresh.
	cmd.mode = SDRAMCmd::AutoRefresh;
	cmd.autoRefreshNumber = BoardConfig::Sdram::AutoRefreshCount;
	FMC.SDRAM.setCommand(cmd);
	// 4) Load the mode register.
	cmd.mode = SDRAMCmd::LoadMode;
	cmd.autoRefreshNumber = 1;
	cmd.modeRegister = BoardConfig::Sdram::ModeRegister;
	FMC.SDRAM.setCommand(cmd);
	// 5) Program the refresh rate.
	FMC.SDRAM.setRefreshRate(BoardConfig::Sdram::RefreshRate);
}

void R_FMC_SDRAM_INIT() {
	
}


#endif
