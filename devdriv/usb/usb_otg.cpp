// ASCII C++ TAB4 LF
// ModuTitle: [Device] USB OTG controller and runtime role switching
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x) && CONFIG_ENABLE_USB
#include <c/prochip/CortexM7.h>
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/NVIC>
#include <cpp/Device/RCC/RCC-STM32H7.hpp>
#include <cpp/Device/USB/USBPeri-Device.hpp>
#include <cpp/Device/USB/USBPeri-Class.hpp>
#include <cpp/Device/USB/USB.hpp>
#include <cpp/Device/USB/OTGHostDevice.hpp>
#if _GUI_ENABLE
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#endif
#include <c/driver/_predefine/predef.usbperi.hpp>
#include "../../prehost/stm32h743X-ac7m/unicfg.hpp"

namespace Devs {
	void prepare_usb_msc_disconnect(uni::device::SpaceUSB::USBHostDevice& device);
}

namespace {
	namespace Board = BoardConfig::Usb::Otg2;
	using namespace uni::device::SpaceUSB;

	constexpr uint32 kController = 2;
	constexpr uint32 kTransferStopTimeoutMs = 100;
	constexpr uint32 kEventHandle = _OTG2_FS_ADDR;
	constexpr byte kUsb33EnableBit = 24, kUsb33ReadyBit = 26;
	constexpr byte kHsi48EnableBit = 12, kHsi48ReadyBit = 13;
	enum class SwitchPhase { Idle, DisconnectWait, HostPowerWait };
	enum class HostPhase { Idle, Debounce, ResetHold, ResetEnable, Recovery, Enumerating, Ready, RetryWait, Failed };
	static_assert(Board::HostChannels > 0 && Board::HostChannels <= numsof(HCD2.hc), "Invalid OTG host channel count");

	struct HostCompletion {
		OTGHostDevice* owner = nullptr;
		uint32 generation = 0, submission = 0;
		stduint length = 0, interrupt_mask = 0;
		URBState state = URBState::Idle;
	};

	stduint OtgTicks(stduint ms) {
		return maxof(stduint(1), (ms * CONFIG_SysTickFreq + 999) / 1000);
	}

	void PowerIicDelay() {
		for (volatile unsigned i = 0; i < Board::IicDelayLoops; ++i) {}
	}

	class PowerIic : public uni::IIC_SOFT {
	public:
		using uni::IIC_t::Send;
		PowerIic() : uni::IIC_SOFT(
			uni::GPIO[Board::SdaPort][Board::SdaPin],
			uni::GPIO[Board::SclPort][Board::SclPin], false) {}
		void Initialize() {
			push_pull = true;
			func_delay = PowerIicDelay;
			setMode();
		}
		bool SetPower(bool enabled) {
			SendStart();
			Send(byte((Board::PowerAddress7 << 1) | 1), true);
			if (getError()) { SendStop(); return false; }
			byte value = ReadByte(true, false);
			SendStop();
			if (getError()) return false;
			const byte bit = byte(1u << Board::PowerBit);
			if (enabled == bool(Board::PowerActiveHigh)) value |= bit;
			else value &= byte(~bit);
			SendStart(Board::PowerAddress7);
			if (getError()) { SendStop(); return false; }
			Send(value, true);
			SendStop();
			return !getError();
		}
	};

	// USB 2.0, sections 9.6.1/9.6.3/9.6.5: one vendor interface, EP0 only.
	const byte device_descriptor[] = {
		18, 1, 0x00, 0x02, 0, 0, 0, 64,
		byte(Board::DeviceVendorId), byte(Board::DeviceVendorId >> 8),
		byte(Board::DeviceProductId), byte(Board::DeviceProductId >> 8),
		0x00, 0x01, 1, 2, 0, 1,
	};
	const byte configuration_descriptor[] = {
		9, 2, 18, 0, 1, 1, 0, 0xC0, 1,
		9, 4, 0, 0, 0, 0xFF, 0, 0, 0,
	};
	const byte language_descriptor[] = {4, 3, 0x09, 0x04};

	class EnumerationInterface : public ClassPeripheral {
		byte interface_status[2]{};
	public:
		bool setMode(byte configuration) override { return configuration == 1; }
		bool canMode(byte) override { return true; }
		bool Setup() override {
			const auto& request = Parent()->request;
			if (request.index == 0 && request.value == 0 &&
				request.request_type == (_USB_REQ_TYPE_STANDARD | _USB_REQ_RECIPIENT_INTERFACE | 0x80)) {
				if (request.request == _USB_REQ_GET_INTERFACE && request.length == 1) {
					Parent()->SendControl(interface_status, 1);
					return true;
				}
				if (request.request == _USB_REQ_GET_STATUS && request.length == 2) {
					Parent()->SendControl(interface_status, 2);
					return true;
				}
			}
			if (request.request_type == (_USB_REQ_TYPE_STANDARD | _USB_REQ_RECIPIENT_INTERFACE) &&
				request.request == _USB_REQ_SET_INTERFACE && request.index == 0 &&
				request.value == 0 && request.length == 0) return true;
			Parent()->StallControl();
			return false;
		}
		const byte* getConfigurationDescriptor(uint16& length) override {
			length = sizeof(configuration_descriptor);
			return configuration_descriptor;
		}
		const byte* getOtherSpeedConfigurationDescriptor(uint16& length) override {
			length = 0;
			return nullptr;
		}
		const byte* getDeviceQualifierDescriptor(uint16& length) override {
			length = 0;
			return nullptr;
		}
	};

