// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Human Interface Device Drivers
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/Serial/IIC-HID.hpp>

namespace {
	constexpr stduint HidI2cTransferBudget = 100000u;
	constexpr uint16 AcpiI2cTypeFlagTenBitAddress = 1u << 0;

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

	constexpr uint32 HidUsagePageGenericDesktop = 0x01u;
	constexpr uint32 HidUsagePageKeyboard = 0x07u;
	constexpr uint32 HidUsagePageButton = 0x09u;
	constexpr uint32 HidUsagePageDigitizer = 0x0Du;
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
		uint32 capabilities;
		stduint unknown_field_count;
		byte keyboard_scratch[29];
		HidKeyboardReportState keyboard_reports[HidInputMaximumKeyboardReports];
		uint16 button_states[256];
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
		for0(index, layout.CollectionCount()) {
			const auto& collection = layout.Collections()[index];
			if (collection.usage.page == HidUsagePageDigitizer && collection.usage.id == HidUsageFinger) ++finger_collections;
			if (collection.usage.page == HidUsagePageDigitizer && collection.usage.id == HidUsageTouchpad) runtime.capabilities |= DeviceInputCapability_Touch | DeviceInputCapability_Touchpad;
			if (collection.usage.page == HidUsagePageDigitizer && collection.usage.id == HidUsageTouchscreen) runtime.capabilities |= DeviceInputCapability_Touch | DeviceInputCapability_Touchscreen;
			if (collection.usage.page == HidUsagePageGenericDesktop && collection.usage.id == HidUsageKeyboard) runtime.capabilities |= DeviceInputCapability_Keyboard;
		}
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
				if (usage.page == HidUsagePageButton) runtime.capabilities |= DeviceInputCapability_Button;
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
		bool has_touch = false;
		bool has_contact_count = false;
		uint16 contact_count = 0;
		uint16 current_buttons = 0;
		uint16 active_buttons = runtime.button_states[report_id];
		int64 relative_x = 0;
		int64 relative_y = 0;
		int64 wheel = 0;
		int64 absolute_x = 0;
		int64 absolute_y = 0;
		int64 absolute_x_minimum = 0;
		int64 absolute_x_maximum = 0;
		int64 absolute_y_minimum = 0;
		int64 absolute_y_maximum = 0;
		for0(field_index, layout.FieldCount()) {
			const auto& field = layout.Fields()[field_index];
			if (field.type != uni::HIDReportType::Input || field.report_id != report_id || (field.flags & uni::HIDReportMainFlag_Constant)) continue;
			uni::HIDUsage field_usage{};
			if (layout.ResolveUsage(field, 0, field_usage)) {
				if (field_usage.page == HidUsagePageKeyboard) has_keyboard = true;
				if (field_usage.page == HidUsagePageButton) has_buttons = true;
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
			const uint16 changed = active_buttons ^ current_buttons;
			for0(bit, 16) {
				if (!(changed & (1u << bit))) continue;
				DeviceInputEvent event{};
				event.type = DeviceInputEventType::Button;
				event.report_id = report_id;
				event.flags = current_buttons & (1u << bit) ? DeviceInputEventFlag_Pressed : DeviceInputEventFlag_None;
				event.usage_page = HidUsagePageButton;
				event.usage = bit + 1u;
				event.value = event.flags & DeviceInputEventFlag_Pressed ? 1 : 0;
				event.buttons = current_buttons;
				EmitHidInputEvent(runtime, event);
			}
			active_buttons = current_buttons;
			runtime.button_states[report_id] = current_buttons;
		}
		if (has_relative_pointer || (has_buttons && (runtime.capabilities & (DeviceInputCapability_RelativePointer | DeviceInputCapability_Touchpad)))) {
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
			if (has_contact_count) {
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
			if (contact_count > 1 || emitted_contacts > 1) runtime.capabilities |= DeviceInputCapability_Multitouch;
		}
		return true;
	}

	void RouteMecocoaInput(DeviceNode*, const DeviceInputEvent& event, void*) {
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
				hand_mouse_usb(mouse);
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
	};

	void CaptureHidInputTestEvent(DeviceNode*, const DeviceInputEvent& event, void* context) {
		auto* capture = static_cast<HidInputTestCapture*>(context);
		if (!capture) return;
		switch (event.type) {
		case DeviceInputEventType::Key: ++capture->key_count; break;
		case DeviceInputEventType::RelativePointer: ++capture->relative_count; break;
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
		ploginfo("[HID-REPORT-TEST] keyboard=%s mouse=%s touch=%s multitouch=%s report-id=%s signed=%s long=%s push-pop=%s delimiter=%s invalid=%s", keyboard ? "success" : "failed", mouse ? "success" : "failed", touch ? "success" : "failed", multitouch ? "success" : "failed", report_id ? "success" : "failed", signed_value ? "success" : "failed", long_item ? "success" : "failed", push_pop ? "success" : "failed", delimiter ? "success" : "failed", invalid ? "success" : "failed");
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
		hid_input_observer_count = saved_observer_count;
		hid_input_observers[0] = saved_observer;
		ploginfo("[HID-INPUT-TEST] keyboard=%s relative=%s absolute=%s touch=%s multitouch=%s lifecycle=%s button=%s wheel=%s unknown=%s", keyboard ? "success" : "failed", pointer && relative ? "success" : "failed", pointer && absolute ? "success" : "failed", touch ? "success" : "failed", multitouch ? "success" : "failed", lifecycle ? "success" : "failed", pointer && button ? "success" : "failed", pointer && wheel ? "success" : "failed", unknown ? "success" : "failed");
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

	bool StartHidI2c(DeviceNode* endpoint) {
		if (!endpoint || DeviceNodeType(endpoint->fields.node_type) != DeviceNodeType::I2cDevice) return false;
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
		AnalyzeHidInputLayout(*input_runtime);
		input_runtime->next = hid_input_runtimes;
		hid_input_runtimes = input_runtime;
		endpoint->fields.vendor_id = descriptor.vendor_id;
		endpoint->fields.device_id = descriptor.product_id;
		endpoint->fields.binding.driver_data = device;
		#if CONFIG_ACPI_BOOT_TEST
		ploginfo("[HID-I2C-DRIVER] status=success endpoint=%s address=%04X register=%04X version=%04X vid=%04X pid=%04X report=%u input=%u output=%u interrupt=%u definitions=%u fields=%u collections=%u capabilities=%08X unknown=%u", info->path, resource->address, info->hid_i2c_descriptor_register, descriptor.version, descriptor.vendor_id, descriptor.product_id, stduint(descriptor.report_descriptor_length), stduint(descriptor.maximum_input_length), stduint(descriptor.maximum_output_length), CountHidI2cInterruptResources(endpoint), report_layout.DefinitionCount(), report_layout.FieldCount(), report_layout.CollectionCount(), input_runtime->capabilities, input_runtime->unknown_field_count);
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
	return runtime && DecodeHidInputReport(*runtime, report, length);
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
