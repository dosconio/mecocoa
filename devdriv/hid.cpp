// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Human Interface Device Drivers
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/Serial/IIC-HID.hpp>
#include <cpp/Device/HID/HID-ALPS.hpp>
#include <cpp/endian>

namespace {
	constexpr stduint HidI2cTransferBudget = 100000u;
	constexpr stduint HidI2cActivationDelay = 10u;
	constexpr stduint HidI2cInputRetryDelay = 2u;
	constexpr stduint HidI2cRecoveryDelay = 10u;
	constexpr stduint HidI2cResetTimeout = 500u;
	constexpr uint32 HidI2cInputDrainLimit = 8u;
	constexpr uint32 HidI2cInputPendingLimit = 64u;
	constexpr uint8 HidI2cActivationAttemptLimit = 50u;
	constexpr uint8 HidI2cInputRetryLimit = 3u;
	constexpr uint8 HidI2cRecoveryAttemptLimit = 3u;
	constexpr uint8 HidI2cStuckInterruptLimit = 8u;
	constexpr uint16 AcpiI2cTypeFlagTenBitAddress = 1u << 0;
	constexpr uint16 HantickVendor = 0x0911u;
	constexpr uint16 HantickProduct5283 = 0x5283u;
	constexpr uint16 HantickProduct5288 = 0x5288u;

	enum HidInputQuirkFlags : uint32 {
		HidInputQuirk_None = 0,
		HidInputQuirk_PreferTouchpadMotion = 1u << 0,
		HidInputQuirk_IgnoreDuplicateContacts = 1u << 1,
		HidInputQuirk_NotSeenMeansUp = 1u << 2,
	};

	enum class HidI2cFailure : uint8 {
		None,
		MissingAcpiDescription,
		MissingI2cResource,
		MissingDescriptorRegister,
		Allocation,
		Descriptor,
		ReportDescriptor,
		ReportLayout,
		InputRuntime,
		ActivationSchedule,
		ActivationTimeout,
		GPIOInterrupt,
		Power,
		Reset,
		ResetSchedule,
		ResetTimeout,
		InputSchedule,
		InputTransfer,
		GPIOCompletion,
		ProtocolInitialization,
		RecoverySchedule,
		RecoveryLimit,
		StuckInterrupt,
	};

	enum class HidI2cActivationState : uint8 {
		Inactive,
		Scheduled,
		ResetPending,
		Running,
		Recovering,
		Suspended,
		Stopping,
		Failed,
	};

	enum class HidI2cInputDisposition : uint8 {
		Consumed,
		Ignored,
		Retry,
		Fatal,
	};

	const char* HidI2cFailureName(HidI2cFailure failure) {
		switch (failure) {
		case HidI2cFailure::None: return "none";
		case HidI2cFailure::MissingAcpiDescription: return "missing-acpi";
		case HidI2cFailure::MissingI2cResource: return "missing-i2c-resource";
		case HidI2cFailure::MissingDescriptorRegister: return "missing-descriptor-register";
		case HidI2cFailure::Allocation: return "allocation";
		case HidI2cFailure::Descriptor: return "descriptor";
		case HidI2cFailure::ReportDescriptor: return "report-descriptor";
		case HidI2cFailure::ReportLayout: return "report-layout";
		case HidI2cFailure::InputRuntime: return "input-runtime";
		case HidI2cFailure::ActivationSchedule: return "activation-schedule";
		case HidI2cFailure::ActivationTimeout: return "activation-timeout";
		case HidI2cFailure::GPIOInterrupt: return "gpio-interrupt";
		case HidI2cFailure::Power: return "power";
		case HidI2cFailure::Reset: return "reset";
		case HidI2cFailure::ResetSchedule: return "reset-schedule";
		case HidI2cFailure::ResetTimeout: return "reset-timeout";
		case HidI2cFailure::InputSchedule: return "input-schedule";
		case HidI2cFailure::InputTransfer: return "input-transfer";
		case HidI2cFailure::GPIOCompletion: return "gpio-completion";
		case HidI2cFailure::ProtocolInitialization: return "protocol-initialization";
		case HidI2cFailure::RecoverySchedule: return "recovery-schedule";
		case HidI2cFailure::RecoveryLimit: return "recovery-limit";
		case HidI2cFailure::StuckInterrupt: return "stuck-interrupt";
		default: return "unknown";
		}
	}

	const DeviceAcpiSerialResource* FindHidI2cResource(const DeviceNode* endpoint) {
		if (!endpoint || !endpoint->fields.acpi_info) return nullptr;
		const auto* info = endpoint->fields.acpi_info;
		for0(index, info->serial_resource_count) {
			const auto& resource = info->serial_resources[index];
			if (resource.bus_type != DeviceAcpiSerialBusType::I2C) continue;
			const uint16 maximum_address = resource.type_flags & AcpiI2cTypeFlagTenBitAddress ? 0x3FFu : 0x7Fu;
			if (resource.address <= maximum_address) return &resource;
		}
		return nullptr;
	}

	uint32 SelectHidI2cInputQuirks(uint16 vendor_id, uint16 product_id) {
		// Compatibility evidence: Linux drivers/hid/i2c-hid/i2c-hid-core.c.
		if (vendor_id == HantickVendor && product_id == HantickProduct5288) return uni::HIDI2CInputQuirk_BadReportLength;
		return uni::HIDI2CInputQuirk_None;
	}

	uint32 SelectHidInputQuirks(uint16 vendor_id, uint16 product_id) {
		// Compatibility evidence: Linux hid-multitouch MT_CLS_WIN_8_FORCE_MULTI_INPUT_NSMU.
		if (vendor_id == HantickVendor && product_id == HantickProduct5288) return HidInputQuirk_PreferTouchpadMotion | HidInputQuirk_IgnoreDuplicateContacts | HidInputQuirk_NotSeenMeansUp;
		// Physical evidence: Hantick 0911:5283 exposes the same split Mouse and Touchpad report behavior.
		if (vendor_id == HantickVendor && product_id == HantickProduct5283) return HidInputQuirk_PreferTouchpadMotion | HidInputQuirk_IgnoreDuplicateContacts | HidInputQuirk_NotSeenMeansUp;
		return HidInputQuirk_None;
	}

	constexpr uint32 HidUsagePageGenericDesktop = 0x01u;
	constexpr uint32 HidUsagePageKeyboard = 0x07u;
	constexpr uint32 HidUsagePageButton = 0x09u;
	constexpr uint32 HidUsagePageDigitizer = 0x0Du;
	constexpr uint32 HidUsageMouse = 0x02u;
	constexpr uint32 HidUsageKeyboard = 0x06u;
	constexpr uint32 HidUsageX = 0x30u;
	constexpr uint32 HidUsageY = 0x31u;
	constexpr uint32 HidUsageWheel = 0x38u;
	constexpr uint32 HidUsageTouchscreen = 0x04u;
	constexpr uint32 HidUsageTouchpad = 0x05u;
	constexpr uint32 HidUsageFinger = 0x22u;
	constexpr uint32 HidUsageTipPressure = 0x30u;
	constexpr uint32 HidUsageInRange = 0x32u;
	constexpr uint32 HidUsageWidth = 0x48u;
	constexpr uint32 HidUsageHeight = 0x49u;
	constexpr uint32 HidUsageConfidence = 0x47u;
	constexpr uint32 HidUsageTipSwitch = 0x42u;
	constexpr uint32 HidUsageContactIdentifier = 0x51u;
	constexpr uint32 HidUsageContactCount = 0x54u;
	constexpr stduint HidInputMaximumObservers = 8;
	constexpr stduint HidInputMaximumContacts = 16;
	constexpr stduint HidInputMaximumKeyboardReports = 8;

	struct HidInputObserverEntry {
		DeviceInputObserver observer;
		void* context;
	};

	struct HidTouchContactState {
		uint16 id;
		uint8 report_id;
		bool active;
		bool seen;
		int32 x;
		int32 y;
		int32 residual_x;
		int32 residual_y;
	};

	struct HidTouchSample {
		stduint collection_index;
		uint16 contact_id;
		bool has_contact_id;
		bool has_tip;
		bool tip;
		bool has_in_range;
		bool in_range;
		bool has_confidence;
		bool confidence;
		bool has_x;
		bool has_y;
		int64 x;
		int64 y;
		int64 x_minimum;
		int64 x_maximum;
		int64 y_minimum;
		int64 y_maximum;
		int32 pressure;
		int32 width;
		int32 height;
	};

	struct HidKeyboardReportState {
		uint8 report_id;
		uint8 modifiers;
		bool used;
		byte keys[28];
	};

	struct HidInputRuntime {
		DeviceNode* endpoint;
		uni::HIDI2CDevice* device;
		const uni::HIDReportLayout* layout;
		byte* input_buffer;
		stduint input_capacity;
		DeviceGPIOInterruptRegistration GPIO_interrupt;
		stduint GPIO_interrupt_index;
		HidI2cActivationState activation_state;
		HidI2cFailure recovery_failure;
		uint8 activation_attempts;
		uint8 input_retry_count;
		uint8 recovery_attempts;
		uint8 empty_interrupt_count;
		uint32 capabilities;
		uint32 input_quirks;
		uint32 deferred_interrupt_count;
		uint32 pending_report_count;
		stduint unknown_field_count;
		uint8 button_usage_count;
		uint8 mouse_button_usage_count;
		uint8 touchpad_button_usage_count;
		bool input_continuation_scheduled;
		bool recovery_scheduled;
		bool level_completion_pending;
		bool wake_interrupt_pending;
		bool ALPS_initialized;
		uni::HIDALPSDeviceState ALPS;
		byte keyboard_scratch[29];
		HidKeyboardReportState keyboard_reports[HidInputMaximumKeyboardReports];
		uint16 button_states[256];
		uint16 touchpad_button_masks[256];
		uint16 active_buttons;
		uint16 primary_contact_id;
		uint8 primary_contact_report_id;
		bool primary_contact_valid;
		HidTouchContactState contacts[HidInputMaximumContacts];
		HidTouchSample samples[HidInputMaximumContacts];
		HidInputRuntime* next;
	};

	static HidInputObserverEntry hid_input_observers[HidInputMaximumObservers]{};
	static stduint hid_input_observer_count = 0;
	static HidInputRuntime* hid_input_runtimes = nullptr;

	bool CollectionHasUsage(const uni::HIDReportLayout& layout, stduint collection_index, uint32 page, uint32 usage) {
		const auto* collections = layout.Collections();
		for0(depth, layout.CollectionCount() + 1) {
			if (collection_index == uni::HIDReportNoCollection || collection_index >= layout.CollectionCount()) return false;
			const auto& collection = collections[collection_index];
			if (collection.usage.page == page && collection.usage.id == usage) return true;
			collection_index = collection.parent_index;
		}
		return false;
	}

	stduint FindFingerCollection(const uni::HIDReportLayout& layout, stduint collection_index) {
		const auto* collections = layout.Collections();
		for0(depth, layout.CollectionCount() + 1) {
			if (collection_index == uni::HIDReportNoCollection || collection_index >= layout.CollectionCount()) return uni::HIDReportNoCollection;
			const auto& collection = collections[collection_index];
			if (collection.usage.page == HidUsagePageDigitizer && collection.usage.id == HidUsageFinger) return collection_index;
			collection_index = collection.parent_index;
		}
		return uni::HIDReportNoCollection;
	}

	bool IsTouchField(const uni::HIDReportLayout& layout, const uni::HIDReportField& field, bool& touchpad, bool& touchscreen) {
		touchpad = CollectionHasUsage(layout, field.collection_index, HidUsagePageDigitizer, HidUsageTouchpad);
		touchscreen = CollectionHasUsage(layout, field.collection_index, HidUsagePageDigitizer, HidUsageTouchscreen);
		return touchpad || touchscreen || FindFingerCollection(layout, field.collection_index) != uni::HIDReportNoCollection;
	}

	bool IsRecognizedInputUsage(const uni::HIDReportLayout& layout, const uni::HIDReportField& field, const uni::HIDUsage& usage) {
		bool touchpad = false;
		bool touchscreen = false;
		const bool touch = IsTouchField(layout, field, touchpad, touchscreen);
		if (usage.page == HidUsagePageKeyboard) return true;
		if (usage.page == HidUsagePageButton) return true;
		if (usage.page == HidUsagePageGenericDesktop && (usage.id == HidUsageX || usage.id == HidUsageY || usage.id == HidUsageWheel)) return true;
		if (touch && usage.page == HidUsagePageDigitizer && (usage.id == HidUsageTipSwitch || usage.id == HidUsageInRange || usage.id == HidUsageConfidence || usage.id == HidUsageContactIdentifier || usage.id == HidUsageContactCount || usage.id == HidUsageTipPressure || usage.id == HidUsageWidth || usage.id == HidUsageHeight)) return true;
		return false;
	}

	void AnalyzeHidInputLayout(HidInputRuntime& runtime) {
		if (!runtime.layout) return;
		const auto& layout = *runtime.layout;
		stduint finger_collections = 0;
		bool has_mouse_collection = false;
		bool has_touchpad_collection = false;
		for0(index, layout.CollectionCount()) {
			const auto& collection = layout.Collections()[index];
			if (collection.usage.page == HidUsagePageDigitizer && collection.usage.id == HidUsageFinger) ++finger_collections;
			if (collection.usage.page == HidUsagePageDigitizer && collection.usage.id == HidUsageTouchpad) {
				runtime.capabilities |= DeviceInputCapability_Touch | DeviceInputCapability_Touchpad;
				has_touchpad_collection = true;
			}
			if (collection.usage.page == HidUsagePageDigitizer && collection.usage.id == HidUsageTouchscreen) runtime.capabilities |= DeviceInputCapability_Touch | DeviceInputCapability_Touchscreen;
			if (collection.usage.page == HidUsagePageGenericDesktop && collection.usage.id == HidUsageMouse) has_mouse_collection = true;
			if (collection.usage.page == HidUsagePageGenericDesktop && collection.usage.id == HidUsageKeyboard) runtime.capabilities |= DeviceInputCapability_Keyboard;
		}
		if (has_mouse_collection && has_touchpad_collection) runtime.input_quirks |= HidInputQuirk_PreferTouchpadMotion;
		if (finger_collections > 1) runtime.capabilities |= DeviceInputCapability_Multitouch;
		for0(field_index, layout.FieldCount()) {
			const auto& field = layout.Fields()[field_index];
			if (field.type != uni::HIDReportType::Input || (field.flags & uni::HIDReportMainFlag_Constant)) continue;
			bool field_recognized = false;
			bool touchpad = false;
			bool touchscreen = false;
			const bool touch = IsTouchField(layout, field, touchpad, touchscreen);
			if (touch) runtime.capabilities |= DeviceInputCapability_Touch;
			if (touchpad) runtime.capabilities |= DeviceInputCapability_Touchpad;
			if (touchscreen) runtime.capabilities |= DeviceInputCapability_Touchscreen;
			for0(element, field.report_count) {
				uni::HIDUsage usage{};
				if (!layout.ResolveUsage(field, element, usage)) continue;
				if (usage.page == HidUsagePageKeyboard) runtime.capabilities |= DeviceInputCapability_Keyboard;
				if (usage.page == HidUsagePageButton) {
					runtime.capabilities |= DeviceInputCapability_Button;
					if (usage.id <= 16u && usage.id > runtime.button_usage_count) runtime.button_usage_count = uint8(usage.id);
					if (usage.id <= 16u && CollectionHasUsage(layout, field.collection_index, HidUsagePageGenericDesktop, HidUsageMouse) && usage.id > runtime.mouse_button_usage_count) runtime.mouse_button_usage_count = uint8(usage.id);
					if (usage.id <= 16u && IsTouchField(layout, field, touchpad, touchscreen) && touchpad) {
						if (usage.id > runtime.touchpad_button_usage_count) runtime.touchpad_button_usage_count = uint8(usage.id);
						runtime.touchpad_button_masks[field.report_id] |= uint16(1u << (usage.id - 1u));
					}
				}
				if (usage.page == HidUsagePageGenericDesktop && usage.id == HidUsageWheel) runtime.capabilities |= DeviceInputCapability_Wheel;
				if (touch && usage.page == HidUsagePageDigitizer && usage.id == HidUsageContactCount && field.logical_maximum > 1) runtime.capabilities |= DeviceInputCapability_Multitouch;
				if (usage.page == HidUsagePageGenericDesktop && (usage.id == HidUsageX || usage.id == HidUsageY)) {
					if (field.flags & uni::HIDReportMainFlag_Relative) runtime.capabilities |= DeviceInputCapability_RelativePointer;
					else if (!touch) runtime.capabilities |= DeviceInputCapability_AbsolutePointer;
				}
				field_recognized = field_recognized || IsRecognizedInputUsage(layout, field, usage);
			}
			if (!field_recognized) {
				++runtime.unknown_field_count;
				runtime.capabilities |= DeviceInputCapability_Raw;
			}
		}
	}