	struct OtgRuntime {
		PowerIic power;
		PeripheralDevice peripheral;
		EnumerationInterface peripheral_interface;
		PeripheralDescriptor descriptors{};
		byte string_descriptor[64]{};
		UsbRoleStatus status;
		DeviceNode* node = nullptr;
		DeviceNode* power_bus_node = nullptr;
		DeviceNode* power_device_node = nullptr;
		stduint owner_tid;
		UsbRole hardware_role = UsbRole::None;
		SwitchPhase phase = SwitchPhase::Idle;
		bool core_accessible = false;
		volatile bool irq_active = false, port_pending = false, wake_queued = false;
		volatile bool timer_armed = false, timer_due = false;
		volatile uint32 timer_serial = 0, port_changes = 0, disconnects = 0;
		volatile uint32 notification_failures = 0;
		uint32 notified_changes = 0, seen_disconnects = 0, reported_failures = 0;
		PeripheralState notified_device_state = PeripheralState::Default;
		OTGHostDevice* host_device = nullptr;
		bool host_registered = false, host_cancel_active = false;
		UsbTransferStopResult host_cancel_result = UsbTransferStopResult::Stopped;
		uint32 host_cancel_started = 0, host_cancel_mask = 0;
		HostCompletion* completions = nullptr;
		volatile HostPhase host_phase = HostPhase::Idle;
		uint32 host_generation = 0, host_deadline_ms = 0, host_clock_ms = 0;
		uint32 host_last_wall = 0;
		volatile uint16 completion_pending = 0;
		volatile bool host_accepting = false, host_time_pending = false, completion_overflow = false;
		byte host_retries = 0;
		#if _GUI_ENABLE
		byte mouse_buttons = 0, keyboard_modifiers = 0;
		byte keyboard_keys[HIDBootKeyboardReportDecoder::kReportBytes - 2]{};
		#endif
		explicit OtgRuntime(stduint tid) : owner_tid(tid) {
			completions = new HostCompletion[Board::HostChannels];
		}
		~OtgRuntime() { delete[] completions; }
	};

	bool UnregisterPowerTopology(OtgRuntime& runtime) {
		const bool restore_interrupts = IC.TryMaskInterrupt();
		bool ready = true;
		if (runtime.power_device_node) {
			ready = Devsman::UnregisterDevice(runtime.power_device_node);
			if (ready) runtime.power_device_node = nullptr;
		}
		if (ready && runtime.power_bus_node) {
			ready = Devsman::UnregisterBus(runtime.power_bus_node);
			if (ready) runtime.power_bus_node = nullptr;
		}
		if (restore_interrupts) IC.enInterrupt(true);
		return ready;
	}

	bool RegisterPowerTopology(OtgRuntime& runtime) {
		const bool restore_interrupts = IC.TryMaskInterrupt();
		const auto bus_name = String::newFormat("i2c-soft@P%c%u-P%c%u",
			(int)Board::SclPort, (unsigned)Board::SclPin, (int)Board::SdaPort, (unsigned)Board::SdaPin);
		runtime.power_bus_node = Devsman::RegisterBus(DeviceBusType::I2C, DeviceNodeType::I2cBus,
			bus_name.reference(), nullptr, "i2c-soft", static_cast<uni::IIC_SOFT*>(&runtime.power));
		bool ready = runtime.power_bus_node &&
			Devsman::AddPinResource(runtime.power_bus_node, 0, "SCL", Board::SclPort, Board::SclPin) &&
			Devsman::AddPinResource(runtime.power_bus_node, 1, "SDA", Board::SdaPort, Board::SdaPin) &&
			Devsman::AddConfigurationResource(runtime.power_bus_node, 0, "delay-loops", Board::IicDelayLoops);
		if (ready) {
			const auto name = String::newFormat("gpio-expander@%02X", (unsigned)Board::PowerAddress7);
			runtime.power_device_node = Devsman::RegisterDevice(runtime.power_bus_node,
				DeviceNodeType::I2cDevice, DeviceBusType::I2C, name.reference(), "usb-power", &runtime.power);
			ready = runtime.power_device_node &&
				Devsman::AddAddressResource(runtime.power_device_node, 0, Board::PowerAddress7, 7) &&
				Devsman::AddConfigurationResource(runtime.power_device_node, 0, "power-output-bit", Board::PowerBit) &&
				Devsman::AddConfigurationResource(runtime.power_device_node, 1, "power-active-high", Board::PowerActiveHigh);
		}
		if (!ready && !UnregisterPowerTopology(runtime))
			plogerro("[USB OTG2] cannot remove incomplete power topology");
		if (restore_interrupts) IC.enInterrupt(true);
		return ready;
	}

	OtgRuntime* otg = nullptr;

	#if _GUI_ENABLE
	void OtgKeyboardReport(keyboard_event_t event) {
		if (!otg || !otg->host_accepting || otg->status.current != UsbRole::Host) return;
		otg->keyboard_modifiers = event.mod_val;
		if (event.keycode) {
			byte* empty = nullptr;
			bool found = false;
			for (auto& key : otg->keyboard_keys) {
				if (key == event.keycode) {
					if (event.method == keyboard_event_t::method_t::keyup) key = 0;
					found = true;
					break;
				}
				if (!key && !empty) empty = &key;
			}
			if (!found && empty && event.method == keyboard_event_t::method_t::keydown)
				*empty = event.keycode;
		}
		hand_kboard(event);
	}

	void OtgMouseReport(MouseGeneralMessage message) {
		if (!otg || !otg->host_accepting || otg->status.current != UsbRole::Host) return;
		otg->mouse_buttons = byte((message.btn_left ? 1u : 0u) |
			(message.btn_right ? 2u : 0u) | (message.btn_middle ? 4u : 0u));
		hand_mouse_from_device(message, otg);
	}
	#endif

	void NotifyWork(OtgRuntime& runtime) {
		if (runtime.wake_queued) return;
		DeviceEvent event{};
		event.kind = _IMM(DeviceEventKind::Interrupt);
		event.device_handle = kEventHandle;
		event.source = _IMM(IRQ_OTG_FS);
		event.generation = runtime.status.generation;
		event.count = 1;
		event.sequence = runtime.port_changes;
		if (device_event_proc(runtime.owner_tid, event)) runtime.wake_queued = true;
		else ++runtime.notification_failures;
	}

	void HostConnected() {
		if (otg && otg->irq_active) ++otg->port_changes;
	}

	void HostDisconnected() {
		if (!otg || !otg->irq_active) return;
		++otg->disconnects;
		++otg->port_changes;
	}

	void HostSof() {
		if (!otg || !otg->irq_active || otg->status.current != UsbRole::Host) return;
		if (otg->host_phase != HostPhase::Idle && otg->host_phase != HostPhase::Failed)
			otg->host_time_pending = true;
	}

	void HostUrbCompleted(pureptr_t channel, ...) {
		auto* runtime = otg;
		const stduint ch = stduint(channel);
		if (!runtime || !runtime->host_accepting || ch >= Board::HostChannels) return;
		const uint16 bit = uint16(1u << ch);
		if (runtime->completion_pending & bit) {
			runtime->completion_overflow = true;
			return;
		}
		auto& completion = runtime->completions[ch];
		completion.owner = OTGHostDevice::OwnerOfChannel(byte(ch));
		if (!completion.owner) return;
		completion.generation = runtime->host_generation;
		completion.submission = completion.owner->getControlSubmission();
		if (completion.owner->IsControlChannel(byte(ch))) completion.owner->DeferControlCompletion();
		completion.length = HCD2.getXferCount(byte(ch));
		completion.state = HCD2.getURBState(byte(ch));
		completion.interrupt_mask = HCD2.ChannelReg(byte(ch), 0x00C);
		// Keep a halted channel's result stable until the service consumes it.
		HCD2.ChannelReg(byte(ch), 0x00C) = 0;
		runtime->completion_pending |= bit;
	}

