// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: [Device] Goodix GT1xx / GT9xx touch acquisition
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x)
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/IIC>
#include "touch-gtxxx.hpp"
#include "../../prehost/stm32h743X-ac7m/_openedv/unicfg.hpp"

namespace {
	stduint TouchTicks(stduint ms) {
		return maxof(stduint(1), (ms * CONFIG_SysTickFreq + 999) / 1000);
	}

	bool RestTouchThread(stduint ticks) {
		auto thread = Taskman::CurrentTB();
		const bool restore_interrupts = IC.TryMaskInterrupt();
		// Arm before blocking, under the same interrupt mask: no lost timer wakeup.
		const bool armed = Systimex::AppendThreadWake(ticks, thread->tid);
		if (armed) thread->Block(ThreadBlock::BlockReason::BR_Resting);
		if (restore_interrupts) IC.enInterrupt(true);
		if (armed) Taskman::Schedule(true);
		return armed;
	}

	void TouchIicDelay() {
		for (volatile unsigned i = 0; i < BoardConfig::Touch::IicDelayLoops; ++i) {}
	}

	class Gt1xxIic : public uni::IIC_SOFT {
	public:
		Gt1xxIic(uni::GPIN& sda, uni::GPIN& scl) : uni::IIC_SOFT(sda, scl, false) {}
		void Initialize() {
			// Match the board's tested SDA input/output switching bus implementation.
			push_pull = true;
			func_delay = TouchIicDelay;
			setMode();
		}
	};

	using TouchReadStatus = uni::TouchReadResult;

	class Gt1xxController : public uni::TouchTrait {
	public:
		byte pid[5] = {};
		byte ack_stage = 0xFF, status_byte = 0, interrupt_mode = 1;
		uint16 config_register = 0;
		uint16 error_register = 0;
		volatile stduint interrupts = 0;
	private:
		Gt1xxIic& iic;
		uni::GPIN& reset;
		uni::GPIN& interrupt;
		uni::Size2 screen;
		volatile bool pending = false;
		bool ack_pending = false, staged_valid = false;
		uni::UniquePtr<byte[]> raw_;
		uni::UniquePtr<uni::TouchContact[]> latest_;
		uni::TouchFrameInfo latest_info_;
		uni::TouchFrameInfo* pending_info_ = nullptr;
		uni::TouchContact* pending_data_ = nullptr;
		uint32 sequence_ = 0;
		stduint last_tick_ = 0;
		uint64 elapsed_ticks_ = 0;

		bool SendChecked(byte value, byte stage) {
			if (iic << value) return true;
			ack_stage = stage;
			iic.SendStop();
			return false;
		}
		bool AddressRegister(uint16 reg) {
			ack_stage = 0xFF;
			error_register = reg;
			iic.SendStart();
			return SendChecked(byte(BoardConfig::Touch::Address7 << 1), 0) &&
				SendChecked(byte(reg >> 8), 1) && SendChecked(byte(reg), 2);
		}
		bool ReadRegister(uint16 reg, byte* data, unsigned length) {
			if (!AddressRegister(reg)) return false;
			iic.SendStart();
			if (!SendChecked(byte((BoardConfig::Touch::Address7 << 1) | 1), 3)) return false;
			for (unsigned i = 0; i < length; ++i) data[i] = iic.ReadByte(true, i + 1 < length);
			iic.SendStop();
			return !iic.getError();
		}
		bool WriteRegister(uint16 reg, byte value) {
			if (!AddressRegister(reg) || !SendChecked(value, 4)) return false;
			iic.SendStop();
			return !iic.getError();
		}
		static uint16 Read16(const byte* p) { return uint16(p[0] | (uint16(p[1]) << 8)); }
		TouchReadStatus FinishFrame(uni::TouchFrameWriter& target) {
			// Retain this target until the hardware acknowledges the complete snapshot.
			if (!WriteRegister(0x814E, 0)) return TouchReadStatus::Retry;
			ack_pending = false;
			pending_info_ = nullptr;
			pending_data_ = nullptr;
			if (!staged_valid) return TouchReadStatus::Invalid;
			target.info->sequence = ++sequence_;
			latest_info_ = *target.info;
			for (uint16 i = 0; i < latest_info_.count; ++i) latest_[i] = target.data[i];
			return TouchReadStatus::Frame;
		}
	public:
		Gt1xxController(Gt1xxIic& bus, uni::GPIN& rst, uni::GPIN& irq, uni::Size2 size)
			: iic(bus), reset(rst), interrupt(irq), screen(size) {}

