// ASCII g++ TAB4 LF
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/PCI.hpp>

namespace {
	const Iommu::Backend* iommu_backend;
}

void Iommu::Probe(stduint dmar_address, stduint ivrs_address) {
	if (dmar_address || !ivrs_address) iommu_backend = VTd::Probe(dmar_address);
	else plogtrac("[IOMMU] IVRS detected; AMD translation backend is unavailable");
}

bool Iommu::EnableDmaTranslation(DeviceNode* pci_root) {
	return iommu_backend ? iommu_backend->enable_dma(pci_root) : true;
}

Iommu::DmaState Iommu::GetDmaState() {
	return iommu_backend ? iommu_backend->dma_state() : DmaState::Inactive;
}

bool Iommu::MapDeviceDma(DeviceNode* device, stduint physical, stduint length,
	Iova::DmaMappingFlag flags, Iova::DmaMapping& mapping) {
	return iommu_backend && iommu_backend->map_dma(device, physical, length, flags, mapping);
}

bool Iommu::UnmapDeviceDma(DeviceNode* device, uint64 device_address, stduint length) {
	return iommu_backend && iommu_backend->unmap_dma(device, device_address, length);
}

void Iommu::EnableFaultEvents() {
	if (iommu_backend) iommu_backend->enable_faults();
}

void Iommu::NotifyFaultService() {
	if (iommu_backend) iommu_backend->notify_faults();
}

void Iommu::ServiceFaultEvents() {
	if (iommu_backend) iommu_backend->service_faults();
}

bool Iommu::PciInterruptRemappingAvailable(const DeviceNode* device) {
	return iommu_backend && iommu_backend->remaps_pci(device);
}

bool Iommu::PrepareInterruptRemapping(bool extended, stduint reserved_irq_entries) {
	return iommu_backend && iommu_backend->prepare_interrupts(extended, reserved_irq_entries);
}

bool Iommu::CommitExtendedInterruptMode() {
	return iommu_backend && iommu_backend->commit_extended_mode();
}

bool Iommu::DisableInterruptRemapping() {
	return iommu_backend ? iommu_backend->disable_interrupts() : true;
}

bool Iommu::ExtendedDestinationMode() {
	return iommu_backend && iommu_backend->extended_destination();
}

bool Iommu::IoApicInterruptSource(uint8 id, IoApicSource& source) {
	return iommu_backend && iommu_backend->ioapic_source(id, source);
}

bool Iommu::AllocateIoApicInterruptLocked(const IoApicSource& source, const Devs::X86IoApicRoute& route,
	uint8 pin, InterruptRemappingHandle& handle, uint64& entry) {
	return iommu_backend && iommu_backend->allocate_ioapic(source, route, pin, handle, entry);
}

bool Iommu::AllocatePciInterrupt(DeviceNode* device, uint32 target_lapic_id, uint8 vector, MsiMessage& message) {
	return iommu_backend && iommu_backend->allocate_pci(device, target_lapic_id, vector, message);
}

void Iommu::ReleaseInterruptLocked(InterruptRemappingHandle handle) {
	if (iommu_backend) iommu_backend->release_interrupt(handle);
}

void Iommu::ReleaseInterrupt(InterruptRemappingHandle handle) {
	if (!handle) return;
	SpinlockLocal guard(Devs::x86_ioapic_route_lock());
	ReleaseInterruptLocked(handle);
}

enum class PciMessageAction : uint8 { Check, Disable, Prepare };