	bool DeviceActive() {
		return otg && otg->irq_active && otg->status.current == UsbRole::Device;
	}

	void DeviceSetup() {
		if (DeviceActive()) otg->peripheral.HandleSetup();
	}
	void DeviceReset() {
		if (!DeviceActive()) return;
		otg->peripheral.dev_address = 0;
		otg->peripheral.dev_config = 0;
		otg->peripheral.dev_remote_wakeup = false;
		otg->peripheral.HandleReset();
		otg->port_pending = true;
	}
	void DeviceSuspend() { if (DeviceActive()) otg->peripheral.HandleSuspend(); }
	void DeviceResume() { if (DeviceActive()) otg->peripheral.HandleResume(); }
	void DeviceConnected() { if (DeviceActive()) otg->peripheral.HandleConnect(); }
	void DeviceDisconnected() {
		if (!DeviceActive()) return;
		otg->peripheral.HandleDisconnect();
		otg->port_pending = true;
	}
	void DeviceDataOut(void* endpoint, ...) {
		if (DeviceActive()) otg->peripheral.HandleDataOut(byte(stduint(endpoint)));
	}
	void DeviceDataIn(void* endpoint, ...) {
		if (DeviceActive()) otg->peripheral.HandleDataInn(byte(stduint(endpoint)));
	}

	// interrupt_usb.hpp calls this after HCD/PCD has acknowledged the hardware.
	void OtgInterruptComplete() {
		auto* runtime = otg;
		if (!runtime || !runtime->irq_active) return;
		if (runtime->status.current == UsbRole::Host) {
			if (runtime->port_changes != runtime->notified_changes) {
				runtime->notified_changes = runtime->port_changes;
				runtime->port_pending = true;
				// Keep disconnect notifications enabled after the HCD connect callback.
				HCD2.GlobalReg(OTGGlobalReg::GINTMSK).setof(USB_OTG_GINTSTS_DISCINT_Pos);
			}
		}
		else if (runtime->notified_device_state != runtime->peripheral.getState()) {
			runtime->notified_device_state = runtime->peripheral.getState();
			runtime->port_pending = true;
		}
		// RX FIFO processing follows HCINT in HCD::HandleIRQ; capture its final byte count here.
		for (byte ch = 0; ch < Board::HostChannels; ++ch) {
			if (runtime->completion_pending & uint16(1u << ch))
				runtime->completions[ch].length = HCD2.getXferCount(ch);
		}
		if (runtime->port_pending || runtime->timer_due || runtime->completion_pending ||
			runtime->host_time_pending || runtime->completion_overflow) NotifyWork(*runtime);
	}

	void TimerReady(pureptr_t, stduint serial) {
		const bool restore = IC.TryMaskInterrupt();
		if (otg && otg->timer_armed && otg->timer_serial == serial) {
			otg->timer_due = true;
			NotifyWork(*otg);
		}
		if (restore) IC.enInterrupt(true);
	}

	void CancelTimer(OtgRuntime& runtime) {
		const bool restore = IC.TryMaskInterrupt();
		const uint32 serial = runtime.timer_serial;
		const bool armed = runtime.timer_armed;
		runtime.timer_armed = runtime.timer_due = false;
		if (!++runtime.timer_serial) ++runtime.timer_serial;
		if (armed) Systimex::CancelDeferredCallback(serial, (_tocall_ft)TimerReady);
		if (restore) IC.enInterrupt(true);
	}

	bool ArmTimer(OtgRuntime& runtime, stduint milliseconds) {
		CancelTimer(runtime);
		const bool restore = IC.TryMaskInterrupt();
		runtime.timer_armed = true;
		const bool armed = Systimex::AppendDeferredCallback(
			OtgTicks(milliseconds), runtime.timer_serial, (_tocall_ft)TimerReady);
		if (!armed) runtime.timer_armed = false;
		if (restore) IC.enInterrupt(true);
		return armed;
	}

	void MaskController(OtgRuntime& runtime) {
		HCD2.enInterrupt(false);
		if (runtime.core_accessible)
			HCD2.GlobalReg(OTGGlobalReg::GAHBCFG).rstof(USB_OTG_GAHBCFG_GINT_Pos);
		__DSB();
		NVIC.Reflect(uni::NVICReg::ICPR, _IMM(IRQ_OTG_FS) >> 5) =
			uint32(1) << (_IMM(IRQ_OTG_FS) & 31);
	}

	USBHostControllerIdentity HostControllerIdentity(const OtgRuntime& runtime) {
		return {runtime.node->fields.binding.driver_name,
			runtime.node->fields.binding.driver_data, 0x00u};
	}

	USBHostDeviceLocation HostDeviceLocation(const OTGHostDevice& device) {
		return {nullptr, device.DeviceAddress(), 1, 1};
	}

