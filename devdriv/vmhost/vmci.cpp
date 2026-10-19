// ASCII g++ TAB4 LF
// ModuTitle: VMware VMCI Ring1 Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <stdio.h>
#include <c/stdinc.h>
#include "../../include/taskman.com.hpp"
#include "../../include/syscall-pow.hpp"

#if defined(_ACCM) && ((_ACCM & 0xFF00) == 0x8600)

namespace {
	constexpr uint16 VmciVendorId = 0x15ADu;
	constexpr uint16 VmciDeviceId = 0x0740u;
	constexpr uint32 VmciMmioBarIndex = 1;
	constexpr uint64 VmciMmioBarSize = 256u * 1024u;
	constexpr uint64 VmciMmioRegisterOffset = 128u * 1024u;
	constexpr uint64 VmciMmioRegisterSize = 64u * 1024u;

	enum class VmciRegister : uint32 {
		Status = 0x00u,
		Control = 0x04u,
		InterruptCause = 0x08u,
		InterruptMask = 0x0Cu,
		DataOut = 0x10u,
		DataIn = 0x14u,
		Capabilities = 0x18u,
	};

	constexpr uint32 VmciRequiredRegisterSize =
		_IMM(VmciRegister::Capabilities) + sizeof(uint32);
	constexpr uint32 VmciControlReset = 1u << 0;
	constexpr uint32 VmciControlInterruptDisable = 1u << 2;
	constexpr uint32 VmciCapabilityHypercall = 1u << 0;
	constexpr uint32 VmciCapabilityGuestcall = 1u << 1;
	constexpr uint32 VmciCapabilityDatagram = 1u << 2;
	constexpr uint32 VmciCapabilityNotifications = 1u << 3;
	constexpr uint32 VmciCapabilityPpn64 = 1u << 4;
	constexpr uint32 VmciCapabilityDmaDatagram = 1u << 5;

	struct VmciContext {
		stduint dev_handle = 0;
		PwcallDeviceIdentity identity = {};
		PwcallDeviceResourceInfo io = {};
		PwcallDeviceResourceInfo mmio = {};
		PwcallDeviceResourceInfo irq = {};
		volatile uint32* registers = nullptr;
		void* mapped_address = nullptr;
		uint32 status = 0;
		uint32 capabilities = 0;
		uint32 interrupt_cause = 0;
		uint32 interrupt_mask = 0;
		bool use_mmio = false;
	};

	VmciContext g_vmci{};

	bool FindResource(stduint dev_handle, PwcallDeviceResourceType type,
		uint32 resource_index, PwcallDeviceResourceInfo& resource) {
		const stdsint count = Powercall::DevGetResourceCount(dev_handle);
		if (count <= 0) return false;
		for (stdsint i = 0; i < count; ++i) {
			PwcallDeviceResourceQuery query = {};
			query.index = uint32(i);
			if (Powercall::DevGetResource(dev_handle, &query) != 0) continue;
			if (PwcallDeviceResourceType(query.resource.type) != type ||
				query.resource.index != resource_index) continue;
			resource = query.resource;
			return true;
		}
		return false;
	}

	bool ReadIoRegister(VmciRegister reg, uint32& value) {
		PwcallDeviceIoRequest request = {};
		request.resource_type = _IMM(PwcallDeviceResourceType::PciBarIo);
		request.resource_index = g_vmci.io.index;
		request.width = sizeof(uint32);
		request.offset = _IMM(reg);
		if (Powercall::DevIoRead(g_vmci.dev_handle, &request) != 0) return false;
		value = request.value;
		return true;
	}

	bool WriteIoRegister(VmciRegister reg, uint32 value) {
		PwcallDeviceIoRequest request = {};
		request.resource_type = _IMM(PwcallDeviceResourceType::PciBarIo);
		request.resource_index = g_vmci.io.index;
		request.width = sizeof(uint32);
		request.offset = _IMM(reg);
		request.value = value;
		return Powercall::DevIoWrite(g_vmci.dev_handle, &request) == 0;
	}

