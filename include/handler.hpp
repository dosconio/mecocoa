#ifndef HANDLER_HPP_
#define HANDLER_HPP_

#include <c/stdinc.h>

#if defined(_MCCA) && (_MCCA & 0xFF00) == 0x8600

#if defined(CONFIG_ENABLE_MMU) && CONFIG_ENABLE_MMU
namespace uni { struct Paging; }
#endif

namespace Handler {
	constexpr uint8 Ring1VectorBase = 0x78;
	constexpr uint8 Ring1ExtendedVectorBase = 0x82;
	constexpr uint8 Ring1LegacyVectorCount = 8;
	constexpr uint8 Ring1VectorCount = 32;
	// Reserved for the VT-d unit, outside the device vector pools.
	constexpr uint8 VtdFaultVector = 0xA1;
	constexpr uint8 DeviceMessageVectorBase = 0xD0;
	constexpr uint8 DeviceMessageVectorCount = 16;

	// Initialize on the BSP before enabling device interrupts or starting APs.
	void InitializeEntries();
	//
	stduint GeneratedEntry(uint8 vector);

	//
	#if defined(CONFIG_ENABLE_MMU) && CONFIG_ENABLE_MMU
	bool MapEntries(uni::Paging& paging);
	#endif

	// The caller must hold the shared I/O APIC route lock for these operations.
	uint8 AllocateVectors(uint8 first, uint8 length, uint8 count = 1);
	//
	void InstallDispatch(uint8 vector, stduint entry, void (*handler)(stduint));
	//
	void BeginVectorRelease(uint8 vector);
	//
	void FinishVectorRelease(uint8 vector, bool reusable = true);
	//
	bool IsVectorDrained(uint8 vector, uint32 lapic_id);

	// This hint can be read without holding the route lock.
	bool HasReleasingVectors();
}

#endif

#endif