	bool DropHostDevice(OtgRuntime& runtime, bool restart = true) {
		if (runtime.host_device && (runtime.host_cancel_active ||
			runtime.host_device->FindClassDriver(ClassDriverType::MassStorage))) {
			Devs::prepare_usb_msc_disconnect(*runtime.host_device);
			if (runtime.host_cancel_active && runtime.host_cancel_result == UsbTransferStopResult::Failed) {
				runtime.host_cancel_result = UsbTransferStopResult::Pending;
				runtime.host_cancel_started = uint32(SysTick::getTick()) - kTransferStopTimeoutMs;
			}
			UsbTransferStopResult stopped;
			do {
				stopped = Devsman::StopOTGTransfers(runtime.host_device);
				if (stopped != UsbTransferStopResult::Pending) break;
				// Let the hardware halt while the task sleeps; no transfer completion is dispatched here.
				if (syscall(syscall_t::REST, 1, 1) != 0) {
					runtime.status.state = UsbRoleState::Fault;
					runtime.status.error = UsbRoleError::Notification;
					runtime.irq_active = false;
					MaskController(runtime);
					return false;
				}
			} while (true);
			if (stopped != UsbTransferStopResult::Stopped) return false;
		}
		const bool restore_mask = runtime.host_cancel_active;
		const bool restore = IC.TryMaskInterrupt();
		runtime.host_accepting = false;
		HCD2.enInterrupt(false);
		auto* device = runtime.host_device;
		runtime.host_device = nullptr;
		runtime.completion_pending = 0;
		runtime.completion_overflow = false;
		if (!++runtime.host_generation) ++runtime.host_generation;
		runtime.host_phase = HostPhase::Idle;
		runtime.host_cancel_active = false;
		if (restore) IC.enInterrupt(true);
		#if _GUI_ENABLE
		if (runtime.mouse_buttons) {
			MouseGeneralMessage released{};
			runtime.mouse_buttons = 0;
			hand_mouse_from_device(released, &runtime);
		}
		keyboard_event_t released{};
		if (runtime.keyboard_modifiers) {
			runtime.keyboard_modifiers = 0;
			hand_kboard(released);
		}
		released.method = keyboard_event_t::method_t::keyup;
		for (auto& key : runtime.keyboard_keys) {
			if (!key) continue;
			released.keycode = key;
			key = 0;
			hand_kboard(released);
		}
		#endif
		if (device) {
			if (!restore_mask) {
				HCD2.Stop();
				OTG::FlushRxFifo(_OTG2_FS_ADDR);
				OTG::FlushTxFifo(_OTG2_FS_ADDR, 0x10);
			}
			if (runtime.host_registered) {
				runtime.host_registered = false;
				if (g_host_device_disconnected_hook)
					g_host_device_disconnected_hook(HostControllerIdentity(runtime),
						HostDeviceLocation(*device), *device);
			}
			delete device;
			for (byte ch = 0; ch < Board::HostChannels; ++ch) {
				HCD2.hc[ch].xfer_buff = nullptr;
				HCD2.hc[ch].xfer_count = 0;
				HCD2.ChannelReg(ch, 0x00C) = 0;
				HCD2.ChannelReg(ch, 0x008) = 0xFFFFFFFFu;
			}
		}
		if (restart && runtime.irq_active && runtime.status.current == UsbRole::Host) {
			if (restore_mask) HCD2.GlobalReg(OTGGlobalReg::GINTMSK) = runtime.host_cancel_mask;
			if (device) HCD2.Start();
			__DSB();
			HCD2.enInterrupt();
		}
		return true;
	}

	bool StopController(OtgRuntime& runtime) {
		if (runtime.hardware_role == UsbRole::Host && !DropHostDevice(runtime, false)) return false;
		CancelTimer(runtime);
		const bool restore = IC.TryMaskInterrupt();
		runtime.irq_active = false;
		MaskController(runtime);
		const uint32 generation = runtime.status.generation;
		if (!++runtime.status.generation) ++runtime.status.generation;
		runtime.status.current = UsbRole::None;
		runtime.status.host_connected = 0;
		runtime.status.device_configured = 0;
		runtime.port_pending = runtime.wake_queued = false;
		if (restore) IC.enInterrupt(true);
		device_event_cancel(runtime.owner_tid, kEventHandle, generation);
		if (runtime.hardware_role == UsbRole::Host) {
			HCD2.Stop();
			// StopHost() enables GINT again; NVIC remains masked until the new role.
			MaskController(runtime);
			Reference port(_OTG2_FS_ADDR + USB_OTG_HOST_PORT_BASE);
			port = stduint(port) & ~(USB_OTG_HPRT_PENA | USB_OTG_HPRT_PCDET |
				USB_OTG_HPRT_PENCHNG | USB_OTG_HPRT_POCCHNG | USB_OTG_HPRT_PPWR);
		}
		else if (runtime.hardware_role == UsbRole::Device) {
			runtime.peripheral.canMode();
			MaskController(runtime);
		}
		HCD2.ConnectHandler = HCD2.DisconnectHandler = HCD2.SOFHandler = nullptr;
		HCD2.NotifyURBChangeHandler = nullptr;
		PCD2.SetupStageHandler = PCD2.ResetHandler = nullptr;
		PCD2.ConnectHandler = PCD2.DisconnectHandler = nullptr;
		PCD2.SuspendHandler = PCD2.ResumeHandler = PCD2.SOFHandler = nullptr;
		PCD2.DataOutStageHandler = PCD2.DataInStageHandler = nullptr;
		HCD2.setInterrupt(nullptr);
		runtime.hardware_role = UsbRole::None;
		runtime.phase = SwitchPhase::Idle;
		return runtime.power.SetPower(false);
	}

	void FailTransition(OtgRuntime& runtime, UsbRoleError error) {
		if (!StopController(runtime)) error = UsbRoleError::Power;
		runtime.status.error = error;
		runtime.status.state = UsbRoleState::Fault;
		plogerro("[USB OTG2] role switch failed target=%u error=%u",
			(unsigned)runtime.status.target, (unsigned)error);
	}

	bool WaitRegisterBit(Reference reg, byte bit) {
		const uint32 started = uint32(SysTick::getTick());
		while (!reg.bitof(bit)) {
			if (uint32(SysTick::getTick()) - started >= Board::ClockTimeoutMs ||
				syscall(syscall_t::REST, 1, 1) != 0) return false;
		}
		return true;
	}

	bool PrepareClock(OtgRuntime& runtime) {
		PWR[uni::PWRReg::CR3].setof(kUsb33EnableBit);
		if (!WaitRegisterBit(PWR[uni::PWRReg::CR3], kUsb33ReadyBit)) return false;
		RCC[uni::RCCReg::CR].setof(kHsi48EnableBit);
		if (!WaitRegisterBit(RCC[uni::RCCReg::CR], kHsi48ReadyBit)) return false;
		RCC.setPeriphClock(uni::PeriphClock::USB, uni::ClockSource::HSI48);
		// STM32H743 WFI workaround: embedded FS PHY requires USB2OTGHSULPILPEN=0 (ST support topic 349480).
		auto sleep_clock = RCC[uni::RCCReg::AHB1LPENR];
		sleep_clock.setof(RCC_AHB1LPENR_USB2OTGHSLPEN_Pos);
		sleep_clock.rstof(RCC_AHB1LPENR_USB2OTGHSULPILPEN_Pos);
		__DSB();
		uni::GPIO[Board::DmPort][Board::DmPin].setMode(
			GPIOMode::OUT_AF_PushPull, GPIOSpeed::Veryhigh)._set_alternate(Board::AlternateFunction);
		uni::GPIO[Board::DpPort][Board::DpPin].setMode(
			GPIOMode::OUT_AF_PushPull, GPIOSpeed::Veryhigh)._set_alternate(Board::AlternateFunction);
		HCD2.enClock();
		runtime.core_accessible = true;
		return true;
	}

