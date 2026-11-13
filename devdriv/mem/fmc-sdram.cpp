// UTF-8 CPP-ISO11 TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: FMC SDRAM
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x)
#include "../../prehost/stm32h743X-ac7m/unicfg.hpp"
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/FMC>

_ESYM_C void R_FMC_SDRAM_INIT();

__attribute__((section(".init.rmod")))
extern const RMOD_LIST RMOD_LIST_FMC_SDRAM{
	R_FMC_SDRAM_INIT,
	"FMC_SDRAM",
};

// SDRAM initialization: controller setup, clock enable, precharge, auto-refresh, mode register, and refresh rate.
bool sdram_init() {
	if (!ConfigDeviceGPIN(DeviceType::FmcSDRAM)) return false;
	if (!FMC.SDRAM.setMode(BoardConfig::Sdram::ControllerConfig, BoardConfig::Sdram::Timing)) return false;

	SDRAMCommand cmd;
	cmd.target = BoardConfig::Sdram::CommandTarget;
	// 1) Enable the SDRAM clock.
	cmd.mode = SDRAMCmd::ClockConfigEnable;
	cmd.autoRefreshNumber = 1;
	cmd.modeRegister = 0;
	if (!FMC.SDRAM.setCommand(cmd)) return false;
	SysDelay_us(BoardConfig::Sdram::PowerUpDelayUs);
	// 2) Precharge all banks.
	cmd.mode = SDRAMCmd::PALL;
	if (!FMC.SDRAM.setCommand(cmd)) return false;
	// 3) Perform auto-refresh.
	cmd.mode = SDRAMCmd::AutoRefresh;
	cmd.autoRefreshNumber = BoardConfig::Sdram::AutoRefreshCount;
	if (!FMC.SDRAM.setCommand(cmd)) return false;
	// 4) Load the mode register.
	cmd.mode = SDRAMCmd::LoadMode;
	cmd.autoRefreshNumber = 1;
	cmd.modeRegister = BoardConfig::Sdram::ModeRegister;
	if (!FMC.SDRAM.setCommand(cmd)) return false;
	// 5) Program the refresh rate.
	return FMC.SDRAM.setRefreshRate(BoardConfig::Sdram::RefreshRate);
}

void R_FMC_SDRAM_INIT() {
	namespace Sdram = BoardConfig::Sdram;
	constexpr auto register_size = FMC_Bank5_6_R_BASE - FMC_R_BASE +
		(static_cast<unsigned>(FMC_SDRAMReg::SDSR) + 1) * sizeof(uint32);
	auto* fmc = Devsman::RegisterPlatformDevice("fmc", "stm32-fmc", &FMC);
	if (!fmc || !Devsman::AddMemoryResource(fmc, 0, FMC_R_BASE, register_size)) {
		if (fmc) Devsman::UnregisterDevice(fmc);
		plogwarn("[FMC] controller registration failed");
		return;
	}
	fmc->fields.rcc_state = FMC.isAble()
		? DeviceRccState::Enabled : DeviceRccState::Disabled;
	if (fmc->fields.rcc_state == DeviceRccState::Disabled) return;

	const auto name = String::newFormat("sdram@%08x", (unsigned)Sdram::Base);
	auto* ram = Devsman::RegisterPlatformDevice(fmc, name.reference(), "fmc-sdram", &FMC.SDRAM);
	constexpr unsigned bank = Sdram::ControllerConfig.bank == SDRAMBank::Bank1 ? 1 : 2;
	constexpr unsigned width = 8u << static_cast<unsigned>(Sdram::ControllerConfig.dataWidth);
	if (ram &&
		Devsman::AddConfigurationResource(ram, 0, "ram-base", Sdram::Base) &&
		Devsman::AddConfigurationResource(ram, 1, "ram-size-bytes", Sdram::Size) &&
		Devsman::AddConfigurationResource(ram, 2, "bank", bank) &&
		Devsman::AddConfigurationResource(ram, 3, "data-width-bits", width)) return;
	if (ram) Devsman::UnregisterDevice(ram);
	plogwarn("[FMC] SDRAM registration failed");
}


#endif