		bool Initialize() {
			raw_.reset(new (std::nothrow_t{}) byte[BoardConfig::Touch::MaxContacts * 8]);
			latest_.reset(new (std::nothrow_t{}) uni::TouchContact[BoardConfig::Touch::MaxContacts]);
			if (!raw_ || !latest_) return false;
			iic.Initialize();
			// INT selects the seven-bit address while reset is asserted.
			reset.setMode(uni::GPIOMode::OUT_PushPull);
			reset = false;
			interrupt.setMode(uni::GPIOMode::OUT_PushPull);
			interrupt = BoardConfig::Touch::Address7 == 0x14;
			if (!RestTouchThread(TouchTicks(20))) return false;
			reset = true;
			if (!RestTouchThread(TouchTicks(10))) return false;
			interrupt = false;
			if (!RestTouchThread(TouchTicks(50))) return false;
			interrupt.setMode(uni::GPIOMode::IN_Pull).setPull(true);
			if (!RestTouchThread(TouchTicks(100))) return false;
			if (!ReadRegister(0x8140, pid, 4)) return false;
			const bool gt9147 = pid[0] == '9' && pid[1] == '1' && pid[2] == '4' && pid[3] == '7';
			const bool gt1158 = pid[0] == '1' && pid[1] == '1' && pid[2] == '5' && pid[3] == '8';
			const bool gt911 = pid[0] == '9' && pid[1] == '1' && pid[2] == '1' && pid[3] == 0;
			const bool gt9271 = pid[0] == '9' && pid[1] == '2' && pid[2] == '7' && pid[3] == '1';
			if (!gt9147 && !gt1158 && !gt911 && !gt9271) return false;
			if (BoardConfig::Touch::MaxContacts > (gt9271 ? 10u : 5u)) return false;
			if (gt9147) {
				if (!WriteRegister(0x8040, 2) || !RestTouchThread(TouchTicks(10)) ||
					!WriteRegister(0x8040, 0) || !RestTouchThread(TouchTicks(10))) return false;
			}
			// Read the existing Module_Switch1 trigger bits; do not rewrite panel configuration.
			config_register = gt1158 ? 0x8050 : 0x8047;
			byte module_switch = 0;
			if (!ReadRegister(config_register + 6, &module_switch, 1)) return false;
			interrupt_mode = module_switch & 3;
			return true;
		}

		void HandleInterrupt() { ++interrupts; pending = true; }
		bool TakePending() {
			const bool restore_interrupts = IC.TryMaskInterrupt();
			const bool value = pending;
			pending = false;
			if (restore_interrupts) IC.enInterrupt(true);
			return value;
		}
		void EnableInterrupt(Handler_t handler) {
			// For level modes, use the assertion edge and drain while INT remains active.
			const auto edge = interrupt_mode == 0 || interrupt_mode == 3 ?
				uni::GPIORupt::Posedge : uni::GPIORupt::Negedge;
			interrupt.setMode(edge, handler);
			uni::EXTI_ClearPending(interrupt);
			pending = true;// also check a frame which arrived before EXTI was enabled
			interrupt.enInterrupt(true);
		}
		void DisableInterrupt() { interrupt.enInterrupt(false); }
		bool IsLevelAsserted() const {
			return interrupt_mode >= 2 && bool(interrupt) == (interrupt_mode == 3);
		}
		bool HasUnacknowledgedFrame() const { return ack_pending; }
		void AbortRead() { ack_pending = false; pending_info_ = nullptr; pending_data_ = nullptr; }
		bool getCapabilities(uni::TouchCapabilities& caps) const override {
			caps.bounds = uni::Rectangle(uni::Point(0, 0), screen);
			caps.max_contacts = BoardConfig::Touch::MaxContacts;
			caps.features = uni::TouchFeature_HardwareId | uni::TouchFeature_ContactSize;
			return true;
		}