	HidInputRuntime* FindHidInputRuntime(const DeviceNode* endpoint) {
		for (auto* runtime = hid_input_runtimes; runtime; runtime = runtime->next) {
			if (runtime->endpoint == endpoint) return runtime;
		}
		return nullptr;
	}

	void EmitHidInputEvent(HidInputRuntime& runtime, const DeviceInputEvent& event) {
		for0(index, hid_input_observer_count) hid_input_observers[index].observer(runtime.endpoint, event, hid_input_observers[index].context);
	}

	int32 ClampInputValue(int64 value) {
		if (value > 0x7FFFFFFFll) return 0x7FFFFFFF;
		if (value < -0x7FFFFFFFll - 1) return -0x7FFFFFFF - 1;
		return int32(value);
	}

	uint16 AggregateHidButtons(const HidInputRuntime& runtime) {
		uint16 buttons = 0;
		for0(report_id, 256) buttons |= runtime.button_states[report_id];
		return buttons;
	}

	void ReleaseIdleTouchpadButtons(HidInputRuntime& runtime, uint8 report_id) {
		for0(index, HidInputMaximumContacts) if (runtime.contacts[index].active) return;
		const uint16 previous_buttons = runtime.active_buttons;
		for0(index, 256) runtime.button_states[index] &= ~runtime.touchpad_button_masks[index];
		runtime.active_buttons = AggregateHidButtons(runtime);
		const uint16 changed = previous_buttons ^ runtime.active_buttons;
		if (!changed) return;
		for0(bit, 16) {
			if (!(changed & (1u << bit))) continue;
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::Button;
			event.report_id = report_id;
			event.usage_page = HidUsagePageButton;
			event.usage = bit + 1u;
			event.buttons = runtime.active_buttons;
			EmitHidInputEvent(runtime, event);
		}
		DeviceInputEvent pointer_event{};
		pointer_event.type = DeviceInputEventType::RelativePointer;
		pointer_event.report_id = report_id;
		pointer_event.buttons = runtime.active_buttons;
		EmitHidInputEvent(runtime, pointer_event);
	}

	bool IsDuplicateTouchSample(const HidInputRuntime& runtime, stduint sample_index, uint16 contact_id) {
		if (!(runtime.input_quirks & HidInputQuirk_IgnoreDuplicateContacts)) return false;
		for0(index, sample_index) {
			const auto& sample = runtime.samples[index];
			if (sample.collection_index == uni::HIDReportNoCollection || (!sample.has_x && !sample.has_y && !sample.has_tip && !sample.has_in_range)) continue;
			if ((sample.has_contact_id ? sample.contact_id : uint16(index)) == contact_id) return true;
		}
		return false;
	}

	int32 NormalizeInputCoordinate(int64 value, int64 minimum, int64 maximum) {
		if (maximum <= minimum) return 0;
		if (value < minimum) value = minimum;
		if (value > maximum) value = maximum;
		const uint64 distance = uint64(value - minimum);
		const uint64 range = uint64(maximum - minimum);
		if (distance <= ~uint64(0) / 65535u) return int32(distance * 65535u / range);
		const uint64 divisor = range / 65535u + (range % 65535u ? 1u : 0u);
		return divisor ? int32(distance / divisor) : 0;
	}

	HidTouchSample* FindTouchSample(HidInputRuntime& runtime, stduint collection_index) {
		for0(index, HidInputMaximumContacts) {
			if (runtime.samples[index].collection_index == collection_index) return &runtime.samples[index];
			if (runtime.samples[index].collection_index == uni::HIDReportNoCollection) {
				runtime.samples[index].collection_index = collection_index;
				return &runtime.samples[index];
			}
		}
		return nullptr;
	}

	HidTouchContactState* FindTouchContact(HidInputRuntime& runtime, uint16 contact_id, uint8 report_id, bool allocate) {
		HidTouchContactState* free_contact = nullptr;
		for0(index, HidInputMaximumContacts) {
			auto& contact = runtime.contacts[index];
			if (contact.active && contact.id == contact_id && contact.report_id == report_id) return &contact;
			if (!contact.active && !free_contact) free_contact = &contact;
		}
		if (!allocate || !free_contact) return nullptr;
		*free_contact = {};
		free_contact->id = contact_id;
		free_contact->report_id = report_id;
		return free_contact;
	}

	HidKeyboardReportState* FindKeyboardReportState(HidInputRuntime& runtime, uint8 report_id) {
		HidKeyboardReportState* free_state = nullptr;
		for0(index, HidInputMaximumKeyboardReports) {
			auto& state = runtime.keyboard_reports[index];
			if (state.used && state.report_id == report_id) return &state;
			if (!state.used && !free_state) free_state = &state;
		}
		if (!free_state) return nullptr;
		*free_state = {};
		free_state->used = true;
		free_state->report_id = report_id;
		return free_state;
	}

	void EmitKeyboardEvents(HidInputRuntime& runtime, uint8 report_id) {
		auto* state = FindKeyboardReportState(runtime, report_id);
		if (!state) return;
		if (state->modifiers != runtime.keyboard_scratch[28]) {
			const uint8 changed = state->modifiers ^ runtime.keyboard_scratch[28];
			state->modifiers = runtime.keyboard_scratch[28];
			for0(bit, 8) {
				if (!(changed & (1u << bit))) continue;
				DeviceInputEvent event{};
				event.type = DeviceInputEventType::Key;
				event.report_id = report_id;
				event.flags = state->modifiers & (1u << bit) ? DeviceInputEventFlag_Pressed : DeviceInputEventFlag_None;
				event.usage_page = HidUsagePageKeyboard;
				event.usage = 0xE0u + bit;
				event.value = event.flags & DeviceInputEventFlag_Pressed ? 1 : 0;
				event.modifiers = state->modifiers;
				EmitHidInputEvent(runtime, event);
			}
		}
		for0(usage, 224) {
			const byte mask = byte(1u << (usage & 7u));
			const bool previous = state->keys[usage >> 3] & mask;
			const bool current = runtime.keyboard_scratch[usage >> 3] & mask;
			if (previous == current) continue;
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::Key;
			event.report_id = report_id;
			event.flags = current ? DeviceInputEventFlag_Pressed : DeviceInputEventFlag_None;
			event.usage_page = HidUsagePageKeyboard;
			event.usage = usage;
			event.value = current ? 1 : 0;
			event.modifiers = state->modifiers;
			EmitHidInputEvent(runtime, event);
		}
		for0(index, 28) state->keys[index] = runtime.keyboard_scratch[index];
	}

	bool DecodeHIDALPSInputReport(HidInputRuntime& runtime, const byte* report, stduint length) {
		uni::HIDALPSInputReport decoded{};
		const auto status = uni::DecodeHIDALPSInput(runtime.ALPS, report, length, decoded);
		if (status == uni::HIDALPSStatus::NotHandled) return false;
		if (status != uni::HIDALPSStatus::Success) return false;
		constexpr uint8 contact_state_report_id = 0xFFu;
		uint16 active_buttons = runtime.button_states[contact_state_report_id];
		const uint16 current_buttons = decoded.buttons;
		const uint16 changed_buttons = active_buttons ^ current_buttons;
		for0(bit, 3) {
			if (!(changed_buttons & (1u << bit))) continue;
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::Button;
			event.report_id = decoded.report_id;
			event.flags = current_buttons & (1u << bit) ? DeviceInputEventFlag_Pressed : DeviceInputEventFlag_None;
			event.usage_page = HidUsagePageButton;
			event.usage = bit + 1u;
			event.value = event.flags & DeviceInputEventFlag_Pressed ? 1 : 0;
			event.buttons = current_buttons;
			EmitHidInputEvent(runtime, event);
		}
		active_buttons = current_buttons;
		runtime.button_states[contact_state_report_id] = current_buttons;
		runtime.active_buttons = current_buttons;
		if (decoded.pointing_stick) {
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::RelativePointer;
			event.report_id = decoded.report_id;
			event.x = decoded.relative_x;
			event.y = decoded.relative_y;
			event.buttons = active_buttons;
			EmitHidInputEvent(runtime, event);
			return true;
		}
		bool pointer_emitted = false;
		for0(index, uni::HIDALPSMaximumContacts) {
			const auto& sample = decoded.contacts[index];
			auto* contact = FindTouchContact(runtime, uint16(index), contact_state_report_id, sample.active);
			if (!contact) continue;
			const bool was_active = contact->active;
			const int32 normalized_x = NormalizeInputCoordinate(sample.x, runtime.ALPS.x_minimum, runtime.ALPS.x_maximum);
			const int32 normalized_y = NormalizeInputCoordinate(sample.y, runtime.ALPS.y_minimum, runtime.ALPS.y_maximum);
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::Touch;
			event.report_id = decoded.report_id;
			event.flags = DeviceInputEventFlag_Touchpad;
			if (sample.active && !was_active) event.flags |= DeviceInputEventFlag_Begin;
			if (!sample.active && was_active) event.flags |= DeviceInputEventFlag_End;
			event.x = normalized_x;
			event.y = normalized_y;
			event.pressure = sample.pressure;
			event.contact_id = uint16(index);
			event.contact_count = decoded.contact_count;
			event.buttons = active_buttons;
			EmitHidInputEvent(runtime, event);
			if (sample.active) {
				if (!runtime.primary_contact_valid) {
					runtime.primary_contact_id = uint16(index);
					runtime.primary_contact_report_id = contact_state_report_id;
					runtime.primary_contact_valid = true;
				} else if (runtime.primary_contact_id == index && runtime.primary_contact_report_id == contact_state_report_id && was_active) {
					DeviceInputEvent pointer_event{};
					pointer_event.type = DeviceInputEventType::RelativePointer;
					pointer_event.report_id = decoded.report_id;
					const int32 delta_x = normalized_x - contact->x + contact->residual_x;
					const int32 delta_y = normalized_y - contact->y + contact->residual_y;
					pointer_event.x = delta_x / 256;
					pointer_event.y = delta_y / 256;
					contact->residual_x = delta_x - pointer_event.x * 256;
					contact->residual_y = delta_y - pointer_event.y * 256;
					pointer_event.buttons = active_buttons;
					if (pointer_event.x || pointer_event.y) {
						EmitHidInputEvent(runtime, pointer_event);
						pointer_emitted = true;
					}
				}
			} else {
				contact->residual_x = 0;
				contact->residual_y = 0;
				if (runtime.primary_contact_valid && runtime.primary_contact_id == index && runtime.primary_contact_report_id == contact_state_report_id) runtime.primary_contact_valid = false;
			}
			contact->active = sample.active;
			contact->seen = sample.active;
			contact->x = normalized_x;
			contact->y = normalized_y;
		}
		if (changed_buttons && !pointer_emitted) {
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::RelativePointer;
			event.report_id = decoded.report_id;
			event.buttons = active_buttons;
			EmitHidInputEvent(runtime, event);
		}
		return true;
	}