	bool WaitRole(UsbRole role) {
		const stduint expected = role == UsbRole::Host ? 1 : 0;
		const uint32 started = uint32(SysTick::getTick());
		// ST USB_SetCurrentMode waits before checking CMOD, including an unchanged role.
		if (syscall(syscall_t::REST, 1, 1) != 0) return false;
		while (OTG::getMode(_OTG2_FS_ADDR) != expected) {
			if (uint32(SysTick::getTick()) - started >= Board::ClockTimeoutMs ||
				syscall(syscall_t::REST, 1, 1) != 0) return false;
		}
		return true;
	}

	const byte* GetDeviceDescriptor(PeripheralSpeed, uint16& length) {
		length = sizeof(device_descriptor);
		return device_descriptor;
	}
	const byte* GetLanguageDescriptor(PeripheralSpeed, uint16& length) {
		length = sizeof(language_descriptor);
		return language_descriptor;
	}
	const byte* StringDescriptor(const char* string, uint16& length) {
		byte* buffer = otg->string_descriptor;
		unsigned count = 0;
		while (string[count] && count < (sizeof(otg->string_descriptor) - 2) / 2) {
			buffer[2 + count * 2] = byte(string[count]);
			buffer[3 + count * 2] = 0;
			++count;
		}
		length = uint16(2 + count * 2);
		buffer[0] = byte(length);
		buffer[1] = 3;
		return buffer;
	}
	const byte* GetManufacturerDescriptor(PeripheralSpeed, uint16& length) {
		return StringDescriptor("Mecocoa", length);
	}
	const byte* GetProductDescriptor(PeripheralSpeed, uint16& length) {
		return StringDescriptor("Mecocoa OTG", length);
	}

	void FinishRole(OtgRuntime& runtime) {
		const bool restore = IC.TryMaskInterrupt();
		runtime.status.current = runtime.hardware_role;
		runtime.status.state = UsbRoleState::Running;
		runtime.status.error = UsbRoleError::None;
		runtime.phase = SwitchPhase::Idle;
		runtime.irq_active = true;
		runtime.port_pending = true;
		if (runtime.hardware_role == UsbRole::Host) {
			runtime.host_last_wall = uint32(SysTick::getTick());
			runtime.host_clock_ms = 0;
		}
		HCD2.setInterrupt(OtgInterruptComplete);
		__DSB();
		HCD2.enInterrupt();
		NotifyWork(runtime);
		if (restore) IC.enInterrupt(true);
		ploginfo("[USB OTG2] role=%s", runtime.status.current == UsbRole::Host ? "Host" : "Device");
	}

	void StartTargetRole(OtgRuntime& runtime) {
		if (!PrepareClock(runtime)) { FailTransition(runtime, UsbRoleError::Clock); return; }
		// Initialize the PHY/core before selecting and waiting for the target mode.
		if (!OTG::Initialize(_OTG2_FS_ADDR, false, false, false)) {
			FailTransition(runtime, UsbRoleError::Controller);
			return;
		}
		OTG::setMode(_OTG2_FS_ADDR, runtime.status.target == UsbRole::Host ? 0 : 1);
		if (!WaitRole(runtime.status.target)) { FailTransition(runtime, UsbRoleError::Timeout); return; }
		runtime.hardware_role = runtime.status.target;
		if (runtime.hardware_role == UsbRole::Host) {
			HCD2.speed = 3;
			HCD2.host_channels = Board::HostChannels;
			HCD2.phy_itface = 2;
			HCD2.dma_enable = false;
			HCD2.use_external_vbus = false;
			if (!HCD2.setMode()) {
				FailTransition(runtime, UsbRoleError::Controller);
				return;
			}
			if (!WaitRole(UsbRole::Host)) { FailTransition(runtime, UsbRoleError::Timeout); return; }
			HCD2.ConnectHandler = HostConnected;
			HCD2.DisconnectHandler = HostDisconnected;
			HCD2.SOFHandler = HostSof;
			HCD2.NotifyURBChangeHandler = HostUrbCompleted;
			HCD2.setInterruptPriority(1, 0);
			if (!runtime.power.SetPower(true)) { FailTransition(runtime, UsbRoleError::Power); return; }
			runtime.phase = SwitchPhase::HostPowerWait;
			if (!ArmTimer(runtime, Board::PowerStableMs))
				FailTransition(runtime, UsbRoleError::Notification);
			return;
		}
		PCD2.speed = 3;
		PCD2.ep0_mps = 64;
		PCD2.dev_endpoints = 8;
		PCD2.phy_itface = 2;
		PCD2.dma_enable = false;
		PCD2.vbus_sensing_enable = Board::VbusSensing;
		PCD2.sof_enable = PCD2.lpm_enable = PCD2.battery_charging_enable = false;
		PCD2.use_external_vbus = PCD2.use_dedicated_ep1 = false;
		if (!PCD2.setMode()) {
			FailTransition(runtime, UsbRoleError::Controller);
			return;
		}
		if (!WaitRole(UsbRole::Device)) { FailTransition(runtime, UsbRoleError::Timeout); return; }
		PCD2.setRxFiFo(0x80);
		PCD2.setTxFiFo(0, 0x40);
		// Reconstruct in persistent storage; never put endpoint state on the stack.
		runtime.peripheral.~PeripheralDevice();
		new (&runtime.peripheral) PeripheralDevice;
		runtime.peripheral.setMode(PCD2, runtime.descriptors);
		runtime.peripheral.RegisterClass(runtime.peripheral_interface);
		runtime.notified_device_state = PeripheralState::Default;
		PCD2.SetupStageHandler = DeviceSetup;
		PCD2.ResetHandler = DeviceReset;
		PCD2.SuspendHandler = DeviceSuspend;
		PCD2.ResumeHandler = DeviceResume;
		PCD2.ConnectHandler = DeviceConnected;
		PCD2.DisconnectHandler = DeviceDisconnected;
		PCD2.DataOutStageHandler = DeviceDataOut;
		PCD2.DataInStageHandler = DeviceDataIn;
		PCD2.setInterruptPriority(1, 0);
		if (!runtime.peripheral.Start()) { FailTransition(runtime, UsbRoleError::Controller); return; }
		FinishRole(runtime);
	}