	bool ReadRegister(VmciRegister reg, uint32& value) {
		if (!g_vmci.use_mmio) return ReadIoRegister(reg, value);
		value = g_vmci.registers[_IMM(reg) / sizeof(uint32)];
		return true;
	}

	bool WriteRegister(VmciRegister reg, uint32 value) {
		if (!g_vmci.use_mmio) return WriteIoRegister(reg, value);
		g_vmci.registers[_IMM(reg) / sizeof(uint32)] = value;
		return true;
	}

	bool MapMmioRegisters() {
		if (g_vmci.mmio.length != VmciMmioBarSize) return false;
		PwcallDeviceMapRequest request = {};
		request.resource_type = _IMM(PwcallDeviceResourceType::PciBarMmio);
		request.resource_index = g_vmci.mmio.index;
		request.map_flags = _IMM(PwcallDeviceMapFlag::Writable);
		request.offset = VmciMmioRegisterOffset;
		request.length = VmciMmioRegisterSize;
		const stdsint address = Powercall::DevMmap(g_vmci.dev_handle, &request);
		if (address <= 0) return false;
		g_vmci.mapped_address = reinterpret_cast<void*>(stduint(address));
		g_vmci.registers = reinterpret_cast<volatile uint32*>(g_vmci.mapped_address);
		g_vmci.use_mmio = true;
		return true;
	}

	bool SelectRegisterAccess() {
		if (FindResource(g_vmci.dev_handle, PwcallDeviceResourceType::PciBarMmio,
			VmciMmioBarIndex, g_vmci.mmio) && MapMmioRegisters()) return true;
		if (!FindResource(g_vmci.dev_handle, PwcallDeviceResourceType::PciBarIo,
			0, g_vmci.io)) return false;
		if (g_vmci.io.length < VmciRequiredRegisterSize ||
			g_vmci.io.start + VmciRequiredRegisterSize > 0x10000u) return false;
		g_vmci.use_mmio = false;
		return true;
	}

	bool InitializeRegisters() {
		if (!WriteRegister(VmciRegister::Control, VmciControlReset) ||
			!WriteRegister(VmciRegister::Control, VmciControlInterruptDisable) ||
			!WriteRegister(VmciRegister::InterruptMask, 0)) return false;
		if (!ReadRegister(VmciRegister::Status, g_vmci.status) ||
			!ReadRegister(VmciRegister::Capabilities, g_vmci.capabilities) ||
			!ReadRegister(VmciRegister::InterruptCause, g_vmci.interrupt_cause) ||
			!ReadRegister(VmciRegister::InterruptMask, g_vmci.interrupt_mask)) return false;
		return true;
	}

	bool ValidateCapabilities() {
		if ((g_vmci.capabilities & VmciCapabilityDatagram) == 0) {
			printf("vmci: datagram capability is missing\n\r");
			return false;
		}
		if (g_vmci.use_mmio &&
			(g_vmci.capabilities & VmciCapabilityDmaDatagram) == 0) {
			printf("vmci: MMIO access requires DMA datagram capability\n\r");
			return false;
		}
		return true;
	}

	void ShutdownVmci() {
		if (g_vmci.dev_handle && (g_vmci.use_mmio || g_vmci.io.length)) {
			(void)WriteRegister(VmciRegister::Control, VmciControlReset);
		}
		if (g_vmci.mapped_address) {
			(void)Powercall::DevUmap(g_vmci.mapped_address, stduint(VmciMmioRegisterSize));
		}
		g_vmci.mapped_address = nullptr;
		g_vmci.registers = nullptr;
		g_vmci.use_mmio = false;
		if (g_vmci.dev_handle) (void)Powercall::DevClose(g_vmci.dev_handle);
		g_vmci.dev_handle = 0;
	}