		TouchReadStatus ReadFrame(uni::TouchFrameWriter& target) override {
			if (!target.info || !target.data || target.capacity < BoardConfig::Touch::MaxContacts || !raw_ || !latest_)
				return TouchReadStatus::InvalidTarget;
			if (ack_pending) {
				if (target.info != pending_info_ || target.data != pending_data_) return TouchReadStatus::InvalidTarget;
				return FinishFrame(target);
			}
			byte status = 0;
			if (!ReadRegister(0x814E, &status, 1)) return TouchReadStatus::Retry;
			status_byte = status;
			if (!(status & 0x80)) return TouchReadStatus::NoData;
			const stduint now = tick;
			elapsed_ticks_ += stduint(now - last_tick_);
			last_tick_ = now;
			target.info->timestamp_us = (elapsed_ticks_ / CONFIG_SysTickFreq) * 1000000u +
				(elapsed_ticks_ % CONFIG_SysTickFreq) * 1000000u / CONFIG_SysTickFreq;
			target.info->count = status & 0x0F;
			target.info->flags = 0;
			staged_valid = target.info->count <= BoardConfig::Touch::MaxContacts;
			if (staged_valid && target.info->count) {
				if (!ReadRegister(0x814F, raw_.get(), target.info->count * 8)) return TouchReadStatus::Retry;
				unsigned ids = 0;
				for (uint16 i = 0; i < target.info->count; ++i) {
					const byte* record = raw_.get() + i * 8;
					auto& contact = target.data[i];
					contact.id = record[0] & 0x0F;
					stduint x = Read16(record + 1), y = Read16(record + 3);
					if (BoardConfig::Touch::SwapXY) { const stduint swap = x; x = y; y = swap; }
					if ((record[0] & 0x80) || (ids & (1u << contact.id)) || x >= screen.x || y >= screen.y) {
						staged_valid = false;
						break;
					}
					ids |= 1u << contact.id;
					contact.position.x = BoardConfig::Touch::MirrorX ? screen.x - 1 - x : x;
					contact.position.y = BoardConfig::Touch::MirrorY ? screen.y - 1 - y : y;
					contact.size = Read16(record + 5);
					contact.pressure = 0;
					contact.valid = uni::TouchContactField_Id | uni::TouchContactField_Size;
				}
			}
			ack_pending = true;
			pending_info_ = target.info;
			pending_data_ = target.data;
			return FinishFrame(target);
		}
		// The RTOS owner drives acquisition; legacy queries observe the last acknowledged frame.
		byte Scan() override { return getPointCount(); }
		byte getPointCount() const override { return byte(latest_info_.count); }
		uni::Point getPoint(byte i) const override { return i < latest_info_.count ? latest_[i].position : uni::Point(0, 0); }
	};

	static_assert(BoardConfig::Touch::MaxContacts > 0 && BoardConfig::Touch::MaxContacts <= 10,
		"GT9xx supports at most ten contacts");
	static_assert(BoardConfig::Touch::Address7 == 0x14 || BoardConfig::Touch::Address7 == 0x5D,
		"GT9xx reset address must be 0x14 or 0x5D");
	static_assert(BoardConfig::Touch::ReadyRetryTicks > 0 && BoardConfig::Touch::ReadyTimeoutMs > 0,
		"Touch ready retries must have a finite positive interval");
	static_assert(BoardConfig::Touch::IoRetryLimit > 0 && BoardConfig::Touch::IoRetryLimit <= 8 &&
		BoardConfig::Touch::IoRetryDelayMs > 0, "Touch I2C retries must be bounded");

	constexpr uint16 kTouchFrameSlots = 8;
	constexpr uint32 kTouchEventHandle = 0x47543158;// private GT1X event source on this device thread

	struct TouchDiagnostics {
		stduint receives = 0, retries = 0, stale_retries = 0;
		stduint errors = 0, invalid = 0, not_ready = 0;
		stduint last_error_log = 0, last_notify_log = 0, reported_notify_fail = 0;