	uint32 AdvanceHostClock(OtgRuntime& runtime) {
		const bool restore = IC.TryMaskInterrupt();
		runtime.host_time_pending = false;
		const uint32 wall = uint32(SysTick::getTick());
		if (restore) IC.enInterrupt(true);
		// SysTick returns milliseconds; HFNUM can reset or stop with the host port.
		const uint32 elapsed = wall - runtime.host_last_wall;
		runtime.host_last_wall = wall;
		runtime.host_clock_ms += elapsed;
		OTGHostDevice::AdvanceTime(elapsed);
		return elapsed;
	}

	bool HostDeadlineReached(const OtgRuntime& runtime) {
		return int32(runtime.host_clock_ms - runtime.host_deadline_ms) >= 0;
	}

	void WriteHostReset(bool asserted) {
		Reference port(_OTG2_FS_ADDR + USB_OTG_HOST_PORT_BASE);
		stduint value = port;
		value &= ~(USB_OTG_HPRT_PENA | USB_OTG_HPRT_PCDET | USB_OTG_HPRT_PENCHNG |
			USB_OTG_HPRT_POCCHNG | USB_OTG_HPRT_PRST);
		if (asserted) value |= USB_OTG_HPRT_PRST;
		port = value;
	}

	void BeginHostEnumeration(OtgRuntime& runtime) {
		runtime.host_retries = 0;
		runtime.host_phase = HostPhase::Debounce;
		runtime.host_deadline_ms = runtime.host_clock_ms + Board::HostDebounceMs;
	}

	void FailHostEnumeration(OtgRuntime& runtime, const char* reason) {
		if (!DropHostDevice(runtime)) return;
		WriteHostReset(false);
		const bool connected = Reference(_OTG2_FS_ADDR + USB_OTG_HOST_PORT_BASE).bitof(USB_OTG_HPRT_PCSTS_Pos);
		if (connected && runtime.host_retries < Board::HostRetryLimit) {
			++runtime.host_retries;
			runtime.host_phase = HostPhase::RetryWait;
			runtime.host_deadline_ms = runtime.host_clock_ms + Board::HostRetryDelayMs;
			plogwarn("[USB OTG2] enumeration retry=%u reason=%s", unsigned(runtime.host_retries), reason);
		}
		else {
			runtime.host_phase = connected ? HostPhase::Failed : HostPhase::Idle;
			plogerro("[USB OTG2] enumeration failed: %s", reason);
		}
	}

	void DrainHostCompletions(OtgRuntime& runtime) {
		for (byte ch = 0; ch < Board::HostChannels; ++ch) {
			const uint16 bit = uint16(1u << ch);
			const bool restore = IC.TryMaskInterrupt();
			if (!(runtime.completion_pending & bit)) {
				if (restore) IC.enInterrupt(true);
				continue;
			}
			const HostCompletion completion = runtime.completions[ch];
			runtime.completion_pending &= uint16(~bit);
			const bool valid = runtime.host_accepting && completion.generation == runtime.host_generation &&
				completion.owner && completion.owner == OTGHostDevice::OwnerOfChannel(ch) &&
				(!completion.owner->IsControlChannel(ch) ||
					completion.submission == completion.owner->getControlSubmission());
			if (valid) {
				HCD2.hc[ch].xfer_count = completion.length;
				HCD2.ChannelReg(ch, 0x00C) = completion.interrupt_mask;
			}
			if (restore) IC.enInterrupt(true);
			if (valid) {
				completion.owner->OnChannelURBCompleted(ch, completion.state);
			}
		}
	}

	void PumpHostEnumeration(OtgRuntime& runtime, uint32 elapsed_ms) {
		if (runtime.host_cancel_active) return;
		if (runtime.completion_overflow) {
			FailHostEnumeration(runtime, "completion overwritten");
			return;
		}
		DrainHostCompletions(runtime);
		if (runtime.host_cancel_active) return;
		// New submissions above record the current bus time, never the previous wake's time.
		OTGHostDevice::ServiceAll(elapsed_ms);
		if (runtime.host_phase == HostPhase::Enumerating || runtime.host_phase == HostPhase::Ready) {
			if (!runtime.host_device) { FailHostEnumeration(runtime, "missing bridge"); return; }
			if (runtime.host_phase == HostPhase::Enumerating && runtime.host_device->IsEnumerated()) {
				runtime.host_phase = HostPhase::Ready;
				if (g_host_device_configured_hook) {
					runtime.host_registered = true;
					g_host_device_configured_hook(HostControllerIdentity(runtime),
						HostDeviceLocation(*runtime.host_device), *runtime.host_device);
				}
				ploginfo("[USB OTG2] enumerated speed=%u VID=%04X PID=%04X endpoints=%u configured=%u",
					unsigned(runtime.host_device->Speed()), unsigned(runtime.host_device->VendorID()),
					unsigned(runtime.host_device->ProductID()), unsigned(runtime.host_device->NumEndpointConfigs()),
					unsigned(runtime.host_device->IsInitialized()));
				for (int i = 0; i < runtime.host_device->NumEndpointConfigs(); ++i) {
					const auto& endpoint = runtime.host_device->EndpointConfigs()[i];
					ploginfo("[USB OTG2] endpoint=%u dir=%s type=%u mps=%u interval=%u",
						unsigned(endpoint.ep_id.Number()), endpoint.ep_id.IsIn() ? "IN" : "OUT",
						unsigned(endpoint.ep_type), unsigned(endpoint.max_packet_size), unsigned(endpoint.interval));
				}
			}
			else if (runtime.host_phase == HostPhase::Enumerating && HostDeadlineReached(runtime))
				FailHostEnumeration(runtime, "timeout");
			return;
		}
		if (runtime.host_phase == HostPhase::Idle || runtime.host_phase == HostPhase::Failed) return;
		if (runtime.host_phase == HostPhase::ResetEnable) {
			if (Reference(_OTG2_FS_ADDR + USB_OTG_HOST_PORT_BASE).bitof(USB_OTG_HPRT_PENA_Pos)) {
				runtime.host_phase = HostPhase::Recovery;
				runtime.host_deadline_ms = runtime.host_clock_ms + Board::HostRecoveryMs;
			}
			else if (HostDeadlineReached(runtime)) FailHostEnumeration(runtime, "port enable timeout");
			return;
		}
		if (!HostDeadlineReached(runtime)) return;
		if (runtime.host_phase == HostPhase::Debounce || runtime.host_phase == HostPhase::RetryWait) {
			WriteHostReset(true);
			runtime.host_phase = HostPhase::ResetHold;
			runtime.host_deadline_ms = runtime.host_clock_ms + Board::HostResetHoldMs;
		}
		else if (runtime.host_phase == HostPhase::ResetHold) {
			WriteHostReset(false);
			HCD2.HostReg(OTGHostReg::HCFG).setof(USB_OTG_HCFG_PERSCHEDENA_Pos);
			runtime.host_phase = HostPhase::ResetEnable;
			runtime.host_deadline_ms = runtime.host_clock_ms + Board::HostEnableTimeoutMs;
		}
		else if (runtime.host_phase == HostPhase::Recovery) {
			const byte speed = byte(HCD2.getCurrentSpeed());
			runtime.host_device = new OTGHostDevice(HCD2, 0, speed);
			if (!runtime.host_device || runtime.host_device->ControlChannel() == kNoChannel) {
				FailHostEnumeration(runtime, "bridge allocation");
				return;
			}
			runtime.host_accepting = true;
			runtime.host_phase = HostPhase::Enumerating;
			runtime.host_deadline_ms = runtime.host_clock_ms + Board::HostEnumerationTimeoutMs;
			if (auto error = runtime.host_device->StartInitialize()) {
				FailHostEnumeration(runtime, error.Name());
				return;
			}
			ploginfo("[USB OTG2] enumerating speed=%u", unsigned(speed));
		}
	}