	bool InitializeVmci() {
		const stduint cls = (stduint(VmciVendorId) << 16) | VmciDeviceId;
		const stdsint opened = Powercall::DevOpen(0, cls,
			_IMM(PwcallDeviceOpenFlag::Interrupt));
		if (opened <= 0) return false;
		g_vmci.dev_handle = stduint(opened);
		if (Powercall::DevGetIdentity(g_vmci.dev_handle, &g_vmci.identity) != 0 ||
			g_vmci.identity.vendor_id != VmciVendorId ||
			g_vmci.identity.device_id != VmciDeviceId) return false;
		if (!FindResource(g_vmci.dev_handle, PwcallDeviceResourceType::IrqLine,
			0, g_vmci.irq) || !SelectRegisterAccess() || !InitializeRegisters()) return false;

		const auto& bar = g_vmci.use_mmio ? g_vmci.mmio : g_vmci.io;
		printf("vmci: pci=%02x:%02x.%u access=%s bar=%u base=%08x len=%u irq=%u\n\r",
			unsigned(g_vmci.identity.pci_bus), unsigned(g_vmci.identity.pci_device),
			unsigned(g_vmci.identity.pci_function), g_vmci.use_mmio ? "mmio" : "io",
			unsigned(bar.index), unsigned(uint32(bar.start)), unsigned(uint32(bar.length)),
			unsigned(g_vmci.irq.start));
		printf("vmci: status=%08x caps=%08x hypercall=%u guestcall=%u datagram=%u "
			"notify=%u ppn64=%u dma-datagram=%u icr=%08x imr=%08x\n\r",
			unsigned(g_vmci.status), unsigned(g_vmci.capabilities),
			unsigned((g_vmci.capabilities & VmciCapabilityHypercall) != 0),
			unsigned((g_vmci.capabilities & VmciCapabilityGuestcall) != 0),
			unsigned((g_vmci.capabilities & VmciCapabilityDatagram) != 0),
			unsigned((g_vmci.capabilities & VmciCapabilityNotifications) != 0),
			unsigned((g_vmci.capabilities & VmciCapabilityPpn64) != 0),
			unsigned((g_vmci.capabilities & VmciCapabilityDmaDatagram) != 0),
			unsigned(g_vmci.interrupt_cause), unsigned(g_vmci.interrupt_mask));
		return ValidateCapabilities();
	}

	bool ReceiveEvent() {
		DeviceEvent event = {};
		CommMsg message = {};
		message.data.address = _IMM(&event);
		message.data.length = sizeof(event);
		if (Powercall::SysComm(COMM_RECV, ANYPROC, &message) != 0) return false;
		if (message.type != _IMM(KernelMsg::DeviceEvent) ||
			message.data.length < sizeof(event) ||
			event.version != DeviceEventProtocolVersion ||
			event.device_handle != g_vmci.dev_handle) return true;
		switch (DeviceEventKind(event.kind)) {
		case DeviceEventKind::Interrupt:
		{
			uint32 cause = 0;
			const bool serviced = ReadRegister(VmciRegister::InterruptCause, cause);
			g_vmci.interrupt_cause = cause;
			const bool acknowledged = Powercall::DevAck(g_vmci.dev_handle,
				stduint(event.sequence), event.generation) == 0;
			return serviced && acknowledged;
		}
		case DeviceEventKind::Removed:
		case DeviceEventKind::Fault:
		case DeviceEventKind::Shutdown:
			return false;
		default:
			return true;
		}
	}
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	if (Powercall::Hello() != 0) return -1;
	if (!InitializeVmci()) {
		printf("vmci: initialization failed\n\r");
		ShutdownVmci();
		return -1;
	}
	if (Powercall::DevPublish(g_vmci.dev_handle,
		PwcallDevicePublishCommand::Started) != 0) {
		printf("vmci: device publish failed\n\r");
		ShutdownVmci();
		return -1;
	}
	printf("vmci: register initialization complete, interrupts disabled\n\r");
	while (ReceiveEvent()) {}
	ShutdownVmci();
	return -1;
}

#else

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return -1;
}

#endif
