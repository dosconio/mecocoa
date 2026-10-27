// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: ACPI Global System Interrupt Routing
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#ifndef MECOCOA_DEVSMAN_ACPI_GSI_H_
#define MECOCOA_DEVSMAN_ACPI_GSI_H_

#include <c/stdinc.h>

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)

enum class ACPIGSIStatus : uint8 {
	Success,
	InvalidArgument,
	Unsupported,
	GSIUnavailable,
	ConfigurationConflict,
	ResourceUnavailable,
	RouteFailure,
	OwnershipMismatch,
};

struct ACPIGSIConfiguration {
	uint32 gsi;
	bool level_triggered;
	bool active_low;
	bool shareable;
};

using ACPIGSIFilter = bool (*)(void* context);

struct ACPIGSIBinding {
	uint32 gsi;
	uint32 generation;
	uint16 slot;
	uint8 vector;
	bool active;
};

namespace Devs {
	ACPIGSIStatus BindACPIGSI(const ACPIGSIConfiguration& configuration, ACPIGSIFilter filter, void* context, ACPIGSIBinding& binding);
	ACPIGSIStatus UnbindACPIGSI(ACPIGSIBinding& binding);
	#if CONFIG_ACPI_BOOT_TEST
	void RunACPIGSITest();
	#endif
}

#endif
#endif
