#ifndef DEVSMAN_INN_HPP_
#define DEVSMAN_INN_HPP_

#include <c/stdinc.h>

struct DeviceNode;

enum DeviceInputCapabilityFlags : uint32 {
	DeviceInputCapability_None = 0,
	DeviceInputCapability_Keyboard = 1u << 0,
	DeviceInputCapability_RelativePointer = 1u << 1,
	DeviceInputCapability_AbsolutePointer = 1u << 2,
	DeviceInputCapability_Touch = 1u << 3,
	DeviceInputCapability_Multitouch = 1u << 4,
	DeviceInputCapability_Touchpad = 1u << 5,
	DeviceInputCapability_Touchscreen = 1u << 6,
	DeviceInputCapability_Button = 1u << 7,
	DeviceInputCapability_Wheel = 1u << 8,
	DeviceInputCapability_Raw = 1u << 9,
};

enum class DeviceInputEventType : uint8 {
	Key,
	RelativePointer,
	AbsolutePointer,
	Touch,
	Button,
	Wheel,
	Raw,
};

enum DeviceInputEventFlags : uint16 {
	DeviceInputEventFlag_None = 0,
	DeviceInputEventFlag_Pressed = 1u << 0,
	DeviceInputEventFlag_Begin = 1u << 1,
	DeviceInputEventFlag_End = 1u << 2,
	DeviceInputEventFlag_InRange = 1u << 3,
	DeviceInputEventFlag_Confidence = 1u << 4,
	DeviceInputEventFlag_Touchpad = 1u << 5,
	DeviceInputEventFlag_Touchscreen = 1u << 6,
	DeviceInputEventFlag_ConfidenceValid = 1u << 7,
};

struct DeviceInputEvent {
	DeviceInputEventType type;
	uint8 report_id;
	uint16 flags;
	uint32 usage_page;
	uint32 usage;
	int32 value;
	int32 x;
	int32 y;
	int32 pressure;
	int32 width;
	int32 height;
	uint16 contact_id;
	uint16 contact_count;
	uint16 buttons;
	uint8 modifiers;
	uint8 reserved;
};

using DeviceInputObserver = void (*)(DeviceNode* node, const DeviceInputEvent& event, void* context);

#endif