	stdsint OtgControl(DeviceNode* node, stduint command, void* args, stduint) {
		if (!otg || node != otg->node || !args) return -1;
		const bool restore = IC.TryMaskInterrupt();
		if (UsbCtrlCommand(command) == UsbCtrlCommand::GetUsbRole) {
			auto& status = *static_cast<UsbRoleStatus*>(args);
			status = otg->status;
			if (otg->status.current == UsbRole::Device) {
				const auto state = otg->peripheral.getState();
				status.device_configured = state == PeripheralState::Configured ||
					(state == PeripheralState::Suspended && otg->peripheral.dev_old_state == PeripheralState::Configured);
			}
			if (restore) IC.enInterrupt(true);
			return 0;
		}
		stduint ipsr = 0;
		_ASM volatile("mrs %0, ipsr" : "=r"(ipsr));
		if (UsbCtrlCommand(command) != UsbCtrlCommand::RequestUsbRole || ipsr ||
			!Taskman::CurrentTB()) {
			if (restore) IC.enInterrupt(true);
			return -1;
		}
		auto& request = *static_cast<UsbRoleRequest*>(args);
		if (request.controller != kController ||
			(request.role != UsbRole::None && request.role != UsbRole::Host && request.role != UsbRole::Device) ||
			otg->status.state == UsbRoleState::Starting || otg->status.state == UsbRoleState::Switching) {
			if (restore) IC.enInterrupt(true);
			return -1;
		}
		if (request.role == otg->status.current && otg->status.state != UsbRoleState::Fault) {
			request.sequence = otg->status.sequence;
			if (restore) IC.enInterrupt(true);
			return 0;
		}
		const UsbRoleStatus previous = otg->status;
		otg->status.target = request.role;
		otg->status.state = otg->status.current == UsbRole::None ? UsbRoleState::Starting : UsbRoleState::Switching;
		otg->status.error = UsbRoleError::None;
		if (!++otg->status.sequence) ++otg->status.sequence;
		request.sequence = otg->status.sequence;
		const bool sent = !syssend_async(otg->owner_tid, &request, sizeof(request),
			_IMM(DevsmanMsg::USB_ROLE_REQUEST));
		if (!sent) {
			otg->status = previous;
			request.sequence = 0;
		}
		if (restore) IC.enInterrupt(true);
		return sent ? 0 : -1;
	}

	const DeviceNodeOps otg_ops{nullptr, nullptr, OtgControl};
}

bool Devsman::InitializeOTG(stduint owner_tid) {
	const auto* owner = Taskman::CurrentTB();
	if (!owner || owner->tid != owner_tid || !owner->device_events) return false;
	if (otg) return otg->owner_tid == owner_tid;
	auto* runtime = new OtgRuntime(owner_tid);
	if (!runtime) return false;
	if (!runtime->completions) { delete runtime; return false; }
	runtime->node = RegisterPlatformDevice("usb-otg2", "usb-otg", runtime);
	if (!runtime->node) { delete runtime; return false; }
	runtime->descriptors.GetDeviceDescriptor = GetDeviceDescriptor;
	runtime->descriptors.GetLangIDStrDescriptor = GetLanguageDescriptor;
	runtime->descriptors.GetManufacturerStrDescriptor = GetManufacturerDescriptor;
	runtime->descriptors.GetProductStrDescriptor = GetProductDescriptor;
	runtime->power.Initialize();
	if (!RegisterPowerTopology(*runtime))
		plogwarn("[USB OTG2] power topology registration failed");
	HCD2.base = PCD2.base = _OTG2_FS_ADDR;
	otg = runtime;
	#if _GUI_ENABLE
	HIDMouseDriver::default_observer = OtgMouseReport;
	HIDKeyboardDriver::default_observer = OtgKeyboardReport;
	#endif
	const bool power_off = runtime->power.SetPower(false);
	if (!power_off) {
		runtime->status.state = UsbRoleState::Fault;
		runtime->status.error = UsbRoleError::Power;
	}
	SetOps(runtime->node, &otg_ops);
	if (!power_off) {
		plogerro("[USB OTG2] cannot disable Host power");
		return false;
	}
	UsbRoleRequest request{};
	request.controller = kController;
	request.role = Board::DefaultRole;
	if (OtgControl(runtime->node, _IMM(UsbCtrlCommand::RequestUsbRole), &request, 0)) {
		plogerro("[USB OTG2] cannot request initial role");
		return false;
	}
	ploginfo("[USB OTG2] ready, default role=%s",
		request.role == UsbRole::Host ? "Host" : request.role == UsbRole::Device ? "Device" : "None");
	return true;
}

void Devsman::ProcessOTGRoleRequest(const UsbRoleRequest& request) {
	if (!otg || Taskman::CurrentTID() != otg->owner_tid ||
		request.controller != kController || request.sequence != otg->status.sequence ||
		request.role != otg->status.target || otg->phase != SwitchPhase::Idle ||
		(otg->status.state != UsbRoleState::Starting && otg->status.state != UsbRoleState::Switching)) return;
	if (!StopController(*otg)) { FailTransition(*otg, otg->status.error == UsbRoleError::Controller ?
		UsbRoleError::Controller : UsbRoleError::Power); return; }
	if (request.role == UsbRole::None) {
		otg->status.state = UsbRoleState::Stopped;
		ploginfo("[USB OTG2] role=None");
		return;
	}
	otg->phase = SwitchPhase::DisconnectWait;
	if (!ArmTimer(*otg, Board::DisconnectMs)) FailTransition(*otg, UsbRoleError::Notification);
}