	bool DecodeHidInputReport(HidInputRuntime& runtime, const byte* report, stduint length) {
		if (!runtime.layout || !report || !length) return false;
		const auto& layout = *runtime.layout;
		uint8 report_id = 0;
		const auto* definition = layout.FindDefinition(uni::HIDReportType::Input, 0);
		if (!definition) {
			report_id = report[0];
			definition = layout.FindDefinition(uni::HIDReportType::Input, report_id);
		}
		if (!definition || length < definition->payload_bytes + (definition->has_report_id ? 1u : 0u)) return false;
		for0(index, 29) runtime.keyboard_scratch[index] = 0;
		for0(index, HidInputMaximumContacts) {
			runtime.samples[index] = {};
			runtime.samples[index].collection_index = uni::HIDReportNoCollection;
			runtime.contacts[index].seen = false;
		}
		bool has_keyboard = false;
		bool has_relative_pointer = false;
		bool has_absolute_pointer = false;
		bool has_buttons = false;
		bool buttons_changed = false;
		bool has_touch = false;
		bool suppress_compatibility_motion = false;
		bool has_contact_count = false;
		uint16 contact_count = 0;
		uint16 current_buttons = 0;
		const uint16 previous_report_buttons = runtime.button_states[report_id];
		uint16 active_buttons = runtime.active_buttons;
		int64 relative_x = 0;
		int64 relative_y = 0;
		int64 wheel = 0;
		int64 absolute_x = 0;
		int64 absolute_y = 0;
		int64 absolute_x_minimum = 0;
		int64 absolute_x_maximum = 0;
		int64 absolute_y_minimum = 0;
		int64 absolute_y_maximum = 0;
		if ((runtime.input_quirks & HidInputQuirk_PreferTouchpadMotion) && (runtime.capabilities & DeviceInputCapability_Touchpad)) {
			for0(field_index, layout.FieldCount()) {
				const auto& field = layout.Fields()[field_index];
				if (field.type != uni::HIDReportType::Input || field.report_id != report_id || (field.flags & uni::HIDReportMainFlag_Constant)) continue;
				if (CollectionHasUsage(layout, field.collection_index, HidUsagePageGenericDesktop, HidUsageMouse)) {
					suppress_compatibility_motion = true;
					break;
				}
			}
		}
		for0(field_index, layout.FieldCount()) {
			const auto& field = layout.Fields()[field_index];
			if (field.type != uni::HIDReportType::Input || field.report_id != report_id || (field.flags & uni::HIDReportMainFlag_Constant)) continue;
			uni::HIDUsage field_usage{};
			if (layout.ResolveUsage(field, 0, field_usage)) {
				if (field_usage.page == HidUsagePageKeyboard) has_keyboard = true;
			}
			bool touchpad = false;
			bool touchscreen = false;
			const bool touch = IsTouchField(layout, field, touchpad, touchscreen);
			for0(element, field.report_count) {
				uint64 unsigned_value = 0;
				int64 value = 0;
				if (field.logical_minimum < 0) {
					if (layout.ExtractSigned(field, element, report, length, value) != uni::HIDReportValueStatus::Success) return false;
				} else {
					if (layout.ExtractUnsigned(field, element, report, length, unsigned_value) != uni::HIDReportValueStatus::Success) return false;
					value = int64(unsigned_value);
				}
				uni::HIDUsage usage{};
				if (field.flags & uni::HIDReportMainFlag_Variable) {
					if (!layout.ResolveUsage(field, element, usage)) continue;
				} else {
					if (!unsigned_value || !layout.ResolveArrayUsage(field, unsigned_value, usage)) continue;
				}
				if (usage.page == HidUsagePageKeyboard) {
					has_keyboard = true;
					if (usage.id >= 0xE0u && usage.id <= 0xE7u) {
						if (value) runtime.keyboard_scratch[28] |= byte(1u << (usage.id - 0xE0u));
						else runtime.keyboard_scratch[28] &= byte(~(1u << (usage.id - 0xE0u)));
					} else if (usage.id < 224u && value) {
						runtime.keyboard_scratch[usage.id >> 3] |= byte(1u << (usage.id & 7u));
					}
					continue;
				}
				if (usage.page == HidUsagePageButton && usage.id && usage.id <= 16u) {
					const bool compatibility_mouse_button = CollectionHasUsage(layout, field.collection_index, HidUsagePageGenericDesktop, HidUsageMouse);
					if ((runtime.input_quirks & HidInputQuirk_PreferTouchpadMotion) && compatibility_mouse_button && usage.id <= runtime.touchpad_button_usage_count) continue;
					has_buttons = true;
					if (value) current_buttons |= uint16(1u << (usage.id - 1u));
					continue;
				}
				if (touch) {
					has_touch = true;
					stduint collection_index = FindFingerCollection(layout, field.collection_index);
					if (collection_index == uni::HIDReportNoCollection) collection_index = field.collection_index;
					auto* sample = FindTouchSample(runtime, collection_index);
					if (!sample) continue;
					if (usage.page == HidUsagePageDigitizer && usage.id == HidUsageContactIdentifier) { sample->contact_id = uint16(value); sample->has_contact_id = true; }
					else if (usage.page == HidUsagePageDigitizer && usage.id == HidUsageTipSwitch) { sample->tip = value != 0; sample->has_tip = true; }
					else if (usage.page == HidUsagePageDigitizer && usage.id == HidUsageInRange) { sample->in_range = value != 0; sample->has_in_range = true; }
					else if (usage.page == HidUsagePageDigitizer && usage.id == HidUsageConfidence) { sample->confidence = value != 0; sample->has_confidence = true; }
					else if (usage.page == HidUsagePageDigitizer && usage.id == HidUsageContactCount) { contact_count = uint16(value); has_contact_count = true; }
					else if (usage.page == HidUsagePageDigitizer && usage.id == HidUsageTipPressure) sample->pressure = ClampInputValue(value);
					else if (usage.page == HidUsagePageDigitizer && usage.id == HidUsageWidth) sample->width = ClampInputValue(value);
					else if (usage.page == HidUsagePageDigitizer && usage.id == HidUsageHeight) sample->height = ClampInputValue(value);
					else if (usage.page == HidUsagePageGenericDesktop && usage.id == HidUsageX) { sample->x = value; sample->x_minimum = field.logical_minimum; sample->x_maximum = field.logical_maximum; sample->has_x = true; }
					else if (usage.page == HidUsagePageGenericDesktop && usage.id == HidUsageY) { sample->y = value; sample->y_minimum = field.logical_minimum; sample->y_maximum = field.logical_maximum; sample->has_y = true; }
					continue;
				}
				if (suppress_compatibility_motion && CollectionHasUsage(layout, field.collection_index, HidUsagePageGenericDesktop, HidUsageMouse) && usage.page == HidUsagePageGenericDesktop && (usage.id == HidUsageX || usage.id == HidUsageY || usage.id == HidUsageWheel)) continue;
				if (usage.page == HidUsagePageGenericDesktop && usage.id == HidUsageX) {
					if (field.flags & uni::HIDReportMainFlag_Relative) { relative_x += value; has_relative_pointer = true; }
					else { absolute_x = value; absolute_x_minimum = field.logical_minimum; absolute_x_maximum = field.logical_maximum; has_absolute_pointer = true; }
				} else if (usage.page == HidUsagePageGenericDesktop && usage.id == HidUsageY) {
					if (field.flags & uni::HIDReportMainFlag_Relative) { relative_y += value; has_relative_pointer = true; }
					else { absolute_y = value; absolute_y_minimum = field.logical_minimum; absolute_y_maximum = field.logical_maximum; has_absolute_pointer = true; }
				} else if (usage.page == HidUsagePageGenericDesktop && usage.id == HidUsageWheel) wheel += value;
			}
		}
		if (has_keyboard) EmitKeyboardEvents(runtime, report_id);
		if (has_buttons) {
			const uint16 changed = previous_report_buttons ^ current_buttons;
			buttons_changed = changed != 0;
			runtime.button_states[report_id] = current_buttons;
			runtime.active_buttons = AggregateHidButtons(runtime);
			active_buttons = runtime.active_buttons;
			for0(bit, 16) {
				if (!(changed & (1u << bit))) continue;
				DeviceInputEvent event{};
				event.type = DeviceInputEventType::Button;
				event.report_id = report_id;
				event.flags = current_buttons & (1u << bit) ? DeviceInputEventFlag_Pressed : DeviceInputEventFlag_None;
				event.usage_page = HidUsagePageButton;
				event.usage = bit + 1u;
				event.value = event.flags & DeviceInputEventFlag_Pressed ? 1 : 0;
				event.buttons = active_buttons;
				EmitHidInputEvent(runtime, event);
			}
		}
		if (has_relative_pointer || (buttons_changed && (runtime.capabilities & (DeviceInputCapability_RelativePointer | DeviceInputCapability_Touchpad)))) {
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::RelativePointer;
			event.report_id = report_id;
			event.x = ClampInputValue(relative_x);
			event.y = ClampInputValue(relative_y);
			event.buttons = active_buttons;
			EmitHidInputEvent(runtime, event);
		}
		if (wheel) {
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::Wheel;
			event.report_id = report_id;
			event.usage_page = HidUsagePageGenericDesktop;
			event.usage = HidUsageWheel;
			event.value = ClampInputValue(wheel);
			event.buttons = active_buttons;
			EmitHidInputEvent(runtime, event);
		}
		if (has_absolute_pointer) {
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::AbsolutePointer;
			event.report_id = report_id;
			event.x = NormalizeInputCoordinate(absolute_x, absolute_x_minimum, absolute_x_maximum);
			event.y = NormalizeInputCoordinate(absolute_y, absolute_y_minimum, absolute_y_maximum);
			event.buttons = active_buttons;
			EmitHidInputEvent(runtime, event);
		}
		if (has_touch) {
			uint16 emitted_contacts = 0;
			for0(index, HidInputMaximumContacts) {
				auto& sample = runtime.samples[index];
				if (sample.collection_index == uni::HIDReportNoCollection || (!sample.has_x && !sample.has_y && !sample.has_tip && !sample.has_in_range)) continue;
				const uint16 contact_id = sample.has_contact_id ? sample.contact_id : uint16(index);
				if (IsDuplicateTouchSample(runtime, index, contact_id)) continue;
				const bool active = sample.has_tip ? sample.tip : sample.has_in_range ? sample.in_range : sample.has_x && sample.has_y;
				auto* contact = FindTouchContact(runtime, contact_id, report_id, active);
				if (!contact) continue;
				const bool was_active = contact->active;
				contact->seen = active;
				const int32 normalized_x = sample.has_x ? NormalizeInputCoordinate(sample.x, sample.x_minimum, sample.x_maximum) : contact->x;
				const int32 normalized_y = sample.has_y ? NormalizeInputCoordinate(sample.y, sample.y_minimum, sample.y_maximum) : contact->y;
				DeviceInputEvent event{};
				event.type = DeviceInputEventType::Touch;
				event.report_id = report_id;
				event.flags = runtime.capabilities & DeviceInputCapability_Touchpad ? DeviceInputEventFlag_Touchpad : DeviceInputEventFlag_Touchscreen;
				if (sample.in_range) event.flags |= DeviceInputEventFlag_InRange;
				if (sample.has_confidence) event.flags |= DeviceInputEventFlag_ConfidenceValid;
				if (sample.confidence) event.flags |= DeviceInputEventFlag_Confidence;
				if (active && !was_active) event.flags |= DeviceInputEventFlag_Begin;
				if (!active && was_active) event.flags |= DeviceInputEventFlag_End;
				event.x = normalized_x;
				event.y = normalized_y;
				event.pressure = sample.pressure;
				event.width = sample.width;
				event.height = sample.height;
				event.contact_id = contact_id;
				event.contact_count = has_contact_count ? contact_count : uint16(active ? 1u : 0u);
				event.buttons = active_buttons;
				EmitHidInputEvent(runtime, event);
				if ((runtime.capabilities & DeviceInputCapability_Touchpad) && active) {
					if (!runtime.primary_contact_valid) {
						runtime.primary_contact_id = contact_id;
						runtime.primary_contact_report_id = report_id;
						runtime.primary_contact_valid = true;
					} else if (runtime.primary_contact_id == contact_id && runtime.primary_contact_report_id == report_id && was_active && (!sample.has_confidence || sample.confidence)) {
						DeviceInputEvent pointer_event{};
						pointer_event.type = DeviceInputEventType::RelativePointer;
						pointer_event.report_id = report_id;
						const int32 delta_x = normalized_x - contact->x + contact->residual_x;
						const int32 delta_y = normalized_y - contact->y + contact->residual_y;
						pointer_event.x = delta_x / 256;
						pointer_event.y = delta_y / 256;
						contact->residual_x = delta_x - pointer_event.x * 256;
						contact->residual_y = delta_y - pointer_event.y * 256;
						pointer_event.buttons = active_buttons;
						if (pointer_event.x || pointer_event.y) EmitHidInputEvent(runtime, pointer_event);
					}
				}
				if (!active) {
					contact->residual_x = 0;
					contact->residual_y = 0;
					if (runtime.primary_contact_valid && runtime.primary_contact_id == contact_id && runtime.primary_contact_report_id == report_id) runtime.primary_contact_valid = false;
				}
				contact->active = active;
				contact->x = normalized_x;
				contact->y = normalized_y;
				if (active) ++emitted_contacts;
			}
			if (has_contact_count || (runtime.input_quirks & HidInputQuirk_NotSeenMeansUp)) {
				for0(index, HidInputMaximumContacts) {
					auto& contact = runtime.contacts[index];
					if (!contact.active || contact.report_id != report_id || contact.seen) continue;
					DeviceInputEvent event{};
					event.type = DeviceInputEventType::Touch;
					event.report_id = report_id;
					event.flags = DeviceInputEventFlag_End | (runtime.capabilities & DeviceInputCapability_Touchpad ? DeviceInputEventFlag_Touchpad : DeviceInputEventFlag_Touchscreen);
					event.x = contact.x;
					event.y = contact.y;
					event.contact_id = contact.id;
					event.contact_count = contact_count;
					EmitHidInputEvent(runtime, event);
					if (runtime.primary_contact_valid && runtime.primary_contact_id == contact.id && runtime.primary_contact_report_id == contact.report_id) runtime.primary_contact_valid = false;
					contact.active = false;
					contact.seen = false;
				}
			}
			ReleaseIdleTouchpadButtons(runtime, report_id);
			if (contact_count > 1 || emitted_contacts > 1) runtime.capabilities |= DeviceInputCapability_Multitouch;
		}
		return true;
	}

	void ReleaseHidInputState(HidInputRuntime& runtime) {
		for0(index, numsof(runtime.keyboard_scratch)) runtime.keyboard_scratch[index] = 0;
		for0(index, HidInputMaximumKeyboardReports) {
			if (runtime.keyboard_reports[index].used) EmitKeyboardEvents(runtime, runtime.keyboard_reports[index].report_id);
		}
		const uint16 previous_buttons = runtime.active_buttons;
		for0(report_id, 256) runtime.button_states[report_id] = 0;
		runtime.active_buttons = 0;
		for0(bit, 16) {
			if (!(previous_buttons & (1u << bit))) continue;
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::Button;
			event.usage_page = HidUsagePageButton;
			event.usage = bit + 1u;
			EmitHidInputEvent(runtime, event);
		}
		if (previous_buttons) {
			DeviceInputEvent event{};
			event.type = DeviceInputEventType::RelativePointer;
			EmitHidInputEvent(runtime, event);
		}
		for0(index, HidInputMaximumContacts) {
			auto& contact = runtime.contacts[index];
			if (contact.active) {
				DeviceInputEvent event{};
				event.type = DeviceInputEventType::Touch;
				event.report_id = contact.report_id;
				event.flags = DeviceInputEventFlag_End | (runtime.capabilities & DeviceInputCapability_Touchpad ? DeviceInputEventFlag_Touchpad : DeviceInputEventFlag_Touchscreen);
				event.x = contact.x;
				event.y = contact.y;
				event.contact_id = contact.id;
				EmitHidInputEvent(runtime, event);
			}
			contact = {};
		}
		runtime.primary_contact_valid = false;
	}