		void Record(Gt1xxController& touch, TouchReadStatus status, stduint now) {
			if (status == TouchReadStatus::Retry) ++errors;
			else if (status == TouchReadStatus::Invalid) ++invalid;
			else return;
			if (errors + invalid == 1 || now - last_error_log >= TouchTicks(1000)) {
				plogwarn("[TOUCH B] read=%s status=%02X reg=%04X ack_stage=%u io=%u invalid=%u",
					status == TouchReadStatus::Retry ? "I2C_ERROR" : "INVALID_FRAME", (unsigned)touch.status_byte,
					(unsigned)touch.error_register, (unsigned)touch.ack_stage, (unsigned)errors, (unsigned)invalid);
				last_error_log = now;
			}
		}
	};

	struct TouchRuntime {
		Gt1xxIic bus;
		Gt1xxController controller;
		TouchDiagnostics diagnostics;
		uni::TouchStorage storage;
		uni::TouchControlBlock control;
		DeviceNode* bus_node = nullptr;
		DeviceNode* device_node = nullptr;
		uni::TouchFrameToken capture_token;
		uni::TouchFrameWriter capture_target;
		bool capture_active = false;
		stduint owner_tid = 0, ready_since = 0, io_failures = 0;
		volatile bool running = false, retry_armed = false, retry_due = false;
		volatile uint32 retry_token = 0;
		volatile stduint irq_notify_fail = 0, retry_notify_fail = 0;
		bool ready_waiting = false, drain_pending = false;

		TouchRuntime(const uni::Size2& screen)
			: bus(uni::GPIO[BoardConfig::Touch::SdaPort][BoardConfig::Touch::SdaPin],
				uni::GPIO[BoardConfig::Touch::SclPort][BoardConfig::Touch::SclPin]),
			controller(bus, uni::GPIO[BoardConfig::Touch::ResetPort][BoardConfig::Touch::ResetPin],
				uni::GPIO[BoardConfig::Touch::InterruptPort][BoardConfig::Touch::InterruptPin], screen) {}
	};

	bool UnregisterTouchTopology(TouchRuntime& runtime) {
		const bool restore_interrupts = IC.TryMaskInterrupt();
		bool ready = true;
		if (runtime.device_node) {
			ready = Devsman::UnregisterDevice(runtime.device_node);
			if (ready) runtime.device_node = nullptr;
		}
		if (ready && runtime.bus_node) {
			ready = Devsman::UnregisterBus(runtime.bus_node);
			if (ready) runtime.bus_node = nullptr;
		}
		if (restore_interrupts) IC.enInterrupt(true);
		return ready;
	}