UsbTransferStopResult Devsman::StopOTGTransfers(uni::device::SpaceUSB::USBHostDevice* device) {
	if (!otg || Taskman::CurrentTID() != otg->owner_tid || !device ||
		device != otg->host_device || otg->hardware_role != UsbRole::Host)
		return UsbTransferStopResult::Failed;
	if (otg->host_cancel_active && otg->host_cancel_result != UsbTransferStopResult::Pending)
		return otg->host_cancel_result;
	if (!otg->host_cancel_active) {
		Devs::prepare_usb_msc_disconnect(*device);
		const bool restore = IC.TryMaskInterrupt();
		otg->host_cancel_active = true;
		otg->host_cancel_result = UsbTransferStopResult::Pending;
		otg->host_cancel_started = uint32(SysTick::getTick());
		otg->host_cancel_mask = HCD2.GlobalReg(OTGGlobalReg::GINTMSK);
		otg->host_accepting = false;
		MaskController(*otg);
		otg->completion_pending = 0;
		otg->completion_overflow = false;
		if (!++otg->host_generation) ++otg->host_generation;
		if (restore) IC.enInterrupt(true);
	}
	bool stopped = otg->host_device->StopTransfers();
	bool reset = false;
	if (!stopped && uint32(SysTick::getTick()) - otg->host_cancel_started < kTransferStopTimeoutMs)
		return UsbTransferStopResult::Pending;
	if (!stopped) {
		reset = true;
		stopped = OTG::ResetCore(_OTG2_FS_ADDR) && otg->host_device->StopTransfers();
	}
	if (stopped) stopped = OTG::FlushRxFifo(_OTG2_FS_ADDR) && OTG::FlushTxFifo(_OTG2_FS_ADDR, 0x10);
	otg->host_cancel_result = stopped ? UsbTransferStopResult::Stopped : UsbTransferStopResult::Failed;
	otg->host_phase = HostPhase::Failed;
	if (!stopped || reset) {
		// A reset loses host configuration; the next role request must initialize it again.
		otg->status.state = UsbRoleState::Fault;
		otg->status.error = UsbRoleError::Controller;
		otg->irq_active = false;
		MaskController(*otg);
		plogerro("[USB OTG2] transfer stop=%s reset=%u", stopped ? "stopped" : "failed", unsigned(reset));
	} else if (otg->irq_active && otg->status.current == UsbRole::Host) {
		// Keep physical disconnect/reconnect visible without restoring channel or RX interrupts.
		HCD2.GlobalReg(OTGGlobalReg::GINTMSK) = USB_OTG_GINTMSK_PRTIM | USB_OTG_GINTMSK_DISCINT;
		HCD2.GlobalReg(OTGGlobalReg::GAHBCFG).setof(USB_OTG_GAHBCFG_GINT_Pos);
		__DSB();
		HCD2.enInterrupt();
	}
	return otg->host_cancel_result;
}

bool Devsman::ProcessOTGEvent(const DeviceEvent& event) {
	// Events only wake the service; its loop pumps the current runtime state.
	return event.device_handle == kEventHandle;
}

void Devsman::PumpOTG() {
	if (!otg || Taskman::CurrentTID() != otg->owner_tid) return;
	// Pending work survives coalesced or failed notifications; never poll hardware here.
	const bool restore = IC.TryMaskInterrupt();
	otg->wake_queued = false;
	const bool timer = otg->timer_armed && otg->timer_due;
	const bool port = otg->port_pending;
	otg->port_pending = false;
	if (restore) IC.enInterrupt(true);
	if (timer) {
		CancelTimer(*otg);
		if (otg->phase == SwitchPhase::DisconnectWait) StartTargetRole(*otg);
		else if (otg->phase == SwitchPhase::HostPowerWait) {
			if (!HCD2.Start()) FailTransition(*otg, UsbRoleError::Controller);
			else FinishRole(*otg);
		}
	}
	uint32 elapsed_ms = 0;
	if (otg->status.current == UsbRole::Host) elapsed_ms = AdvanceHostClock(*otg);
	if ((port || otg->host_phase != HostPhase::Idle) && otg->status.current == UsbRole::Host) {
		const bool restore_port = IC.TryMaskInterrupt();
		const uint32 disconnects = otg->disconnects;
		const bool connected = Reference(_OTG2_FS_ADDR + USB_OTG_HOST_PORT_BASE).bitof(USB_OTG_HPRT_PCSTS_Pos);
		const bool disconnected = otg->status.host_connected &&
			(!connected || otg->seen_disconnects != disconnects);
		const bool newly_connected = connected && (!otg->status.host_connected || disconnected);
		otg->seen_disconnects = disconnects;
		otg->status.host_connected = connected;
		if (restore_port) IC.enInterrupt(true);
		if (disconnected) {
			if (!DropHostDevice(*otg)) return;
			WriteHostReset(false);
			ploginfo("[USB OTG2] disconnected");
		}
		if (newly_connected && otg->status.state == UsbRoleState::Running) {
			BeginHostEnumeration(*otg);
			ploginfo("[USB OTG2] connected");
		}
	}
	if (otg->status.current == UsbRole::Host) {
		PumpHostEnumeration(*otg, elapsed_ms);
		// SOF and URB events provide normal progress; this also detects a stopped frame clock.
		if (otg->host_phase != HostPhase::Idle && otg->host_phase != HostPhase::Failed && !otg->timer_armed) {
			if (!ArmTimer(*otg, 1)) FailHostEnumeration(*otg, "service timer allocation");
		}
	}
	if (port && otg->status.current == UsbRole::Device) {
		const bool restore_device = IC.TryMaskInterrupt();
		const auto state = otg->peripheral.getState();
		const bool configured = state == PeripheralState::Configured ||
			(state == PeripheralState::Suspended && otg->peripheral.dev_old_state == PeripheralState::Configured);
		const bool changed = bool(otg->status.device_configured) != configured;
		otg->status.device_configured = configured;
		if (restore_device) IC.enInterrupt(true);
		if (changed) ploginfo("[USB OTG2] device configured=%u", unsigned(configured));
	}
	if (otg->reported_failures != otg->notification_failures) {
		otg->reported_failures = otg->notification_failures;
		plogwarn("[USB OTG2] event notification failures=%u", otg->reported_failures);
	}
}

#endif