	void RouteMecocoaInput(DeviceNode* endpoint, const DeviceInputEvent& event, void*) {
		#if _GUI_ENABLE && _MCCA == 0x8664
		if (event.type == DeviceInputEventType::Key && event.usage_page == HidUsagePageKeyboard && event.usage <= 0xFFu) {
			keyboard_event_t key{};
			key.mod_val = event.modifiers;
			key.method = event.flags & DeviceInputEventFlag_Pressed ? keyboard_event_t::method_t::keydown : keyboard_event_t::method_t::keyup;
			key.keycode = event.usage >= 0xE0u && event.usage <= 0xE7u ? 0 : uint8(event.usage);
			hand_kboard(key);
			return;
		}
		if (event.type == DeviceInputEventType::RelativePointer) {
			int32 remaining_x = event.x;
			int32 remaining_y = event.y;
			do {
				const int32 x = remaining_x > 127 ? 127 : remaining_x < -127 ? -127 : remaining_x;
				const int32 y = remaining_y > 127 ? 127 : remaining_y < -127 ? -127 : remaining_y;
				MouseMessage mouse{};
				mouse.BtnLeft = (event.buttons & 1u) != 0;
				mouse.BtnRight = (event.buttons & 2u) != 0;
				mouse.BtnMiddle = (event.buttons & 4u) != 0;
				mouse.HIGH = 1;
				mouse.DirX = x < 0;
				mouse.DirY = y < 0;
				mouse.X = int8(x);
				mouse.Y = int8(y);
				hand_mouse_from_device(mouse, endpoint);
				remaining_x -= x;
				remaining_y -= y;
			} while (remaining_x || remaining_y);
		}
		#else
		(void)event;
		#endif
	}

	#if CONFIG_ACPI_BOOT_TEST
	static const byte HidKeyboardReportDescriptor[] = {
		0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
		0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
		0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,
		0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
		0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
		0x81, 0x00, 0xC0,
	};

	static const byte HidMouseReportDescriptor[] = {
		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09,
		0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01,
		0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30,
		0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
		0xC0, 0xC0,
	};

	static const byte HidTouchReportDescriptor[] = {
		0x05, 0x0D, 0x09, 0x04, 0xA1, 0x01, 0x09, 0x22, 0xA1, 0x02, 0x09, 0x42,
		0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, 0x75, 0x07,
		0x95, 0x01, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00,
		0x00, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02, 0xC0, 0xC0,
	};

	static const byte HidMultitouchReportDescriptor[] = {
		0x05, 0x0D, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x07,
		0x09, 0x22, 0xA1, 0x02, 0x09, 0x51, 0x15, 0x00, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xC0,
		0x09, 0x22, 0xA1, 0x02, 0x09, 0x51, 0x15, 0x00, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xC0,
		0xC0,
	};

	static const byte HidMouseWheelReportDescriptor[] = {
		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
		0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
		0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x75, 0x05, 0x95, 0x01, 0x81, 0x01,
		0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F,
		0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
	};

	static const byte HidSplitPointerReportDescriptor[] = {
		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
		0x85, 0x01, 0x09, 0x01, 0xA1, 0x00,
		0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02,
		0x09, 0x30, 0x09, 0x31, 0x81, 0x06, 0xC0,
		0x05, 0x09, 0x85, 0x02, 0x19, 0x01, 0x29, 0x01,
		0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
		0x75, 0x07, 0x95, 0x01, 0x81, 0x01, 0xC0,
	};

	static const byte HidMouseTouchpadReportDescriptor[] = {
		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01,
		0x05, 0x09, 0x09, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
		0x75, 0x07, 0x95, 0x01, 0x81, 0x01,
		0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xC0,
		0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01,
		0x85, 0x03, 0x05, 0x09, 0x09, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
		0x75, 0x07, 0x95, 0x01, 0x81, 0x01,
		0x05, 0x0D, 0x85, 0x02, 0x09, 0x22, 0xA1, 0x02,
		0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
		0x75, 0x07, 0x95, 0x01, 0x81, 0x01,
		0x09, 0x51, 0x15, 0x00, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
		0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0x00, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
		0xC0, 0x05, 0x0D, 0x09, 0x54, 0x15, 0x00, 0x25, 0x0A, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xC0,
	};

	static const byte HidAbsolutePointerReportDescriptor[] = {
		0x05, 0x01, 0x09, 0x01, 0xA1, 0x01, 0x09, 0x30, 0x09, 0x31,
		0x16, 0x00, 0x00, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02, 0xC0,
	};

	static const byte HidTouchpadInputReportDescriptor[] = {
		0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01, 0x09, 0x22, 0xA1, 0x02,
		0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
		0x09, 0x47, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x01,
		0x09, 0x51, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
		0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0x00, 0x26, 0xFF, 0x0F,
		0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
		0x05, 0x0D, 0x09, 0x30, 0x09, 0x48, 0x09, 0x49, 0x15, 0x00, 0x26, 0xFF, 0x00,
		0x75, 0x08, 0x95, 0x03, 0x81, 0x02, 0xC0,
		0x05, 0x0D, 0x09, 0x54, 0x15, 0x00, 0x25, 0x0A, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xC0,
	};

	static const byte HidUnknownUsageReportDescriptor[] = {
		0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x15, 0x00, 0x25, 0xFF,
		0x75, 0x08, 0x95, 0x01, 0x09, 0x01, 0x81, 0x02, 0xC0,
	};

	static const byte HidReportIdDescriptor[] = {
		0x05, 0x01, 0x09, 0x00, 0xA1, 0x01,
		0x85, 0x01, 0x09, 0x30, 0x15, 0x00, 0x25, 0xFF, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
		0x85, 0x02, 0x09, 0x31, 0x91, 0x02,
		0x85, 0x03, 0x09, 0x32, 0xB1, 0x02,
		0xC0,
	};

	static const byte HidSignedReportDescriptor[] = {
		0x05, 0x01, 0x09, 0x30, 0x16, 0x00, 0xF8, 0x26, 0xFF, 0x07,
		0x75, 0x0C, 0x95, 0x01, 0x81, 0x02,
	};

	static const byte HidLongItemReportDescriptor[] = {
		0xFE, 0x02, 0x77, 0xAA, 0xBB,
		0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
	};

	static const byte HidPushPopReportDescriptor[] = {
		0x05, 0x01, 0xA4, 0x05, 0x09, 0xB4, 0x09, 0x30,
		0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
	};
	static const byte HidDelimiterReportDescriptor[] = { 0x05, 0x01, 0xA9, 0x01, 0x09, 0x30, 0xA9, 0x00, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02 };
	static const byte HidKeyboardArrayReport[] = { 0, 0, 4, 0, 0, 0, 0, 0 };
	static const byte HidReportIdInput[] = { 1, 0x5A };
	static const byte HidWrongReportIdInput[] = { 2, 0x5A };

	static const byte HidTruncatedReportDescriptor[] = { 0x75 };
	static const byte HidInvalidReportIdDescriptor[] = { 0x85, 0x00 };
	static const byte HidOversizedReportDescriptor[] = { 0x77, 0xFF, 0xFF, 0xFF, 0xFF, 0x97, 0xFF, 0xFF, 0xFF, 0xFF, 0x81, 0x02 };
	static const byte HidReversedUsageRangeDescriptor[] = { 0x05, 0x01, 0x19, 0x05, 0x29, 0x01 };

	bool ParseHidFixture(const byte* descriptor, stduint length, stduint definitions, stduint fields, stduint collections) {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		return layout.Parse(descriptor, length, uni_hostenv_allocator, result) == uni::HIDReportDescriptorStatus::Success && layout.DefinitionCount() == definitions && layout.FieldCount() == fields && layout.CollectionCount() == collections;
	}