static bool CheckPciMessages(const DeviceNode* device, PciMessageAction action) {
	if (!device) return false;
	uni::PCI::Device dev{};
	dev.segment_group = device->fields.pci_segment;
	dev.bus = device->fields.pci_bus;
	dev.device = device->fields.pci_device;
	dev.function = device->fields.pci_function;
	uint8 pointer_offset = 0x34;
	if (action == PciMessageAction::Check) {
		// PCI Local Bus Specification, configuration headers: use the CardBus capability pointer for type 2.
		const uint32 command_status = uni::PCI::read_config_register(dev, 0x04);
		if (command_status == 0xFFFFFFFFu) return false;
		if (!(command_status & (1u << 20))) return true;
		const uint8 header_type = uint8(uni::PCI::read_config_register(dev, 0x0C) >> 16) & 0x7Fu;
		if (header_type > 2) return false;
		if (header_type == 2) pointer_offset = 0x14;
	}
	uint8 cap = uint8(uni::PCI::read_config_register(dev, pointer_offset));
	if (action == PciMessageAction::Check && !cap) return false;
	const uni::PCI pci{};
	for (uint8 count = 0; count < 48 && cap >= 0x40 && !(cap & 3u); ++count) {
		const uint32 value = uni::PCI::read_config_register(dev, cap);
		const uint8 id = uint8(value);
		if (id == pci.kCapabilityMSI || id == pci.kCapabilityMSIX) {
			const uint32 enable = id == pci.kCapabilityMSI ? (1u << 16) : (1u << 31);
			if (action == PciMessageAction::Check) {
				if (value & enable) return false;
			}
			else {
				uni::PCI::write_config_register(dev, cap, value & ~enable);
				if (uni::PCI::read_config_register(dev, cap) & enable) return false;
			}
			// Clear a stale upper address before programming a 32-bit MSI message.
			if (action == PciMessageAction::Prepare && id == pci.kCapabilityMSI && (value & (1u << 23)))
				uni::PCI::write_config_register(dev, cap + 8, 0);
		}
		cap = uint8(value >> 8);
	}
	return cap == 0;
}

bool Iommu::PciMessageSourcesQuiescent(const DeviceNode* root) {
	if (!root) return false;
	if (root->fields.node_type == uint16(DeviceNodeType::PciDevice) &&
		!CheckPciMessages(root, PciMessageAction::Check)) {
		const auto& pci = root->fields;
		plogwarn("[IOMMU] Cannot verify quiescent PCI messages for %04X:%02X:%02X.%u",
			unsigned(pci.pci_segment), unsigned(pci.pci_bus), unsigned(pci.pci_device), unsigned(pci.pci_function));
		return false;
	}
	for (const Nnode* child = root->link.subf; child; child = child->next) {
		if (!PciMessageSourcesQuiescent(reinterpret_cast<const DeviceNode*>(child))) return false;
	}
	return true;
}

bool Iommu::DisablePciMessageInterrupts(const DeviceNode* device) {
	return CheckPciMessages(device, PciMessageAction::Disable);
}

bool Iommu::PreparePciMessageInterrupts(const DeviceNode* device) {
	return CheckPciMessages(device, PciMessageAction::Prepare);
}

// The active IOMMU backend owns hardware mappings for the lifetime of each DMA buffer.
bool Iova::MapDeviceDma(DeviceNode* device, stduint physical,
	stduint length, DmaMappingFlag flags, DmaMapping& mapping) {
	mapping.device_address = uint64(physical);
	mapping.translated = false;
	const uint32 known_flags = _IMM(DmaMappingFlag::Identity);
	if (_IMM(flags) & ~known_flags) return false;
	if (!device || !physical || !length ||
		(physical & (PAGE_SIZE - 1)) || (length & (PAGE_SIZE - 1)))
		return false;
	const auto state = Iommu::GetDmaState();
	if (state != Iommu::DmaState::Active)
		return state == Iommu::DmaState::Inactive;
	if (device->fields.node_type != uint16(DeviceNodeType::PciDevice))
		return true;
	return Iommu::MapDeviceDma(device, physical, length, flags, mapping);
}

bool Iova::UnmapDeviceDma(DeviceNode* device, uint64 device_address,
	stduint length) {
	if (!device || !device_address || !length ||
		(device_address & (PAGE_SIZE - 1)) || (length & (PAGE_SIZE - 1)))
		return false;
	const auto state = Iommu::GetDmaState();
	if (state != Iommu::DmaState::Active)
		return state == Iommu::DmaState::Inactive;
	return Iommu::UnmapDeviceDma(device, device_address, length);
}

#endif