	bool RegisterTouchTopology(TouchRuntime& runtime) {
		static_assert(BoardConfig::Touch::InterruptPin < 16, "Touch interrupt must be an EXTI GPIO pin");
		static const uni::Request_t exti_low[] = {uni::IRQ_EXTI0, uni::IRQ_EXTI1,
			uni::IRQ_EXTI2, uni::IRQ_EXTI3, uni::IRQ_EXTI4};
		const auto interrupt_pin = uni::GPIO[BoardConfig::Touch::InterruptPort][BoardConfig::Touch::InterruptPin].getID();
		const auto irq = interrupt_pin < numsof(exti_low) ? exti_low[interrupt_pin] :
			interrupt_pin < 10 ? uni::IRQ_EXTI9_5 : uni::IRQ_EXTI15_10;
		const bool restore_interrupts = IC.TryMaskInterrupt();
		const auto bus_name = String::newFormat("i2c-soft@P%c%u-P%c%u",
			(int)BoardConfig::Touch::SclPort, (unsigned)BoardConfig::Touch::SclPin,
			(int)BoardConfig::Touch::SdaPort, (unsigned)BoardConfig::Touch::SdaPin);
		runtime.bus_node = Devsman::RegisterBus(DeviceBusType::I2C, DeviceNodeType::I2cBus,
			bus_name.reference(), nullptr, "i2c-soft", static_cast<uni::IIC_SOFT*>(&runtime.bus));
		bool ready = runtime.bus_node &&
			Devsman::AddPinResource(runtime.bus_node, 0, "SCL", BoardConfig::Touch::SclPort, BoardConfig::Touch::SclPin) &&
			Devsman::AddPinResource(runtime.bus_node, 1, "SDA", BoardConfig::Touch::SdaPort, BoardConfig::Touch::SdaPin) &&
			Devsman::AddConfigurationResource(runtime.bus_node, 0, "delay-loops", BoardConfig::Touch::IicDelayLoops);
		if (ready) {
			const auto name = String::newFormat("touch@%02X", (unsigned)BoardConfig::Touch::Address7);
			runtime.device_node = Devsman::RegisterDevice(runtime.bus_node, DeviceNodeType::I2cDevice,
				DeviceBusType::I2C, name.reference(), "touch-gtxxx", &runtime.controller);
			ready = runtime.device_node &&
				Devsman::AddAddressResource(runtime.device_node, 0, BoardConfig::Touch::Address7, 7) &&
				Devsman::AddPinResource(runtime.device_node, 0, "RST", BoardConfig::Touch::ResetPort, BoardConfig::Touch::ResetPin) &&
				Devsman::AddPinResource(runtime.device_node, 1, "INT", BoardConfig::Touch::InterruptPort, BoardConfig::Touch::InterruptPin) &&
				Devsman::AddResource(runtime.device_node, DeviceResource{uint16(DeviceResourceType::IrqLine),
					DeviceResourceFlag_None, 0, uint64(_IMM(irq)), 1, 0});
		}
		if (restore_interrupts) IC.enInterrupt(true);
		return ready;
	}

	TouchRuntime* touch_runtime = nullptr;
	uint32 touch_retry_serial = 0;

	void Gt1xxInterrupt() {
		auto* runtime = touch_runtime;
		if (!runtime || !runtime->running) return;
		runtime->controller.HandleInterrupt();
		DeviceEvent event{};
		event.kind = _IMM(DeviceEventKind::Interrupt);
		event.device_handle = kTouchEventHandle;
		event.source = BoardConfig::Touch::InterruptPin;
		event.count = 1;
		if (!device_event_proc(runtime->owner_tid, event)) ++runtime->irq_notify_fail;
	}

	void TouchRetryWake(pureptr_t, stduint token) {
		// Keep validation and enqueue atomic against the owner stopping/cancelling this retry.
		const bool restore_interrupts = IC.TryMaskInterrupt();
		auto* runtime = touch_runtime;
		if (runtime && runtime->running && runtime->retry_armed && runtime->retry_token == token) {
			runtime->retry_due = true;
			DeviceEvent event{};
			event.kind = _IMM(DeviceEventKind::TimerExpired);
			event.device_handle = kTouchEventHandle;
			event.sequence = token;
			event.count = 1;
			if (!device_event_proc(runtime->owner_tid, event)) ++runtime->retry_notify_fail;
		}
		if (restore_interrupts) IC.enInterrupt(true);
	}

	void CancelTouchRetry(TouchRuntime& runtime) {
		const bool restore_interrupts = IC.TryMaskInterrupt();
		const uint32 token = runtime.retry_token;
		const bool armed = runtime.retry_armed;
		runtime.retry_armed = false;
		runtime.retry_due = false;
		if (!++touch_retry_serial) ++touch_retry_serial;
		runtime.retry_token = touch_retry_serial;
		if (armed) (void)Systimex::CancelDeferredCallback(token, (_tocall_ft)TouchRetryWake);
		if (restore_interrupts) IC.enInterrupt(true);
	}

	bool ArmTouchRetry(TouchRuntime& runtime, stduint delay) {
		CancelTouchRetry(runtime);
		const bool restore_interrupts = IC.TryMaskInterrupt();
		runtime.retry_armed = true;
		const bool armed = Systimex::AppendDeferredCallback(delay, runtime.retry_token, (_tocall_ft)TouchRetryWake);
		if (!armed) runtime.retry_armed = false;
		if (restore_interrupts) IC.enInterrupt(true);
		if (!armed) plogerro("[TOUCH B] cannot arm the finite retry; touch stopped");
		return armed;
	}

