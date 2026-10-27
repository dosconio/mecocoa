// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Device Manager] ACPI GPIO Firmware Relations
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#ifndef DEVSMAN_GPIO_HPP_
#define DEVSMAN_GPIO_HPP_

#include <c/stdinc.h>

struct DeviceNode;
struct DeviceEvent;

namespace uni {
	struct GeneralPurposeInputOutputPortBackend;
}

constexpr stduint DeviceAcpiGPIOPathCapacity = 160;

// ACPI Specification 6.5, sections 6.4.3.8.1 and 6.4.3.8.2 define the normalized GPIO fields below.
enum class DeviceAcpiGPIOConnectionType : uint8 {
	Interrupt,
	IO,
};

enum class DeviceAcpiGPIOTrigger : uint8 {
	NotApplicable,
	Level,
	Edge,
};

enum class DeviceAcpiGPIOPolarity : uint8 {
	NotApplicable,
	ActiveHigh,
	ActiveLow,
	ActiveBoth,
};

enum class DeviceAcpiGPIOSharing : uint8 {
	Exclusive,
	Shared,
};

enum class DeviceAcpiGPIOPull : uint8 {
	Default,
	Up,
	Down,
	None,
	VendorDefined,
};

enum class DeviceAcpiGPIOIORestriction : uint8 {
	NotApplicable,
	None,
	InputOnly,
	OutputOnly,
	NoneAndPreserve,
};

enum class DeviceAcpiGPIOResourceStatus : uint8 {
	Success,
	MalformedDescriptor,
	UnsupportedRevision,
	UnsupportedConnectionType,
	ReservedValue,
	InvalidPinTable,
	InvalidResourceSource,
	InvalidVendorData,
	DuplicatePin,
	DuplicateResource,
	MalformedResourceSource,
	ControllerNotFound,
	ControllerNotDevice,
};

enum class DeviceAcpiGPIODependencyStatus : uint8 {
	NotDeclared,
	Satisfied,
	ControllerNotListed,
	ControllerUnavailable,
	Malformed,
	EvaluationFailed,
};

enum class DeviceGPIOBindingStatus : uint8 {
	Unresolved,
	WaitingController,
	Ready,
	Claimed,
	Failed,
};

enum class DeviceGPIOStatus : uint8 {
	Success,
	InvalidArgument,
	ResourceUnavailable,
	ControllerUnavailable,
	OutOfRange,
	AlreadyClaimed,
	OwnershipMismatch,
	Unsupported,
	ConfigurationConflict,
};

struct DeviceAcpiGPIOResource {
	char resource_source[DeviceAcpiGPIOPathCapacity];
	char controller_path[DeviceAcpiGPIOPathCapacity];
	uint32 pin;
	uint16 general_flags;
	uint16 connection_flags;
	uint16 drive_strength;
	uint16 debounce_timeout;
	uint16 descriptor_index;
	uint16 pin_index;
	uint16 dependency_count;
	uint8 revision;
	uint8 resource_source_index;
	uint8 pin_configuration;
	DeviceAcpiGPIOConnectionType type;
	DeviceAcpiGPIOTrigger trigger;
	DeviceAcpiGPIOPolarity polarity;
	DeviceAcpiGPIOSharing sharing;
	DeviceAcpiGPIOPull pull;
	DeviceAcpiGPIOIORestriction io_restriction;
	DeviceAcpiGPIOResourceStatus status;
	DeviceAcpiGPIODependencyStatus dependency_status;
	DeviceGPIOBindingStatus binding_status;
	DeviceNode* controller_node;
	stduint port_id;
	bool consumer;
	bool wake_capable;
};

struct DeviceGPIOLease {
	DeviceNode* owner;
	DeviceNode* controller;
	DeviceAcpiGPIOResource* resource;
	DeviceGPIOLease* next;
	stduint port_id;
	uint32 pin;
	bool shared;
	bool active;
};

using DeviceGPIOInterruptHandler = void (*)(DeviceNode* owner, stduint resource_index, uint32 count, void* context);

struct DeviceGPIOInterruptRegistration {
	uint32 generation;
	uint16 slot;
	bool active;
};

struct DeviceGPIOControllerCompatibilityDescription {
	const char* family_name;
	const char* product_name;
	const char* driver_name;
	const void* register_layout;
	uint32 evidence_flags;
	uint32 minimum_access_length;
	uint8 access_index;
};

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)
namespace Devs {
	DeviceGPIOStatus RegisterGPIOController(DeviceNode* controller, const uni::GeneralPurposeInputOutputPortBackend& backend);
	DeviceGPIOStatus UnregisterGPIOController(DeviceNode* controller);
	DeviceGPIOStatus ClaimGPIO(DeviceNode* owner, stduint resource_index, DeviceGPIOLease& lease);
	DeviceGPIOStatus ReleaseGPIO(DeviceGPIOLease& lease);
	DeviceGPIOStatus RegisterGPIOInterrupt(DeviceNode* owner, stduint resource_index, DeviceGPIOInterruptHandler handler, void* context, DeviceGPIOInterruptRegistration& registration);
	DeviceGPIOStatus CompleteGPIOInterrupt(DeviceGPIOInterruptRegistration& registration);
	DeviceGPIOStatus UnregisterGPIOInterrupt(DeviceGPIOInterruptRegistration& registration);
	bool DispatchGPIOInterruptEvent(const DeviceEvent& event);
	#if CONFIG_ACPI_BOOT_TEST
	DeviceGPIOStatus TriggerGPIOModelInterrupt(DeviceNode* owner, stduint resource_index);
	#endif
}
#endif

#endif
