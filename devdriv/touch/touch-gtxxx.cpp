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

	enum class TouchReadStatus { NoFrame, Frame, IoError, InvalidFrame };

	class Gt1xxController : public uni::TouchTrait {
	public:
		enum : unsigned { Capacity = 10 };
		struct Contact {
			byte id = 0;
			uint16 x = 0, y = 0, size = 0;
		};
		struct Frame {
			stduint sequence = 0, time = 0;
			byte count = 0;
			Contact contacts[Capacity];
		};
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
		Frame latest = {}, staged = {};

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
		TouchReadStatus FinishFrame() {
			// Keep the staged frame on acknowledgement failure; publish it only once.
			if (!WriteRegister(0x814E, 0)) return TouchReadStatus::IoError;
			ack_pending = false;
			if (!staged_valid) return TouchReadStatus::InvalidFrame;
			staged.sequence = latest.sequence + 1;
			latest = staged;
			return TouchReadStatus::Frame;
		}
	public:
		Gt1xxController(Gt1xxIic& bus, uni::GPIN& rst, uni::GPIN& irq, uni::Size2 size)
			: iic(bus), reset(rst), interrupt(irq), screen(size) {}

		bool Initialize() {
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
		const Frame& GetFrame() const { return latest; }
		bool HasUnacknowledgedFrame() const { return ack_pending; }

		TouchReadStatus ReadFrame() {
			if (ack_pending) return FinishFrame();
			byte status = 0;
			if (!ReadRegister(0x814E, &status, 1)) return TouchReadStatus::IoError;
			status_byte = status;
			if (!(status & 0x80)) return TouchReadStatus::NoFrame;
			staged = Frame{};
			staged.time = tick;
			staged.count = status & 0x0F;
			staged_valid = staged.count <= BoardConfig::Touch::MaxContacts;
			if (staged_valid && staged.count) {
				byte raw[Capacity * 8];
				// Each full record starts with its hardware contact ID at 0x814F.
				if (!ReadRegister(0x814F, raw, staged.count * 8)) return TouchReadStatus::IoError;
				unsigned ids = 0;
				for (byte i = 0; i < staged.count; ++i) {
					const byte* record = raw + i * 8;
					auto& contact = staged.contacts[i];
					contact.id = record[0] & 0x0F;
					stduint x = Read16(record + 1), y = Read16(record + 3);
					if (BoardConfig::Touch::SwapXY) { const stduint swap = x; x = y; y = swap; }
					if ((record[0] & 0x80) || (ids & (1u << contact.id)) || x >= screen.x || y >= screen.y) {
						staged_valid = false;
						break;
					}
					ids |= 1u << contact.id;
					contact.x = uint16(BoardConfig::Touch::MirrorX ? screen.x - 1 - x : x);
					contact.y = uint16(BoardConfig::Touch::MirrorY ? screen.y - 1 - y : y);
					contact.size = Read16(record + 5);
				}
			}
			ack_pending = true;
			return FinishFrame();
		}
		byte Scan() override { (void)ReadFrame(); return latest.count; }
		byte GetPointCount() const override { return latest.count; }
		uni::Point GetPoint(byte i) const override {
			return i < latest.count ? uni::Point(latest.contacts[i].x, latest.contacts[i].y) : uni::Point(0, 0);
		}
	};

	static_assert(BoardConfig::Touch::MaxContacts > 0 && unsigned(BoardConfig::Touch::MaxContacts) <= unsigned(Gt1xxController::Capacity),
		"Touch contact limit exceeds the frame buffer");
	static_assert(BoardConfig::Touch::Address7 == 0x14 || BoardConfig::Touch::Address7 == 0x5D,
		"GT9xx reset address must be 0x14 or 0x5D");
	static_assert(BoardConfig::Touch::ReadyRetryTicks > 0 && BoardConfig::Touch::ReadyTimeoutMs > 0,
		"Touch ready retries must have a finite positive interval");
	static_assert(BoardConfig::Touch::IoRetryLimit > 0 && BoardConfig::Touch::IoRetryLimit <= 8 &&
		BoardConfig::Touch::IoRetryDelayMs > 0, "Touch I2C retries must be bounded");

	constexpr uint32 kTouchEventHandle = 0x47543158;// private GT1X event source on this device thread

	struct TouchDiagnostics {
		stduint frames = 0, reads = 0, receives = 0, retries = 0, stale_retries = 0;
		stduint errors = 0, invalid = 0, not_ready = 0, max_read_ticks = 0;
		stduint last_log = tick, last_error_log = tick, last_stats = tick;
		unsigned previous_ids = 0;
		bool have_frame = false;

		void Record(Gt1xxController& touch, TouchReadStatus status, stduint now) {
			++reads;
			max_read_ticks = maxof(max_read_ticks, stduint(tick - now));
			if (status == TouchReadStatus::Frame) {
				++frames;
				const auto& frame = touch.GetFrame();
				unsigned ids = 0;
				for (byte i = 0; i < frame.count; ++i) ids |= 1u << frame.contacts[i].id;
				if (!have_frame || ids != previous_ids || now - last_log >= TouchTicks(BoardConfig::Touch::MoveLogPeriodMs)) {
					ploginfo("[TOUCH B] frame=%u tick=%u count=%u ids=%04X",
						(unsigned)frame.sequence, (unsigned)frame.time, (unsigned)frame.count, ids);
					for (byte i = 0; i < frame.count; ++i) {
						const auto& point = frame.contacts[i];
						ploginfo("[TOUCH B] id=%u x=%u y=%u size=%u", (unsigned)point.id,
							(unsigned)point.x, (unsigned)point.y, (unsigned)point.size);
					}
					last_log = now;
				}
				previous_ids = ids;
				have_frame = true;
			}
			else {
				if (status == TouchReadStatus::IoError) ++errors;
				if (status == TouchReadStatus::InvalidFrame) ++invalid;
				if (status != TouchReadStatus::NoFrame &&
					(errors + invalid == 1 || now - last_error_log >= TouchTicks(1000))) {
					plogwarn("[TOUCH B] read=%s status=%02X reg=%04X ack_stage=%u io=%u invalid=%u",
						status == TouchReadStatus::IoError ? "I2C_ERROR" : "INVALID_FRAME", (unsigned)touch.status_byte,
						(unsigned)touch.error_register, (unsigned)touch.ack_stage, (unsigned)errors, (unsigned)invalid);
					last_error_log = now;
				}
			}
		}
	};

	struct TouchRuntime {
		Gt1xxIic bus;
		Gt1xxController controller;
		TouchDiagnostics diagnostics;
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

	TouchRuntime* touch_runtime = nullptr;

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
		if (!++runtime.retry_token) ++runtime.retry_token;
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

	void PrintTouchStats(TouchRuntime& runtime) {
		auto& stats = runtime.diagnostics;
		if (stduint(tick - stats.last_stats) < TouchTicks(5000)) return;
		ploginfo("[TOUCH B] stats irq=%u recv=%u reads=%u frames=%u retry=%u stale=%u notifyfail=%u io=%u invalid=%u notready=%u maxread_ticks=%u",
			(unsigned)runtime.controller.interrupts, (unsigned)stats.receives, (unsigned)stats.reads,
			(unsigned)stats.frames, (unsigned)stats.retries, (unsigned)stats.stale_retries,
			(unsigned)(runtime.irq_notify_fail + runtime.retry_notify_fail), (unsigned)stats.errors,
			(unsigned)stats.invalid, (unsigned)stats.not_ready, (unsigned)stats.max_read_ticks);
		stats.last_stats = tick;// statistics never schedule an idle wakeup
	}

	bool ProcessTouchWork(TouchRuntime& runtime) {
		if (!WaitTouchWork(runtime)) return false;
		CancelTouchRetry(runtime);
		const stduint now = tick;
		const auto status = runtime.controller.ReadFrame();
		runtime.diagnostics.Record(runtime.controller, status, now);
		bool okay = true;
		if (status == TouchReadStatus::IoError) {
			runtime.ready_waiting = false;
			if (++runtime.io_failures >= BoardConfig::Touch::IoRetryLimit) {
				plogerro("[TOUCH B] I2C retry limit reached; touch stopped");
				okay = false;
			}
			else okay = ArmTouchRetry(runtime, TouchTicks(BoardConfig::Touch::IoRetryDelayMs) << (runtime.io_failures - 1));
		}
		else {
			runtime.io_failures = 0;
			if (status == TouchReadStatus::NoFrame) {
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
		PrintTouchStats(runtime);
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
	ploginfo("[TOUCH B] init addr7=%02X SCL=%c%u SDA=%c%u RST=%c%u INT=%c%u",
		(unsigned)BoardConfig::Touch::Address7, BoardConfig::Touch::SclPort, (unsigned)BoardConfig::Touch::SclPin,
		BoardConfig::Touch::SdaPort, (unsigned)BoardConfig::Touch::SdaPin, BoardConfig::Touch::ResetPort,
		(unsigned)BoardConfig::Touch::ResetPin, BoardConfig::Touch::InterruptPort, (unsigned)BoardConfig::Touch::InterruptPin);
	if (!touch.Initialize()) {
		plogerro("[TOUCH B] init failed reg=%04X ack_stage=%u PID=%02X%02X%02X%02X; display stays active",
			(unsigned)touch.error_register, (unsigned)touch.ack_stage, (unsigned)touch.pid[0],
			(unsigned)touch.pid[1], (unsigned)touch.pid[2], (unsigned)touch.pid[3]);
		delete runtime;
		return nullptr;
	}
	runtime->owner_tid = owner->tid;
	runtime->running = true;
	touch_runtime = runtime;
	touch.EnableInterrupt(Gt1xxInterrupt);
	ploginfo("[TOUCH B] PID=%s ready event-driven int_mode=%u config=%04X max_contacts=%u screen=%ux%u",
		(char*)touch.pid, (unsigned)touch.interrupt_mode, (unsigned)touch.config_register,
		(unsigned)BoardConfig::Touch::MaxContacts, (unsigned)screen.x, (unsigned)screen.y);
	return &touch;
}

bool TouchGt1xx::Process() {
	if (!touch_runtime || !touch_runtime->running) return false;
	if (ProcessTouchWork(*touch_runtime)) return true;
	StopTouch(*touch_runtime);
	return false;
}

#endif