	bool WaitTouchWork(TouchRuntime& runtime) {
		for (;;) {
			// Pending is retained even if an event could not be queued. A full queue already wakes us.
			const bool pending = runtime.controller.TakePending();
			if ((pending || runtime.drain_pending) && (!runtime.io_failures || !runtime.retry_armed)) {
				runtime.drain_pending = false;
				return true;
			}
			const bool restore_interrupts = IC.TryMaskInterrupt();
			const bool retry = runtime.retry_armed && runtime.retry_due;
			if (retry) runtime.retry_due = false;
			if (restore_interrupts) IC.enInterrupt(true);
			if (retry) { ++runtime.diagnostics.retries; return true; }

			DeviceEvent event{};
			stduint type = 0;
			// The queue and receive registration cover an interrupt arriving just before we block.
			if (sysrecv(ANYPROC, &event, sizeof(event), &type)) {
				plogerro("[TOUCH B] event receive failed; touch stopped");
				return false;
			}
			++runtime.diagnostics.receives;
			if (type != _IMM(KernelMsg::DeviceEvent) || event.version != DeviceEventProtocolVersion ||
				event.device_handle != kTouchEventHandle) continue;
			if (DeviceEventKind(event.kind) == DeviceEventKind::TimerExpired &&
				(!runtime.retry_armed || event.sequence != runtime.retry_token)) ++runtime.diagnostics.stale_retries;
		}
	}

	void ReportTouchNotificationErrors(TouchRuntime& runtime) {
		auto& stats = runtime.diagnostics;
		const stduint failed = runtime.irq_notify_fail + runtime.retry_notify_fail;
		if (failed == stats.reported_notify_fail) return;
		if (stats.reported_notify_fail && stduint(tick - stats.last_notify_log) < TouchTicks(1000)) return;
		plogwarn("[TOUCH B] event notification failures=%u", (unsigned)failed);
		stats.reported_notify_fail = failed;
		stats.last_notify_log = tick;
	}

	bool ProcessTouchWork(TouchRuntime& runtime) {
		if (!WaitTouchWork(runtime)) return false;
		CancelTouchRetry(runtime);
		const stduint now = tick;
		if (!runtime.capture_active) {
			if (!runtime.control.BeginCapture(runtime.capture_token, runtime.capture_target)) {
				plogerro("[TOUCH] cannot acquire a frame slot; touch stopped");
				return false;
			}
			runtime.capture_active = true;
		}
		const auto status = runtime.controller.ReadFrame(runtime.capture_target);
		runtime.diagnostics.Record(runtime.controller, status, now);
		if (status != TouchReadStatus::Retry) {
			if (status == TouchReadStatus::Frame) {
				if (!runtime.control.CommitCapture(runtime.capture_token))
					plogwarn("[TOUCH] invalid or cancelled frame rejected");
			}
			else runtime.control.AbortCapture(runtime.capture_token);
			runtime.capture_active = false;
			if (status == TouchReadStatus::Invalid) runtime.control.RequestCancel(uni::TouchCancelReason::InvalidFrame);
		}
		if (status == TouchReadStatus::InvalidTarget || status == TouchReadStatus::Unsupported || status == TouchReadStatus::Stopped) {
			plogerro("[TOUCH] acquisition contract failed; touch stopped");
			return false;
		}
		bool okay = true;
		if (status == TouchReadStatus::Retry) {
			runtime.ready_waiting = false;
			if (++runtime.io_failures >= BoardConfig::Touch::IoRetryLimit) {
				plogerro("[TOUCH B] I2C retry limit reached; touch stopped");
				okay = false;
			}
			else okay = ArmTouchRetry(runtime, TouchTicks(BoardConfig::Touch::IoRetryDelayMs) << (runtime.io_failures - 1));
		}
		else {
			runtime.io_failures = 0;
			if (status == TouchReadStatus::NoData) {
				if (!runtime.ready_waiting) { runtime.ready_waiting = true; runtime.ready_since = now; }
				if (stduint(tick - runtime.ready_since) < TouchTicks(BoardConfig::Touch::ReadyTimeoutMs)) {
					okay = ArmTouchRetry(runtime, BoardConfig::Touch::ReadyRetryTicks);
				}
				else {
					++runtime.diagnostics.not_ready;
					runtime.ready_waiting = false;
					if (runtime.controller.IsLevelAsserted()) {
						plogerro("[TOUCH B] INT held active without ready data; touch stopped");
						okay = false;
					}
				}
			}
			else {
				runtime.ready_waiting = false;
				runtime.drain_pending = runtime.controller.IsLevelAsserted();
			}
		}
		ReportTouchNotificationErrors(runtime);
		return okay;
	}