	bool TestKeyboardHidLayout() {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		if (layout.Parse(HidKeyboardReportDescriptor, numsof(HidKeyboardReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success || layout.DefinitionCount() != 2 || layout.FieldCount() != 5 || layout.CollectionCount() != 1) return false;
		uint64 selector = 0;
		uni::HIDUsage usage{};
		const auto& array_field = layout.Fields()[4];
		return layout.ExtractUnsigned(array_field, 0, HidKeyboardArrayReport, numsof(HidKeyboardArrayReport), selector) == uni::HIDReportValueStatus::Success && selector == 4 && layout.ResolveArrayUsage(array_field, selector, usage) && usage.page == 7 && usage.id == 4;
	}

	bool TestReportIdHidLayout() {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		if (layout.Parse(HidReportIdDescriptor, numsof(HidReportIdDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success || layout.DefinitionCount() != 3 || layout.FieldCount() != 3 || layout.CollectionCount() != 1) return false;
		uint64 value = 0;
		const auto& field = layout.Fields()[0];
		return layout.ExtractUnsigned(field, 0, HidReportIdInput, numsof(HidReportIdInput), value) == uni::HIDReportValueStatus::Success && value == 0x5Au && layout.ExtractUnsigned(field, 0, HidWrongReportIdInput, numsof(HidWrongReportIdInput), value) == uni::HIDReportValueStatus::ReportIdMismatch;
	}

	bool TestSignedHidField() {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		if (layout.Parse(HidSignedReportDescriptor, numsof(HidSignedReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success || layout.FieldCount() != 1) return false;
		const byte report[] = { 0xFF, 0x0F };
		int64 value = 0;
		uni::HIDUsage usage{};
		return layout.ExtractSigned(layout.Fields()[0], 0, report, numsof(report), value) == uni::HIDReportValueStatus::Success && value == -1 && layout.ResolveUsage(layout.Fields()[0], 0, usage) && usage.page == 1 && usage.id == 0x30;
	}

	bool TestInvalidHidDescriptors() {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		if (layout.Parse(HidTruncatedReportDescriptor, numsof(HidTruncatedReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::TruncatedItem) return false;
		if (layout.Parse(HidInvalidReportIdDescriptor, numsof(HidInvalidReportIdDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::InvalidReportId) return false;
		if (layout.Parse(HidOversizedReportDescriptor, numsof(HidOversizedReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::ReportTooLarge) return false;
		return layout.Parse(HidReversedUsageRangeDescriptor, numsof(HidReversedUsageRangeDescriptor), uni_hostenv_allocator, result) == uni::HIDReportDescriptorStatus::InvalidUsageRange;
	}

	struct HidInputTestCapture {
		stduint key_count;
		stduint relative_count;
		stduint absolute_count;
		stduint touch_count;
		stduint begin_count;
		stduint end_count;
		stduint button_count;
		stduint wheel_count;
		stduint metadata_count;
		uint16 relative_buttons;
	};

	void CaptureHidInputTestEvent(DeviceNode*, const DeviceInputEvent& event, void* context) {
		auto* capture = static_cast<HidInputTestCapture*>(context);
		if (!capture) return;
		switch (event.type) {
		case DeviceInputEventType::Key: ++capture->key_count; break;
		case DeviceInputEventType::RelativePointer:
			++capture->relative_count;
			capture->relative_buttons = event.buttons;
			break;
		case DeviceInputEventType::AbsolutePointer: ++capture->absolute_count; break;
		case DeviceInputEventType::Touch:
			++capture->touch_count;
			if (event.flags & DeviceInputEventFlag_Begin) ++capture->begin_count;
			if (event.flags & DeviceInputEventFlag_End) ++capture->end_count;
			if ((event.flags & DeviceInputEventFlag_ConfidenceValid) && event.pressure && event.width && event.height) ++capture->metadata_count;
			break;
		case DeviceInputEventType::Button: ++capture->button_count; break;
		case DeviceInputEventType::Wheel: ++capture->wheel_count; break;
		default: break;
		}
	}

	HidInputRuntime* AllocateHidInputTestRuntime(const uni::HIDReportLayout& layout) {
		void* storage = uni_hostenv_allocator->allocate(sizeof(HidInputRuntime), alignof(HidInputRuntime));
		if (!storage) return nullptr;
		auto* runtime = new (storage) HidInputRuntime{};
		runtime->layout = &layout;
		AnalyzeHidInputLayout(*runtime);
		return runtime;
	}

	void ReleaseHidInputTestRuntime(HidInputRuntime* runtime) {
		if (!runtime) return;
		runtime->~HidInputRuntime();
		uni_hostenv_allocator->deallocate(runtime, sizeof(HidInputRuntime));
	}

	bool TestHidKeyboardInput(HidInputTestCapture& capture) {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		if (layout.Parse(HidKeyboardReportDescriptor, numsof(HidKeyboardReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success) return false;
		auto* runtime = AllocateHidInputTestRuntime(layout);
		if (!runtime) return false;
		const byte press[] = { 1, 0, 4, 0, 0, 0, 0, 0 };
		const byte release[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		const bool passed = (runtime->capabilities & DeviceInputCapability_Keyboard) && DecodeHidInputReport(*runtime, press, numsof(press)) && DecodeHidInputReport(*runtime, release, numsof(release)) && capture.key_count == 4;
		ReleaseHidInputTestRuntime(runtime);
		return passed;
	}

	bool TestHidPointerInput(HidInputTestCapture& capture) {
		uni::HIDReportLayout relative_layout;
		uni::HIDReportLayout absolute_layout;
		uni::HIDReportParseResult result{};
		if (relative_layout.Parse(HidMouseWheelReportDescriptor, numsof(HidMouseWheelReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success) return false;
		if (absolute_layout.Parse(HidAbsolutePointerReportDescriptor, numsof(HidAbsolutePointerReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success) return false;
		auto* relative_runtime = AllocateHidInputTestRuntime(relative_layout);
		auto* absolute_runtime = AllocateHidInputTestRuntime(absolute_layout);
		if (!relative_runtime || !absolute_runtime) {
			ReleaseHidInputTestRuntime(relative_runtime);
			ReleaseHidInputTestRuntime(absolute_runtime);
			return false;
		}
		const byte relative_report[] = { 1, 5, 0xFD, 1 };
		const byte absolute_report[] = { 0x00, 0x08, 0x00, 0x04 };
		const bool capabilities = (relative_runtime->capabilities & (DeviceInputCapability_RelativePointer | DeviceInputCapability_Button | DeviceInputCapability_Wheel)) == (DeviceInputCapability_RelativePointer | DeviceInputCapability_Button | DeviceInputCapability_Wheel) && (absolute_runtime->capabilities & DeviceInputCapability_AbsolutePointer);
		const bool passed = capabilities && DecodeHidInputReport(*relative_runtime, relative_report, numsof(relative_report)) && DecodeHidInputReport(*absolute_runtime, absolute_report, numsof(absolute_report)) && capture.relative_count == 1 && capture.button_count == 1 && capture.wheel_count == 1 && capture.absolute_count == 1;
		ReleaseHidInputTestRuntime(relative_runtime);
		ReleaseHidInputTestRuntime(absolute_runtime);
		return passed;
	}

	bool TestHidSplitReportButtons(HidInputTestCapture& capture) {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		if (layout.Parse(HidSplitPointerReportDescriptor, numsof(HidSplitPointerReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success) return false;
		auto* runtime = AllocateHidInputTestRuntime(layout);
		if (!runtime) return false;
		const byte press[] = { 2u, 1u };
		const byte movement[] = { 1u, 5u, 0u };
		const byte release[] = { 2u, 0u };
		const bool pressed = DecodeHidInputReport(*runtime, press, numsof(press)) && runtime->active_buttons == 1u;
		const bool moved = DecodeHidInputReport(*runtime, movement, numsof(movement)) && capture.relative_buttons == 1u;
		const bool released = DecodeHidInputReport(*runtime, release, numsof(release)) && runtime->active_buttons == 0u;
		ReleaseHidInputTestRuntime(runtime);
		return pressed && moved && released;
	}

	bool TestHidTouchpadInputQuirks(HidInputTestCapture& capture) {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		if (layout.Parse(HidMouseTouchpadReportDescriptor, numsof(HidMouseTouchpadReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success) return false;
		auto* runtime = AllocateHidInputTestRuntime(layout);
		if (!runtime) return false;
		const uint32 expected_quirks = HidInputQuirk_PreferTouchpadMotion | HidInputQuirk_IgnoreDuplicateContacts | HidInputQuirk_NotSeenMeansUp;
		runtime->input_quirks = expected_quirks;
		const byte compatibility_motion[] = { 1u, 0u, 12u, 8u };
		const byte compatibility_press[] = { 1u, 1u, 0u, 0u };
		const byte touchpad_press[] = { 3u, 1u };
		const byte touchpad_release[] = { 3u, 0u };
		const byte touch_begin[] = { 2u, 1u, 3u, 0x00u, 0x04u, 0x00u, 0x04u, 1u };
		const byte touch_move[] = { 2u, 1u, 3u, 0x00u, 0x05u, 0x00u, 0x04u, 1u };
		const byte touch_end[] = { 2u, 0u, 3u, 0x00u, 0x05u, 0x00u, 0x04u, 0u };
		const bool motion_suppressed = DecodeHidInputReport(*runtime, compatibility_motion, numsof(compatibility_motion)) && capture.relative_count == 0;
		const bool duplicate_button_suppressed = DecodeHidInputReport(*runtime, compatibility_press, numsof(compatibility_press)) && runtime->active_buttons == 0u && capture.relative_count == 0u;
		const bool pressed = DecodeHidInputReport(*runtime, touchpad_press, numsof(touchpad_press)) && runtime->active_buttons == 1u && capture.relative_count == 1u;
		const bool touch = DecodeHidInputReport(*runtime, touch_begin, numsof(touch_begin)) && DecodeHidInputReport(*runtime, touch_move, numsof(touch_move)) && capture.relative_count == 2u && capture.relative_buttons == 1u;
		const bool contact_release = DecodeHidInputReport(*runtime, touch_end, numsof(touch_end)) && runtime->active_buttons == 0u && capture.relative_count == 3u && capture.relative_buttons == 0u;
		const bool late_button_release = DecodeHidInputReport(*runtime, touchpad_release, numsof(touchpad_release)) && runtime->active_buttons == 0u;
		runtime->samples[0].collection_index = 1u;
		runtime->samples[0].has_contact_id = true;
		runtime->samples[0].contact_id = 7u;
		runtime->samples[0].has_tip = true;
		runtime->samples[0].tip = true;
		runtime->samples[1].collection_index = 2u;
		runtime->samples[1].has_contact_id = true;
		runtime->samples[1].contact_id = 7u;
		runtime->samples[1].has_tip = true;
		runtime->samples[1].tip = true;
		const bool duplicate = IsDuplicateTouchSample(*runtime, 1u, 7u) && !IsDuplicateTouchSample(*runtime, 1u, 8u);
		ReleaseHidInputTestRuntime(runtime);
		const bool selected = SelectHidInputQuirks(HantickVendor, HantickProduct5283) == expected_quirks && SelectHidInputQuirks(HantickVendor, HantickProduct5288) == expected_quirks && SelectHidInputQuirks(HantickVendor, 0u) == HidInputQuirk_None;
		return motion_suppressed && duplicate_button_suppressed && pressed && touch && contact_release && late_button_release && duplicate && selected;
	}

	bool TestHidTouchInput(HidInputTestCapture& capture, bool& multitouch) {
		multitouch = false;
		uni::HIDReportLayout touchpad_layout;
		uni::HIDReportLayout multitouch_layout;
		uni::HIDReportParseResult result{};
		if (touchpad_layout.Parse(HidTouchpadInputReportDescriptor, numsof(HidTouchpadInputReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success) return false;
		if (multitouch_layout.Parse(HidMultitouchReportDescriptor, numsof(HidMultitouchReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success) return false;
		auto* touchpad_runtime = AllocateHidInputTestRuntime(touchpad_layout);
		auto* multitouch_runtime = AllocateHidInputTestRuntime(multitouch_layout);
		if (!touchpad_runtime || !multitouch_runtime) {
			ReleaseHidInputTestRuntime(touchpad_runtime);
			ReleaseHidInputTestRuntime(multitouch_runtime);
			return false;
		}
		const byte begin[] = { 3, 3, 0x00, 0x04, 0x00, 0x02, 20, 10, 12, 1 };
		const byte update[] = { 3, 3, 0x00, 0x05, 0x00, 0x03, 21, 11, 13, 1 };
		const byte end[] = { 2, 3, 0x00, 0x05, 0x00, 0x03, 0, 0, 0, 0 };
		multitouch = (multitouch_runtime->capabilities & (DeviceInputCapability_Touch | DeviceInputCapability_Multitouch | DeviceInputCapability_Touchscreen)) == (DeviceInputCapability_Touch | DeviceInputCapability_Multitouch | DeviceInputCapability_Touchscreen);
		const bool capabilities = (touchpad_runtime->capabilities & (DeviceInputCapability_Touch | DeviceInputCapability_Touchpad)) == (DeviceInputCapability_Touch | DeviceInputCapability_Touchpad);
		const bool passed = capabilities && DecodeHidInputReport(*touchpad_runtime, begin, numsof(begin)) && DecodeHidInputReport(*touchpad_runtime, update, numsof(update)) && DecodeHidInputReport(*touchpad_runtime, end, numsof(end)) && capture.touch_count == 3 && capture.begin_count == 1 && capture.end_count == 1 && capture.relative_count >= 1 && capture.metadata_count == 2;
		ReleaseHidInputTestRuntime(touchpad_runtime);
		ReleaseHidInputTestRuntime(multitouch_runtime);
		return passed;
	}

	bool TestUnknownHidInputUsage() {
		uni::HIDReportLayout layout;
		uni::HIDReportParseResult result{};
		if (layout.Parse(HidUnknownUsageReportDescriptor, numsof(HidUnknownUsageReportDescriptor), uni_hostenv_allocator, result) != uni::HIDReportDescriptorStatus::Success) return false;
		auto* runtime = AllocateHidInputTestRuntime(layout);
		if (!runtime) return false;
		const byte report[] = { 0x5A };
		const bool passed = runtime->unknown_field_count == 1 && (runtime->capabilities & DeviceInputCapability_Raw) && DecodeHidInputReport(*runtime, report, numsof(report));
		ReleaseHidInputTestRuntime(runtime);
		return passed;
	}

	struct HIDALPSTestTransport {
		uint32 pending_address;
		uint8 device_control;
		uint8 sensor_count_x;
		uint8 sensor_count_y;
		uint8 sensor_pitch_x;
		uint8 sensor_pitch_y;
		uint8 resolution;
		uint8 touchpad_buttons;
		uint8 device_type;
		uint8 pointing_stick_buttons;
	};

	void EncodeHIDI2CTestWord(byte* destination, uint16 value) {
		reinterpret_cast<LitEndian<uint16, true>*>(destination)->set(value);
	}

	uint32 DecodeHIDI2CTestAddress(const byte* source) {
		return reinterpret_cast<const LitEndian<uint32, true>*>(source)->get();
	}

	uint8* FindHIDALPSTestRegister(HIDALPSTestTransport& transport, uint32 address) {
		switch (address) {
		case 0x00800040u: return &transport.device_control;
		case 0x00800043u: return &transport.device_type;
		case 0x00800047u: return &transport.sensor_count_x;
		case 0x00800048u: return &transport.sensor_count_y;
		case 0x00800049u: return &transport.sensor_pitch_x;
		case 0x0080004Au: return &transport.sensor_pitch_y;
		case 0x0080004Eu: return &transport.resolution;
		case 0x00800052u: return &transport.touchpad_buttons;
		case 0x0080009Fu: return &transport.pointing_stick_buttons;
		default: return nullptr;
		}
	}

	uni::IICTransferStatus TransferHIDALPSTest(void* context, uni::IICMessage* messages, stduint count, stduint timeout, uni::IICTransferResult& result) {
		result = {};
		result.failed_message = count;
		auto* transport = static_cast<HIDALPSTestTransport*>(context);
		if (!transport || !messages || !count || !timeout) return uni::IICTransferStatus::InvalidArgument;
		if (count == 2u && messages[0].write_buffer && messages[0].length == 2u && messages[1].read_buffer && messages[1].length == uni::HIDI2CDescriptorLength) {
			if (uni::DecodeHIDI2CWord(messages[0].write_buffer) != 0x0020u) return uni::IICTransferStatus::DataNack;
			byte* descriptor = messages[1].read_buffer;
			for0(index, uni::HIDI2CDescriptorLength) descriptor[index] = 0;
			EncodeHIDI2CTestWord(descriptor + 0u, uni::HIDI2CDescriptorLength);
			EncodeHIDI2CTestWord(descriptor + 2u, uni::HIDI2CProtocolVersion);
			EncodeHIDI2CTestWord(descriptor + 4u, 1u);
			EncodeHIDI2CTestWord(descriptor + 6u, 0x0030u);
			EncodeHIDI2CTestWord(descriptor + 8u, 0x0032u);
			EncodeHIDI2CTestWord(descriptor + 10u, 30u);
			EncodeHIDI2CTestWord(descriptor + 12u, 0xFFFFu);
			EncodeHIDI2CTestWord(descriptor + 14u, 0u);
			EncodeHIDI2CTestWord(descriptor + 16u, 0x0040u);
			EncodeHIDI2CTestWord(descriptor + 18u, 0x0042u);
			EncodeHIDI2CTestWord(descriptor + 20u, uni::HIDALPSVendor);
			EncodeHIDI2CTestWord(descriptor + 22u, 0x1215u);
			EncodeHIDI2CTestWord(descriptor + 24u, 1u);
			messages[0].transferred = messages[0].length;
			messages[1].transferred = messages[1].length;
			result.completed_messages = 2u;
			result.completed_bytes = messages[0].length + messages[1].length;
			return uni::IICTransferStatus::Success;
		}
		if (count == 1u && messages[0].write_buffer && messages[0].length >= 16u && messages[0].write_buffer[8] == 0x05u) {
			const byte* command = messages[0].write_buffer + 9u;
			transport->pending_address = DecodeHIDI2CTestAddress(command + 1u);
			if (command[0] == 0xD2u) {
				auto* target = FindHIDALPSTestRegister(*transport, transport->pending_address);
				if (!target) return uni::IICTransferStatus::DataNack;
				*target = command[5];
			} else if (command[0] != 0xD1u) return uni::IICTransferStatus::DataNack;
			messages[0].transferred = messages[0].length;
			result.completed_messages = 1u;
			result.completed_bytes = messages[0].length;
			return uni::IICTransferStatus::Success;
		}
		if (count == 2u && messages[0].write_buffer && messages[0].length >= 6u && messages[1].read_buffer && messages[1].length == 10u) {
			auto* source = FindHIDALPSTestRegister(*transport, transport->pending_address);
			if (!source) return uni::IICTransferStatus::DataNack;
			for0(index, messages[1].length) messages[1].read_buffer[index] = 0;
			messages[1].read_buffer[0] = 10u;
			messages[1].read_buffer[2] = 0x05u;
			messages[1].read_buffer[8] = *source;
			messages[0].transferred = messages[0].length;
			messages[1].transferred = messages[1].length;
			result.completed_messages = 2u;
			result.completed_bytes = messages[0].length + messages[1].length;
			return uni::IICTransferStatus::Success;
		}
		return uni::IICTransferStatus::InvalidArgument;
	}

	bool TestHIDALPSInitialization() {
		HIDALPSTestTransport transport_model{ 0u, 1u, 20u, 15u, 10u, 10u, 8u, 0x11u, 0u, 0u };
		uni::HIDI2CTransport transport{ &transport_model, TransferHIDALPSTest, 0x2Cu, uni::IICMessageFlag_None };
		void* storage = uni_hostenv_allocator->allocate(sizeof(uni::HIDI2CDevice), alignof(uni::HIDI2CDevice));
		if (!storage) return false;
		auto* device = new (storage) uni::HIDI2CDevice(transport, uni_hostenv_allocator);
		uni::HIDI2CResult result{};
		const bool device_initialized = device->Initialize(0x0020u, HidI2cTransferBudget, result) == uni::HIDI2CStatus::Success;
		uni::HIDALPSDeviceState state{};
		const bool protocol_initialized = device_initialized && uni::InitializeHIDALPS(*device, uni::HIDALPSProtocol::U1, HidI2cTransferBudget, state, result) == uni::HIDALPSStatus::Success;
		const bool passed = protocol_initialized && transport_model.device_control == 0x02u && state.x_minimum == 1u && state.x_maximum == 608u && state.y_minimum == 1u && state.y_maximum == 448u && state.button_count == 1u && !state.pointing_stick;
		device->~HIDI2CDevice();
		uni_hostenv_allocator->deallocate(device, sizeof(uni::HIDI2CDevice));
		return passed;
	}

	bool TestHIDALPSInputProtocol(HidInputTestCapture& capture) {
		uni::HIDALPSDeviceState state{};
		state.protocol = uni::HIDALPSProtocol::U1;
		state.x_minimum = 1u;
		state.x_maximum = 4095u;
		state.y_minimum = 1u;
		state.y_maximum = 4095u;
		byte touch_report[28]{};
		touch_report[0] = 0x02u;
		touch_report[1] = 0x01u;
		touch_report[3] = 0x34u;
		touch_report[4] = 0x12u;
		touch_report[5] = 0x78u;
		touch_report[6] = 0x56u;
		touch_report[7] = 0x21u;
		uni::HIDALPSInputReport decoded{};
		const bool touch = uni::DecodeHIDALPSInput(state, touch_report, numsof(touch_report), decoded) == uni::HIDALPSStatus::Success && decoded.report_id == 0x02u && decoded.buttons == 1u && decoded.contact_count == 1u && decoded.contacts[0].x == 0x1234u && decoded.contacts[0].y == 0x5678u && decoded.contacts[0].pressure == 0x21u;
		byte stick_report[6] = { 0x06u, 0x02u, 0x40u, 0x00u, 0xC0u, 0xFFu };
		const bool stick = uni::DecodeHIDALPSInput(state, stick_report, numsof(stick_report), decoded) == uni::HIDALPSStatus::Success && decoded.pointing_stick && decoded.buttons == 2u && decoded.relative_x == 8 && decoded.relative_y == -8;
		const bool match = uni::MatchHIDALPSProtocol(uni::HIDALPSVendor, 0x120Bu) == uni::HIDALPSProtocol::U1 && uni::MatchHIDALPSProtocol(uni::HIDALPSVendor, 0x1215u) == uni::HIDALPSProtocol::U1 && uni::MatchHIDALPSProtocol(uni::HIDALPSVendor, 0x121Eu) == uni::HIDALPSProtocol::U1 && uni::MatchHIDALPSProtocol(0x1234u, 0x1215u) == uni::HIDALPSProtocol::None;
		const bool invalid = uni::DecodeHIDALPSInput(state, touch_report, 27u, decoded) == uni::HIDALPSStatus::InvalidReport;
		void* storage = uni_hostenv_allocator->allocate(sizeof(HidInputRuntime), alignof(HidInputRuntime));
		if (!storage) return false;
		auto* runtime = new (storage) HidInputRuntime{};
		runtime->ALPS = state;
		const bool begin = DecodeHIDALPSInputReport(*runtime, touch_report, numsof(touch_report));
		touch_report[3] = 0xB0u;
		touch_report[4] = 0x04u;
		const bool move = DecodeHIDALPSInputReport(*runtime, touch_report, numsof(touch_report));
		touch_report[1] = 0u;
		touch_report[7] = 0u;
		const bool end = DecodeHIDALPSInputReport(*runtime, touch_report, numsof(touch_report));
		runtime->~HidInputRuntime();
		uni_hostenv_allocator->deallocate(runtime, sizeof(HidInputRuntime));
		return TestHIDALPSInitialization() && touch && stick && match && invalid && begin && move && end && capture.touch_count == 3u && capture.button_count == 2u && capture.relative_count >= 3u && capture.begin_count == 1u && capture.end_count == 1u;
	}

	void RunHidReportTests() {
		const bool keyboard = TestKeyboardHidLayout();
		const bool mouse = ParseHidFixture(HidMouseReportDescriptor, numsof(HidMouseReportDescriptor), 1, 3, 2);
		const bool touch = ParseHidFixture(HidTouchReportDescriptor, numsof(HidTouchReportDescriptor), 1, 3, 2);
		const bool multitouch = ParseHidFixture(HidMultitouchReportDescriptor, numsof(HidMultitouchReportDescriptor), 1, 2, 3);
		const bool report_id = TestReportIdHidLayout();
		const bool signed_value = TestSignedHidField();
		const bool long_item = ParseHidFixture(HidLongItemReportDescriptor, numsof(HidLongItemReportDescriptor), 1, 1, 0);
		const bool push_pop = ParseHidFixture(HidPushPopReportDescriptor, numsof(HidPushPopReportDescriptor), 1, 1, 0);
		const bool delimiter = ParseHidFixture(HidDelimiterReportDescriptor, numsof(HidDelimiterReportDescriptor), 1, 1, 0);
		const bool invalid = TestInvalidHidDescriptors();
		const bool passed = keyboard && mouse && touch && multitouch && report_id && signed_value && long_item && push_pop && delimiter && invalid;
		MCCA_BOOT_TEST_LOG(passed, "[HID-REPORT-TEST] keyboard=%s mouse=%s touch=%s multitouch=%s report-id=%s signed=%s long=%s push-pop=%s delimiter=%s invalid=%s", keyboard ? "success" : "failed", mouse ? "success" : "failed", touch ? "success" : "failed", multitouch ? "success" : "failed", report_id ? "success" : "failed", signed_value ? "success" : "failed", long_item ? "success" : "failed", push_pop ? "success" : "failed", delimiter ? "success" : "failed", invalid ? "success" : "failed");
	}

	void RunHidInputTests() {
		HidInputTestCapture capture{};
		const stduint saved_observer_count = hid_input_observer_count;
		const HidInputObserverEntry saved_observer = hid_input_observers[0];
		hid_input_observer_count = 1;
		hid_input_observers[0] = { CaptureHidInputTestEvent, &capture };
		const bool keyboard = TestHidKeyboardInput(capture);
		capture = {};
		const bool pointer = TestHidPointerInput(capture);
		const bool relative = capture.relative_count == 1;
		const bool absolute = capture.absolute_count == 1;
		const bool button = capture.button_count == 1;
		const bool wheel = capture.wheel_count == 1;
		capture = {};
		bool multitouch = false;
		const bool touch = TestHidTouchInput(capture, multitouch);
		const bool lifecycle = capture.begin_count == 1 && capture.end_count == 1;
		const bool unknown = TestUnknownHidInputUsage();
		capture = {};
		const bool split_report = TestHidSplitReportButtons(capture);
		capture = {};
		const bool touchpad_quirks = TestHidTouchpadInputQuirks(capture);
		capture = {};
		const bool ALPS = TestHIDALPSInputProtocol(capture);
		hid_input_observer_count = saved_observer_count;
		hid_input_observers[0] = saved_observer;
		const bool passed = keyboard && pointer && relative && absolute && touch && multitouch && lifecycle && button && wheel && unknown && split_report && touchpad_quirks && ALPS;
		MCCA_BOOT_TEST_LOG(passed, "[HID-INPUT-TEST] keyboard=%s relative=%s absolute=%s touch=%s multitouch=%s lifecycle=%s button=%s wheel=%s unknown=%s split-report=%s touchpad-quirks=%s alps=%s", keyboard ? "success" : "failed", pointer && relative ? "success" : "failed", pointer && absolute ? "success" : "failed", touch ? "success" : "failed", multitouch ? "success" : "failed", lifecycle ? "success" : "failed", pointer && button ? "success" : "failed", pointer && wheel ? "success" : "failed", unknown ? "success" : "failed", split_report ? "success" : "failed", touchpad_quirks ? "success" : "failed", ALPS ? "success" : "failed");
	}

	stduint CountHidI2cInterruptResources(const DeviceNode* endpoint) {
		if (!endpoint || !endpoint->fields.acpi_info) return 0;
		stduint count = endpoint->fields.acpi_info->irq_resource_count;
		for0(index, endpoint->fields.acpi_info->gpio_resource_count) {
			if (endpoint->fields.acpi_info->gpio_resources[index].type == DeviceAcpiGPIOConnectionType::Interrupt) ++count;
		}
		return count;
	}
	#endif

	stduint FindHidI2cGPIOInterrupt(const DeviceNode* endpoint) {
		if (!endpoint || !endpoint->fields.acpi_info) return ~stduint(0);
		for0(index, endpoint->fields.acpi_info->gpio_resource_count) {
			if (endpoint->fields.acpi_info->gpio_resources[index].type == DeviceAcpiGPIOConnectionType::Interrupt) return index;
		}
		return ~stduint(0);
	}

	uni::IICTransferStatus TransferHidI2c(void* context, uni::IICMessage* messages, stduint count, stduint timeout, uni::IICTransferResult& result) {
		result = {};
		result.failed_message = count;
		auto* endpoint = static_cast<DeviceNode*>(context);
		if (!endpoint || !messages || !count || !timeout) return uni::IICTransferStatus::InvalidArgument;
		DeviceI2cTransferRequest request{ messages, count, timeout, &result, uni::IICTransferStatus::InvalidArgument };
		if (Devsman::Ctrl(endpoint, _IMM(I2cCtrlCommand::Transfer), &request) != 0) return uni::IICTransferStatus::ControllerError;
		return request.status;
	}

	void RecordHidI2cFailure(DeviceNode* endpoint, HidI2cFailure failure, const uni::HIDI2CResult* result = nullptr) {
		if (!endpoint) return;
		endpoint->fields.binding.probe_result = -int32(failure);
		const auto* info = endpoint->fields.acpi_info;
		plogerro("[HID-I2C] endpoint=%s failure=%s status=%u transfer=%u descriptor=%u", info ? info->path : endpoint->link.addr ? endpoint->link.addr : "<unknown>", HidI2cFailureName(failure), result ? stduint(result->status) : 0, result ? stduint(result->transfer_status) : 0, result ? stduint(result->descriptor_status) : 0);
	}

	bool ScheduleHidI2cRecovery(HidInputRuntime& runtime, HidI2cFailure failure);

	bool InitializeHidInputProtocol(HidInputRuntime& runtime) {
		if (runtime.ALPS.protocol == uni::HIDALPSProtocol::None || runtime.ALPS_initialized) return true;
		uni::HIDI2CResult result{};
		uni::HIDALPSDeviceState initialized{};
		if (uni::InitializeHIDALPS(*runtime.device, runtime.ALPS.protocol, HidI2cTransferBudget, initialized, result) != uni::HIDALPSStatus::Success) {
			if (!ScheduleHidI2cRecovery(runtime, HidI2cFailure::ProtocolInitialization)) {
				runtime.activation_state = HidI2cActivationState::Failed;
				RecordHidI2cFailure(runtime.endpoint, HidI2cFailure::RecoverySchedule, &result);
			}
			return false;
		}
		runtime.ALPS = initialized;
		runtime.ALPS_initialized = true;
		return true;
	}

	void ContinueHidI2cInput(pureptr_t timeout, stduint context);
	void ActivateHidI2c(pureptr_t timeout, stduint context);
	void ExpireHidI2cReset(pureptr_t timeout, stduint context);
	void RecoverHidI2c(pureptr_t timeout, stduint context);
	void HandleHidI2cGPIOInterrupt(DeviceNode* endpoint, stduint resource_index, uint32 count, void* context);
	bool CompleteHidI2cLevelInterrupt(HidInputRuntime& runtime);

	void CancelHidI2cDeferredCallbacks(HidInputRuntime& runtime) {
		const stduint context = reinterpret_cast<stduint>(&runtime);
		(void)Systimex::CancelDeferredCallback(context, (_tocall_ft)ContinueHidI2cInput);
		(void)Systimex::CancelDeferredCallback(context, (_tocall_ft)ActivateHidI2c);
		(void)Systimex::CancelDeferredCallback(context, (_tocall_ft)ExpireHidI2cReset);
		(void)Systimex::CancelDeferredCallback(context, (_tocall_ft)RecoverHidI2c);
		runtime.input_continuation_scheduled = false;
		runtime.recovery_scheduled = false;
	}

	bool HidI2cWakeCapable(const HidInputRuntime& runtime) {
		if (!runtime.endpoint || !runtime.endpoint->fields.acpi_info || runtime.GPIO_interrupt_index >= runtime.endpoint->fields.acpi_info->gpio_resource_count) return false;
		return runtime.endpoint->fields.acpi_info->gpio_resources[runtime.GPIO_interrupt_index].wake_capable;
	}

	void UnlinkHidInputRuntime(HidInputRuntime& runtime) {
		HidInputRuntime** link = &hid_input_runtimes;
		while (*link && *link != &runtime) link = &(*link)->next;
		if (*link == &runtime) *link = runtime.next;
		runtime.next = nullptr;
	}

	bool SuspendHidI2cRuntime(HidInputRuntime& runtime, bool preserve_wake) {
		if (runtime.activation_state == HidI2cActivationState::Stopping) return false;
		CancelHidI2cDeferredCallbacks(runtime);
		ReleaseHidInputState(runtime);
		runtime.pending_report_count = 0;
		runtime.input_retry_count = 0;
		runtime.empty_interrupt_count = 0;
		runtime.wake_interrupt_pending = false;
		bool released = true;
		if (runtime.GPIO_interrupt.active && (!preserve_wake || !HidI2cWakeCapable(runtime))) {
			released = Devs::UnregisterGPIOInterrupt(runtime.GPIO_interrupt) == DeviceGPIOStatus::Success;
			runtime.level_completion_pending = false;
		}
		uni::HIDI2CResult result{};
		const bool powered_down = !runtime.device || !runtime.device->IsInitialized() || runtime.device->setPower(uni::HIDI2CPowerState::Sleep, HidI2cTransferBudget, result) == uni::HIDI2CStatus::Success;
		runtime.activation_state = HidI2cActivationState::Suspended;
		return released && powered_down;
	}

	bool StopHidI2cRuntime(HidInputRuntime& runtime, bool failed) {
		auto* endpoint = runtime.endpoint;
		auto* device = runtime.device;
		(void)SuspendHidI2cRuntime(runtime, false);
		runtime.activation_state = HidI2cActivationState::Stopping;
		CancelHidI2cDeferredCallbacks(runtime);
		if (runtime.GPIO_interrupt.active && Devs::UnregisterGPIOInterrupt(runtime.GPIO_interrupt) != DeviceGPIOStatus::Success) {
			runtime.activation_state = HidI2cActivationState::Failed;
			if (endpoint && failed) endpoint->fields.binding.state = static_cast<uint32>(DriverBindingState::Failed);
			return false;
		}
		UnlinkHidInputRuntime(runtime);
		if (endpoint && endpoint->fields.binding.driver_data == device) endpoint->fields.binding.driver_data = nullptr;
		if (endpoint && failed) endpoint->fields.binding.state = static_cast<uint32>(DriverBindingState::Failed);
		if (runtime.input_buffer) uni_hostenv_allocator->deallocate(runtime.input_buffer, runtime.input_capacity);
		runtime.input_buffer = nullptr;
		if (device) {
			device->~HIDI2CDevice();
			uni_hostenv_allocator->deallocate(device, sizeof(uni::HIDI2CDevice));
		}
		runtime.device = nullptr;
		runtime.~HidInputRuntime();
		uni_hostenv_allocator->deallocate(&runtime, sizeof(HidInputRuntime));
		return true;
	}

	void FailHidI2cRuntime(HidInputRuntime& runtime, HidI2cFailure failure, const uni::HIDI2CResult* result = nullptr) {
		RecordHidI2cFailure(runtime.endpoint, failure, result);
		(void)StopHidI2cRuntime(runtime, true);
	}

	void FailHidI2cRecoveryLimit(HidInputRuntime& runtime, const uni::HIDI2CResult* result = nullptr) {
		auto* endpoint = runtime.endpoint;
		if (endpoint) endpoint->fields.binding.probe_result = -int32(HidI2cFailure::RecoveryLimit);
		const auto* info = endpoint ? endpoint->fields.acpi_info : nullptr;
		plogerro("[HID-I2C] endpoint=%s failure=%s cause=%s attempts=%u status=%u transfer=%u descriptor=%u", info ? info->path : endpoint && endpoint->link.addr ? endpoint->link.addr : "<unknown>", HidI2cFailureName(HidI2cFailure::RecoveryLimit), HidI2cFailureName(runtime.recovery_failure), stduint(runtime.recovery_attempts), result ? stduint(result->status) : 0, result ? stduint(result->transfer_status) : 0, result ? stduint(result->descriptor_status) : 0);
		(void)StopHidI2cRuntime(runtime, true);
	}

	bool ScheduleHidI2cInput(HidInputRuntime& runtime, stduint delay) {
		if (runtime.input_continuation_scheduled) return true;
		if (!Systimex::AppendDeferredCallback(delay, reinterpret_cast<stduint>(&runtime), (_tocall_ft)ContinueHidI2cInput)) return false;
		runtime.input_continuation_scheduled = true;
		return true;
	}

	bool ScheduleHidI2cRecovery(HidInputRuntime& runtime, HidI2cFailure failure) {
		if (runtime.activation_state == HidI2cActivationState::Stopping || runtime.activation_state == HidI2cActivationState::Failed) return false;
		if (runtime.activation_state != HidI2cActivationState::Recovering) {
			const stduint context = reinterpret_cast<stduint>(&runtime);
			(void)Systimex::CancelDeferredCallback(context, (_tocall_ft)ContinueHidI2cInput);
			(void)Systimex::CancelDeferredCallback(context, (_tocall_ft)ActivateHidI2c);
			(void)Systimex::CancelDeferredCallback(context, (_tocall_ft)ExpireHidI2cReset);
			runtime.input_continuation_scheduled = false;
			ReleaseHidInputState(runtime);
			runtime.pending_report_count = 0;
			runtime.input_retry_count = 0;
			runtime.activation_state = HidI2cActivationState::Recovering;
		}
		runtime.recovery_failure = failure;
		if (runtime.recovery_scheduled) return true;
		if (!Systimex::AppendDeferredCallback(HidI2cRecoveryDelay, reinterpret_cast<stduint>(&runtime), (_tocall_ft)RecoverHidI2c)) return false;
		runtime.recovery_scheduled = true;
		return true;
	}

	bool ResumeHidI2cRuntime(HidInputRuntime& runtime, HidI2cFailure& failure, uni::HIDI2CResult& result) {
		failure = HidI2cFailure::None;
		if (!runtime.endpoint || !runtime.device || !runtime.endpoint->fields.acpi_info || runtime.GPIO_interrupt_index >= runtime.endpoint->fields.acpi_info->gpio_resource_count) {
			failure = HidI2cFailure::InputRuntime;
			return false;
		}
		if (!runtime.GPIO_interrupt.active) {
			const auto status = Devs::RegisterGPIOInterrupt(runtime.endpoint, runtime.GPIO_interrupt_index, HandleHidI2cGPIOInterrupt, &runtime, runtime.GPIO_interrupt);
			if (status != DeviceGPIOStatus::Success) {
				failure = HidI2cFailure::GPIOInterrupt;
				return false;
			}
		}
		if (runtime.endpoint->fields.acpi_info->gpio_resources[runtime.GPIO_interrupt_index].binding_status != DeviceGPIOBindingStatus::Claimed) {
			failure = HidI2cFailure::GPIOInterrupt;
			return false;
		}
		if (runtime.device->IsResetPending()) (void)runtime.device->ExpireReset(result);
		// Microsoft HID over I2C Protocol Specification 1.0 defines SET_POWER and RESET requests.
		if (runtime.device->setPower(uni::HIDI2CPowerState::On, HidI2cTransferBudget, result) != uni::HIDI2CStatus::Success) {
			failure = HidI2cFailure::Power;
			return false;
		}
		if (runtime.device->BeginReset(HidI2cTransferBudget, result) != uni::HIDI2CStatus::Success) {
			failure = HidI2cFailure::Reset;
			return false;
		}
		runtime.ALPS_initialized = false;
		runtime.activation_state = HidI2cActivationState::ResetPending;
		(void)Systimex::CancelDeferredCallback(reinterpret_cast<stduint>(&runtime), (_tocall_ft)ExpireHidI2cReset);
		runtime.pending_report_count = 0;
		runtime.input_retry_count = 0;
		runtime.empty_interrupt_count = 0;
		runtime.wake_interrupt_pending = false;
		if (!CompleteHidI2cLevelInterrupt(runtime)) {
			(void)runtime.device->ExpireReset(result);
			failure = HidI2cFailure::GPIOCompletion;
			return false;
		}
		if (!Systimex::AppendDeferredCallback(HidI2cResetTimeout, reinterpret_cast<stduint>(&runtime), (_tocall_ft)ExpireHidI2cReset)) {
			(void)runtime.device->ExpireReset(result);
			failure = HidI2cFailure::ResetSchedule;
			return false;
		}
		return true;
	}

	bool RetryableHidI2cInputStatus(const uni::HIDI2CResult& result) {
		if (result.status == uni::HIDI2CStatus::ResetPending || result.status == uni::HIDI2CStatus::Timeout) return true;
		if (result.status != uni::HIDI2CStatus::TransferFailed) return false;
		switch (result.transfer_status) {
		case uni::IICTransferStatus::AddressNack:
		case uni::IICTransferStatus::DataNack:
		case uni::IICTransferStatus::ArbitrationLost:
		case uni::IICTransferStatus::BusBusy:
		case uni::IICTransferStatus::Timeout:
		case uni::IICTransferStatus::ControllerError:
			return true;
		default:
			return false;
		}
	}

	bool CompleteHidI2cLevelInterrupt(HidInputRuntime& runtime) {
		if (!runtime.level_completion_pending) return true;
		if (!runtime.GPIO_interrupt.active) {
			runtime.level_completion_pending = false;
			return true;
		}
		const auto status = Devs::CompleteGPIOInterrupt(runtime.GPIO_interrupt);
		if (status == DeviceGPIOStatus::Success) {
			runtime.level_completion_pending = false;
			return true;
		}
		return false;
	}

	uint32 HidI2cInputBatchSize(uint32 pending) {
		return pending < HidI2cInputDrainLimit ? pending : HidI2cInputDrainLimit;
	}

	HidI2cInputDisposition AcceptHidI2cInputResult(HidInputRuntime& runtime, uni::HIDI2CStatus status, const uni::HIDI2CResult& result) {
		if (RetryableHidI2cInputStatus(result)) return HidI2cInputDisposition::Retry;
		if (status == uni::HIDI2CStatus::ResetComplete) {
			if (!InitializeHidInputProtocol(runtime)) return runtime.activation_state == HidI2cActivationState::Failed ? HidI2cInputDisposition::Fatal : HidI2cInputDisposition::Ignored;
			(void)Systimex::CancelDeferredCallback(reinterpret_cast<stduint>(&runtime), (_tocall_ft)ExpireHidI2cReset);
			runtime.activation_state = HidI2cActivationState::Running;
			runtime.recovery_attempts = 0;
			runtime.empty_interrupt_count = 0;
			return HidI2cInputDisposition::Consumed;
		}
		if (status != uni::HIDI2CStatus::Success || !result.length) {
			return HidI2cInputDisposition::Ignored;
		}
		if (runtime.activation_state == HidI2cActivationState::ResetPending) {
			return HidI2cInputDisposition::Ignored;
		}
		return Devsman::SubmitHidInputReport(runtime.endpoint, runtime.input_buffer, result.length) ? HidI2cInputDisposition::Consumed : HidI2cInputDisposition::Ignored;
	}

	void ProcessHidI2cInput(HidInputRuntime& runtime, bool schedule_continuation = true) {
		if (!runtime.endpoint || !runtime.device || !runtime.input_buffer || !runtime.input_capacity) {
			FailHidI2cRuntime(runtime, HidI2cFailure::InputRuntime);
			return;
		}
		if (runtime.activation_state != HidI2cActivationState::ResetPending && runtime.activation_state != HidI2cActivationState::Running) {
			(void)CompleteHidI2cLevelInterrupt(runtime);
			return;
		}
		const uint32 batch_size = HidI2cInputBatchSize(runtime.pending_report_count);
		uint32 processed = 0;
		while (processed < batch_size) {
			uni::HIDI2CResult result{};
			const auto status = runtime.device->ReadInput(runtime.input_buffer, runtime.input_capacity, HidI2cTransferBudget, result);
			const auto disposition = AcceptHidI2cInputResult(runtime, status, result);
			if (disposition == HidI2cInputDisposition::Fatal) {
				FailHidI2cRuntime(runtime, HidI2cFailure::RecoverySchedule, &result);
				return;
			}
			if (disposition == HidI2cInputDisposition::Retry) {
				if (runtime.input_retry_count < HidI2cInputRetryLimit && (!schedule_continuation || ScheduleHidI2cInput(runtime, HidI2cInputRetryDelay))) {
					++runtime.input_retry_count;
					return;
				}
				runtime.input_retry_count = 0;
				if (!ScheduleHidI2cRecovery(runtime, HidI2cFailure::InputTransfer)) FailHidI2cRuntime(runtime, HidI2cFailure::RecoverySchedule, &result);
				return;
			}
			runtime.input_retry_count = 0;
			if (runtime.pending_report_count) --runtime.pending_report_count;
			++processed;
			if (runtime.activation_state == HidI2cActivationState::Recovering || runtime.activation_state == HidI2cActivationState::Failed) return;
			if (disposition == HidI2cInputDisposition::Consumed) runtime.empty_interrupt_count = 0;
			else if (status == uni::HIDI2CStatus::Success && !result.length) {
				if (runtime.empty_interrupt_count < 0xFFu) ++runtime.empty_interrupt_count;
				if (runtime.empty_interrupt_count >= HidI2cStuckInterruptLimit) {
					if (!ScheduleHidI2cRecovery(runtime, HidI2cFailure::StuckInterrupt)) FailHidI2cRuntime(runtime, HidI2cFailure::RecoverySchedule, &result);
					return;
				}
			}
		}
		if (runtime.pending_report_count) {
			if (!schedule_continuation) return;
			if (ScheduleHidI2cInput(runtime, 1u)) return;
			FailHidI2cRuntime(runtime, HidI2cFailure::InputSchedule);
			return;
		}
		if (!CompleteHidI2cLevelInterrupt(runtime)) FailHidI2cRuntime(runtime, HidI2cFailure::GPIOCompletion);
	}

	void ContinueHidI2cInput(pureptr_t, stduint context) {
		auto* runtime = reinterpret_cast<HidInputRuntime*>(context);
		if (!runtime) return;
		runtime->input_continuation_scheduled = false;
		ProcessHidI2cInput(*runtime);
	}

	void HandleHidI2cGPIOInterrupt(DeviceNode* endpoint, stduint resource_index, uint32 count, void* context) {
		auto* runtime = static_cast<HidInputRuntime*>(context);
		if (!runtime || runtime->endpoint != endpoint || !endpoint || !endpoint->fields.acpi_info || resource_index >= endpoint->fields.acpi_info->gpio_resource_count) return;
		const auto& resource = endpoint->fields.acpi_info->gpio_resources[resource_index];
		if (resource.trigger == DeviceAcpiGPIOTrigger::Level) runtime->level_completion_pending = true;
		if (runtime->activation_state == HidI2cActivationState::Suspended) {
			runtime->wake_interrupt_pending = true;
			return;
		}
		if (runtime->activation_state == HidI2cActivationState::Recovering || runtime->activation_state == HidI2cActivationState::Stopping || runtime->activation_state == HidI2cActivationState::Failed) return;
		#if CONFIG_ACPI_BOOT_TEST
		const uint32 previous = runtime->deferred_interrupt_count;
		#endif
		if (count > ~uint32(0) - runtime->deferred_interrupt_count) runtime->deferred_interrupt_count = ~uint32(0);
		else runtime->deferred_interrupt_count += count;
		const uint32 available = HidI2cInputPendingLimit - runtime->pending_report_count;
		const uint32 accepted = count < available ? count : available;
		runtime->pending_report_count += accepted;
		#if CONFIG_ACPI_BOOT_TEST
		if (!previous) ploginfo("[GPIO-IRQ-PROBE] endpoint=%s pin=%u deferred=%u masked=%u", endpoint->fields.acpi_info->path, resource.pin, runtime->deferred_interrupt_count, stduint(runtime->level_completion_pending));
		#endif
		if (!runtime->input_continuation_scheduled && !ScheduleHidI2cInput(*runtime, 0)) {
			runtime->activation_state = HidI2cActivationState::Failed;
			RecordHidI2cFailure(endpoint, HidI2cFailure::InputSchedule);
			(void)CompleteHidI2cLevelInterrupt(*runtime);
		}
	}

	#if CONFIG_ACPI_BOOT_TEST
	struct HidI2cInputResultTestFixture {
		DeviceNode endpoint;
		uni::HIDReportLayout layout;
		HidInputRuntime runtime;
		HidInputTestCapture capture;
		HidInputObserverEntry saved_observers[HidInputMaximumObservers];
		stduint saved_observer_count;
		byte report[8];
	};

	bool RetryableHidI2cInputTransfer(uni::IICTransferStatus status) {
		uni::HIDI2CResult result{};
		result.status = uni::HIDI2CStatus::TransferFailed;
		result.transfer_status = status;
		return RetryableHidI2cInputStatus(result);
	}

	void RunHidI2cInputTests() {
		void* storage = uni_hostenv_allocator->allocate(sizeof(HidI2cInputResultTestFixture), alignof(HidI2cInputResultTestFixture));
		if (!storage) {
			plogerro("[HID-I2C-INPUT-TEST] allocation=failed");
			return;
		}
		auto* fixture = new (storage) HidI2cInputResultTestFixture{};
		uni::HIDReportParseResult parse_result{};
		const bool initialized = fixture->layout.Parse(HidReportIdDescriptor, numsof(HidReportIdDescriptor), uni_hostenv_allocator, parse_result) == uni::HIDReportDescriptorStatus::Success;
		fixture->runtime.endpoint = &fixture->endpoint;
		fixture->runtime.layout = &fixture->layout;
		fixture->runtime.input_buffer = fixture->report;
		fixture->runtime.input_capacity = numsof(fixture->report);
		fixture->runtime.activation_state = HidI2cActivationState::ResetPending;
		fixture->saved_observer_count = hid_input_observer_count;
		for0(index, HidInputMaximumObservers) fixture->saved_observers[index] = hid_input_observers[index];
		hid_input_observer_count = 1;
		hid_input_observers[0] = { CaptureHidInputTestEvent, &fixture->capture };
		fixture->runtime.next = hid_input_runtimes;
		hid_input_runtimes = &fixture->runtime;

		uni::HIDI2CResult result{};
		result.status = uni::HIDI2CStatus::ResetComplete;
		const bool reset = AcceptHidI2cInputResult(fixture->runtime, result.status, result) == HidI2cInputDisposition::Consumed && fixture->runtime.activation_state == HidI2cActivationState::Running;
		fixture->report[0] = 1;
		fixture->report[1] = 0x5A;
		result.status = uni::HIDI2CStatus::Success;
		result.length = 2;
		const bool report = AcceptHidI2cInputResult(fixture->runtime, result.status, result) == HidI2cInputDisposition::Consumed && fixture->capture.absolute_count == 1u;
		result.length = 0;
		const bool empty = AcceptHidI2cInputResult(fixture->runtime, result.status, result) == HidI2cInputDisposition::Ignored;
		fixture->report[0] = 2;
		fixture->report[1] = 0x5A;
		result.length = 2;
		const bool unknown = AcceptHidI2cInputResult(fixture->runtime, result.status, result) == HidI2cInputDisposition::Ignored;
		fixture->runtime.activation_state = HidI2cActivationState::ResetPending;
		fixture->report[0] = 1;
		const bool stale = AcceptHidI2cInputResult(fixture->runtime, result.status, result) == HidI2cInputDisposition::Ignored && fixture->runtime.activation_state == HidI2cActivationState::ResetPending;
		const bool transient = RetryableHidI2cInputTransfer(uni::IICTransferStatus::AddressNack) && RetryableHidI2cInputTransfer(uni::IICTransferStatus::DataNack) && RetryableHidI2cInputTransfer(uni::IICTransferStatus::ArbitrationLost) && RetryableHidI2cInputTransfer(uni::IICTransferStatus::BusBusy) && RetryableHidI2cInputTransfer(uni::IICTransferStatus::Timeout) && !RetryableHidI2cInputTransfer(uni::IICTransferStatus::InvalidArgument);
		const bool quirk = SelectHidI2cInputQuirks(HantickVendor, HantickProduct5288) == uni::HIDI2CInputQuirk_BadReportLength && SelectHidI2cInputQuirks(HantickVendor, 0u) == uni::HIDI2CInputQuirk_None;
		const bool bounded = HidI2cInputBatchSize(1u) == 1u && HidI2cInputBatchSize(HidI2cInputDrainLimit + 2u) == HidI2cInputDrainLimit;
		fixture->runtime.level_completion_pending = true;
		const bool completion = CompleteHidI2cLevelInterrupt(fixture->runtime) && !fixture->runtime.level_completion_pending;
		const bool passed = initialized && reset && report && empty && unknown && stale && transient && quirk && bounded && completion;
		MCCA_BOOT_TEST_LOG(passed, "[HID-I2C-INPUT-TEST] reset=%s report=%s empty=%s unknown=%s stale=%s transient=%s quirk=%s bounded=%s completion=%s", reset ? "success" : "failed", report ? "success" : "failed", empty ? "success" : "failed", unknown ? "success" : "failed", stale ? "success" : "failed", transient ? "success" : "failed", quirk ? "success" : "failed", bounded ? "success" : "failed", completion ? "success" : "failed");

		hid_input_runtimes = fixture->runtime.next;
		hid_input_observer_count = fixture->saved_observer_count;
		for0(index, HidInputMaximumObservers) hid_input_observers[index] = fixture->saved_observers[index];
		fixture->~HidI2cInputResultTestFixture();
		uni_hostenv_allocator->deallocate(fixture, sizeof(HidI2cInputResultTestFixture));
	}
	#endif
	bool ScheduleHidI2cActivation(HidInputRuntime& runtime) {
		return Systimex::AppendDeferredCallback(HidI2cActivationDelay, reinterpret_cast<stduint>(&runtime), (_tocall_ft)ActivateHidI2c);
	}

	void ExpireHidI2cReset(pureptr_t, stduint context) {
		auto* runtime = reinterpret_cast<HidInputRuntime*>(context);
		if (!runtime || runtime->activation_state != HidI2cActivationState::ResetPending || !runtime->device || !runtime->device->IsResetPending()) return;
		uni::HIDI2CResult result{};
		(void)runtime->device->ExpireReset(result);
		if (!ScheduleHidI2cRecovery(*runtime, HidI2cFailure::ResetTimeout)) FailHidI2cRuntime(*runtime, HidI2cFailure::RecoverySchedule, &result);
	}

	void RecoverHidI2c(pureptr_t, stduint context) {
		auto* runtime = reinterpret_cast<HidInputRuntime*>(context);
		if (!runtime) return;
		runtime->recovery_scheduled = false;
		if (runtime->activation_state != HidI2cActivationState::Recovering || !runtime->endpoint || !runtime->device) return;
		if (runtime->recovery_attempts >= HidI2cRecoveryAttemptLimit) {
			FailHidI2cRecoveryLimit(*runtime);
			return;
		}
		++runtime->recovery_attempts;
		uni::HIDI2CResult result{};
		HidI2cFailure failure = HidI2cFailure::None;
		if (ResumeHidI2cRuntime(*runtime, failure, result)) return;
		runtime->activation_state = HidI2cActivationState::Recovering;
		runtime->recovery_failure = failure;
		if (runtime->recovery_attempts >= HidI2cRecoveryAttemptLimit) {
			FailHidI2cRecoveryLimit(*runtime, &result);
			return;
		}
		if (!ScheduleHidI2cRecovery(*runtime, failure)) FailHidI2cRuntime(*runtime, HidI2cFailure::RecoverySchedule, &result);
	}

	void ActivateHidI2c(pureptr_t, stduint context) {
		auto* runtime = reinterpret_cast<HidInputRuntime*>(context);
		if (!runtime || runtime->activation_state != HidI2cActivationState::Scheduled || !runtime->endpoint || !runtime->device) return;
		auto* endpoint = runtime->endpoint;
		const auto* info = endpoint->fields.acpi_info;
		if (!info || runtime->GPIO_interrupt_index >= info->gpio_resource_count) {
			FailHidI2cRuntime(*runtime, HidI2cFailure::GPIOInterrupt);
			return;
		}
		if (!runtime->GPIO_interrupt.active) {
			const auto GPIO_status = Devs::RegisterGPIOInterrupt(endpoint, runtime->GPIO_interrupt_index, HandleHidI2cGPIOInterrupt, runtime, runtime->GPIO_interrupt);
			if (GPIO_status != DeviceGPIOStatus::Success) {
				FailHidI2cRuntime(*runtime, HidI2cFailure::GPIOInterrupt);
				return;
			}
		}
		if (info->gpio_resources[runtime->GPIO_interrupt_index].binding_status != DeviceGPIOBindingStatus::Claimed) {
			if (++runtime->activation_attempts < HidI2cActivationAttemptLimit && ScheduleHidI2cActivation(*runtime)) return;
			FailHidI2cRuntime(*runtime, runtime->activation_attempts >= HidI2cActivationAttemptLimit ? HidI2cFailure::ActivationTimeout : HidI2cFailure::ActivationSchedule);
			return;
		}
		uni::HIDI2CResult result{};
		HidI2cFailure failure = HidI2cFailure::None;
		if (!ResumeHidI2cRuntime(*runtime, failure, result)) {
			runtime->activation_state = HidI2cActivationState::Scheduled;
			if (!ScheduleHidI2cRecovery(*runtime, failure)) FailHidI2cRuntime(*runtime, HidI2cFailure::RecoverySchedule, &result);
			return;
		}
		#if CONFIG_ACPI_BOOT_TEST
		(void)Devs::TriggerGPIOModelInterrupt(endpoint, runtime->GPIO_interrupt_index);
		MCCA_BOOT_TEST_LOG(true, "[HID-I2C-IRQ-PROBE] endpoint=%s phase=reset status=%u transfer=%u", info->path, stduint(result.status), stduint(result.transfer_status));
		#endif
	}

	bool StartHidI2c(DeviceNode* endpoint) {
		if (!endpoint || DeviceNodeType(endpoint->fields.node_type) != DeviceNodeType::I2cDevice) return false;
		if (auto* existing = FindHidInputRuntime(endpoint)) {
			if (existing->activation_state != HidI2cActivationState::Failed && existing->activation_state != HidI2cActivationState::Stopping) return true;
			if (!StopHidI2cRuntime(*existing, true)) return false;
		}
		const auto* info = endpoint->fields.acpi_info;
		if (!info) {
			RecordHidI2cFailure(endpoint, HidI2cFailure::MissingAcpiDescription);
			return false;
		}
		const auto* resource = FindHidI2cResource(endpoint);
		if (!resource) {
			RecordHidI2cFailure(endpoint, HidI2cFailure::MissingI2cResource);
			return false;
		}
		if (!(info->flags & DeviceAcpiInfoFlag_HasHidI2cDescriptorRegister)) {
			RecordHidI2cFailure(endpoint, HidI2cFailure::MissingDescriptorRegister);
			return false;
		}
		void* storage = uni_hostenv_allocator->allocate(sizeof(uni::HIDI2CDevice), alignof(uni::HIDI2CDevice));
		if (!storage) {
			RecordHidI2cFailure(endpoint, HidI2cFailure::Allocation);
			return false;
		}
		const uint16 address_flags = resource->type_flags & AcpiI2cTypeFlagTenBitAddress ? uni::IICMessageFlag_TenBitAddress : uni::IICMessageFlag_None;
		const uni::HIDI2CTransport transport{ endpoint, TransferHidI2c, resource->address, address_flags };
		auto* device = new (storage) uni::HIDI2CDevice(transport, uni_hostenv_allocator);
		uni::HIDI2CResult result{};
		if (device->Initialize(info->hid_i2c_descriptor_register, HidI2cTransferBudget, result) != uni::HIDI2CStatus::Success) {
			RecordHidI2cFailure(endpoint, HidI2cFailure::Descriptor, &result);
			device->~HIDI2CDevice();
			uni_hostenv_allocator->deallocate(device, sizeof(uni::HIDI2CDevice));
			return false;
		}
		if (device->ReadReportDescriptor(HidI2cTransferBudget, result) != uni::HIDI2CStatus::Success) {
			RecordHidI2cFailure(endpoint, HidI2cFailure::ReportDescriptor, &result);
			device->~HIDI2CDevice();
			uni_hostenv_allocator->deallocate(device, sizeof(uni::HIDI2CDevice));
			return false;
		}
		uni::HIDReportParseResult parse_result{};
		if (device->ParseReportDescriptor(parse_result) != uni::HIDReportDescriptorStatus::Success) {
			endpoint->fields.binding.probe_result = -int32(HidI2cFailure::ReportLayout);
			plogerro("[HID-I2C] endpoint=%s failure=%s parse=%u offset=%u prefix=%02X", info->path, HidI2cFailureName(HidI2cFailure::ReportLayout), stduint(parse_result.status), parse_result.offset, stduint(parse_result.item_prefix));
			device->~HIDI2CDevice();
			uni_hostenv_allocator->deallocate(device, sizeof(uni::HIDI2CDevice));
			return false;
		}
		const auto& descriptor = device->Descriptor();
		device->ConfigureInputQuirks(SelectHidI2cInputQuirks(descriptor.vendor_id, descriptor.product_id));
		const auto& report_layout = device->ReportLayout();
		void* runtime_storage = uni_hostenv_allocator->allocate(sizeof(HidInputRuntime), alignof(HidInputRuntime));
		if (!runtime_storage) {
			RecordHidI2cFailure(endpoint, HidI2cFailure::InputRuntime);
			device->~HIDI2CDevice();
			uni_hostenv_allocator->deallocate(device, sizeof(uni::HIDI2CDevice));
			return false;
		}
		auto* input_runtime = new (runtime_storage) HidInputRuntime{};
		input_runtime->endpoint = endpoint;
		input_runtime->device = device;
		input_runtime->layout = &report_layout;
		input_runtime->input_quirks = SelectHidInputQuirks(descriptor.vendor_id, descriptor.product_id);
		input_runtime->input_capacity = descriptor.maximum_input_length;
		input_runtime->input_buffer = static_cast<byte*>(uni_hostenv_allocator->allocate(input_runtime->input_capacity));
		if (!input_runtime->input_buffer) {
			RecordHidI2cFailure(endpoint, HidI2cFailure::InputRuntime);
			input_runtime->~HidInputRuntime();
			uni_hostenv_allocator->deallocate(input_runtime, sizeof(HidInputRuntime));
			device->~HIDI2CDevice();
			uni_hostenv_allocator->deallocate(device, sizeof(uni::HIDI2CDevice));
			return false;
		}
		input_runtime->GPIO_interrupt_index = FindHidI2cGPIOInterrupt(endpoint);
		input_runtime->ALPS.protocol = uni::MatchHIDALPSProtocol(descriptor.vendor_id, descriptor.product_id);
		AnalyzeHidInputLayout(*input_runtime);
		if (input_runtime->ALPS.protocol != uni::HIDALPSProtocol::None) input_runtime->capabilities |= DeviceInputCapability_Touch | DeviceInputCapability_Touchpad | DeviceInputCapability_Button;
		input_runtime->next = hid_input_runtimes;
		hid_input_runtimes = input_runtime;
		endpoint->fields.vendor_id = descriptor.vendor_id;
		endpoint->fields.device_id = descriptor.product_id;
		endpoint->fields.binding.driver_data = device;
		if (input_runtime->GPIO_interrupt_index != ~stduint(0)) {
			input_runtime->activation_state = HidI2cActivationState::Scheduled;
			if (!ScheduleHidI2cActivation(*input_runtime)) {
				FailHidI2cRuntime(*input_runtime, HidI2cFailure::ActivationSchedule);
				return false;
			}
		}
		else input_runtime->activation_state = HidI2cActivationState::Running;
		#if CONFIG_ACPI_BOOT_TEST
		ploginfo("[HID-I2C-DRIVER] status=success endpoint=%s address=%04X register=%04X version=%04X vid=%04X pid=%04X report=%u input=%u output=%u interrupt=%u definitions=%u fields=%u collections=%u capabilities=%08X unknown=%u buttons=%u/%u/%u protocol=%u", info->path, resource->address, info->hid_i2c_descriptor_register, descriptor.version, descriptor.vendor_id, descriptor.product_id, stduint(descriptor.report_descriptor_length), stduint(descriptor.maximum_input_length), stduint(descriptor.maximum_output_length), CountHidI2cInterruptResources(endpoint), report_layout.DefinitionCount(), report_layout.FieldCount(), report_layout.CollectionCount(), input_runtime->capabilities, input_runtime->unknown_field_count, stduint(input_runtime->button_usage_count), stduint(input_runtime->touchpad_button_usage_count), stduint(input_runtime->mouse_button_usage_count), stduint(input_runtime->ALPS.protocol));
		#endif
		return true;
	}
}

bool Devsman::RegisterInputObserver(DeviceInputObserver observer, void* context) {
	if (!observer) return false;
	for0(index, hid_input_observer_count) {
		if (hid_input_observers[index].observer == observer && hid_input_observers[index].context == context) return true;
	}
	if (hid_input_observer_count >= HidInputMaximumObservers) return false;
	hid_input_observers[hid_input_observer_count++] = { observer, context };
	return true;
}

uint32 Devsman::GetInputCapabilities(const DeviceNode* node) {
	const auto* runtime = FindHidInputRuntime(node);
	return runtime ? runtime->capabilities : DeviceInputCapability_None;
}

bool Devsman::SubmitHidInputReport(DeviceNode* node, const byte* report, stduint length) {
	auto* runtime = FindHidInputRuntime(node);
	if (!runtime) return false;
	if (runtime->ALPS.protocol != uni::HIDALPSProtocol::None && DecodeHIDALPSInputReport(*runtime, report, length)) return true;
	return DecodeHidInputReport(*runtime, report, length);
}

_ESYM_C void R_HID_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_HID{
	.init = R_HID_INIT,
	.name = "HID",
};

void R_HID_INIT() {
	#if CONFIG_ACPI_BOOT_TEST
	RunHidReportTests();
	RunHidInputTests();
	RunHidI2cInputTests();
	#endif
	if (!Devsman::RegisterInputObserver(RouteMecocoaInput)) {
		plogerro("[HID] failed to register Mecocoa input observer");
		return;
	}
	if (!Devsman::RegisterDriverStarter("hid-i2c", StartHidI2c)) {
		plogerro("[HID] failed to register HID-over-I2C driver starter");
		return;
	}
	Devsman::StartKnownDrivers();
}
#endif