	void StopTouch(TouchRuntime& runtime) {
		const bool restore_interrupts = IC.TryMaskInterrupt();
		runtime.running = false;
		runtime.controller.DisableInterrupt();
		CancelTouchRetry(runtime);
		// Remove stale touch notifications before the owner returns to its generic idle loop.
		device_event_cancel(runtime.owner_tid, kTouchEventHandle, 0);
		if (restore_interrupts) IC.enInterrupt(true);
		runtime.controller.AbortRead();
		if (runtime.capture_active) {
			runtime.control.AbortCapture(runtime.capture_token);
			runtime.capture_active = false;
		}
		runtime.control.RequestCancel(uni::TouchCancelReason::Stopped);
	}

}

uni::TouchTrait* TouchGt1xx::Initialize(const uni::Size2& screen) {
	if (touch_runtime) return touch_runtime->running ? &touch_runtime->controller : nullptr;
	auto* owner = Taskman::CurrentTB();
	if (!owner || !device_event_prepare(owner)) {
		plogerro("[TOUCH B] cannot prepare the device event queue");
		return nullptr;
	}
	auto runtime = new TouchRuntime(screen);
	if (!runtime) {
		plogerro("[TOUCH B] cannot allocate the touch device");
		return nullptr;
	}
	auto& touch = runtime->controller;
	if (!touch.Initialize() || !runtime->storage.Initialize(BoardConfig::Touch::MaxContacts, kTouchFrameSlots) ||
		!runtime->control.Initialize(touch, runtime->storage)) {
		plogerro("[TOUCH B] init failed reg=%04X ack_stage=%u PID=%02X%02X%02X%02X; display stays active",
			(unsigned)touch.error_register, (unsigned)touch.ack_stage, (unsigned)touch.pid[0],
			(unsigned)touch.pid[1], (unsigned)touch.pid[2], (unsigned)touch.pid[3]);
		delete runtime;
		return nullptr;
	}
	if (!RegisterTouchTopology(*runtime)) {
		plogerro("[TOUCH] topology registration failed");
		if (UnregisterTouchTopology(*runtime)) delete runtime;
		else {
			plogerro("[TOUCH] cannot remove incomplete topology; runtime retained");
			touch_runtime = runtime;
		}
		return nullptr;
	}
	runtime->owner_tid = owner->tid;
	runtime->running = true;
	touch_runtime = runtime;
	touch.EnableInterrupt(Gt1xxInterrupt);
	return &touch;
}

uni::TouchControlBlock* TouchGt1xx::GetControlBlock() {
	return touch_runtime ? &touch_runtime->control : nullptr;
}

void TouchGt1xx::Shutdown() {
	if (!touch_runtime) return;
	auto* runtime = touch_runtime;
	if (runtime->running) StopTouch(*runtime);
	if (!UnregisterTouchTopology(*runtime)) {
		plogerro("[TOUCH] topology is still referenced; runtime retained");
		return;
	}
	const bool restore_interrupts = IC.TryMaskInterrupt();
	touch_runtime = nullptr;
	if (restore_interrupts) IC.enInterrupt(true);
	delete runtime;
}

bool TouchGt1xx::Process() {
	if (!touch_runtime || !touch_runtime->running) return false;
	if (ProcessTouchWork(*touch_runtime)) return true;
	StopTouch(*touch_runtime);
	return false;
}

#endif
