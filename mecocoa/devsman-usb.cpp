// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <cpp/Device/Bus/ISA.hpp>
#include <cpp/Device/Bus/PCI.hpp>
#include <c/storage/AHCI.h>
#include <c/storage/NVMe.h>
#include <cpp/System/Audiosys/AudioManager.hpp>
#if _MCCA == 0x8664
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#endif
#if (_MCCA & 0xFF00) == 0x8600
#include "c/proctrl/IAx86_64.ext.h"
#include "c/proctrl/IAx86_64.msr.h"
#endif

#if (_MCCA & 0xFF00) == 0x8600

namespace Devs {
	void initialize_device_tree();
	void release_device_subtree(DeviceNode* node);
	DeviceNode* get_bus_root(DeviceBusType bus_type);
	bool detach_child(DeviceNode* parent, DeviceNode* child);
	bool set_driver_binding(DeviceNode* node, const char* driver_name);
	DeviceNode* find_named_child(DeviceNode* parent, DeviceNodeType node_type, const char* name);
	const DeviceResource* find_resource(const DeviceNode* node, DeviceResourceType type, uint32 index);
	DeviceNode* append_plain_device(DeviceNode* parent, DeviceNodeType node_type, DeviceBusType bus_type, const char* name);
	bool append_resource(DeviceNode* node, DeviceResourceType type, uint16 flags, uint32 index, uint64 start, uint64 length, uint64 extra);

	#if _MCCA == 0x8664
	struct USBNodeClassInfo {
		uint8 class_base;
		uint8 class_sub;
		uint8 class_if;
		const char* driver_name;
		const char* kind_name;
	};

	struct USBDeviceNodeInfo {
		const char* driver_name;
		const char* kind_name;
	};

	DeviceNode* ensure_xhci_root_hub_node(uni::device::SpaceUSB3::HostController& xhc);

	USBDeviceNodeInfo classify_usb_device(const uni::device::SpaceUSB3::USBHostDevice_v3& dev) {
		if (dev.DeviceClass() == 0x09u) {
			return {"usb-hub", "hub"};
		}
		return {"usb-device", "device"};
	}

	USBNodeClassInfo classify_usb_interface(const uni::device::SpaceUSB::InterfaceDescriptor& if_desc) {
		if (if_desc.interface_class == 3 && if_desc.interface_sub_class == 1) {
			if (if_desc.interface_protocol == 1) {
				return {if_desc.interface_class, if_desc.interface_sub_class, if_desc.interface_protocol,
					"usb-hid-keyboard", "keyboard"};
			}
			if (if_desc.interface_protocol == 2) {
				return {if_desc.interface_class, if_desc.interface_sub_class, if_desc.interface_protocol,
					"usb-hid-mouse", "mouse"};
			}
		}
		return {if_desc.interface_class, if_desc.interface_sub_class, if_desc.interface_protocol,
			"usb-interface", "interface"};
	}

	DeviceNode* find_device_node_by_driver_data(DeviceNode* parent, uint16 node_type, void* driver_data) {
		if (!parent || !driver_data) return nullptr;
		for (auto* node = reinterpret_cast<DeviceNode*>(parent->link.subf); node; node = reinterpret_cast<DeviceNode*>(node->link.next)) {
			if (!node) continue;
			if (node->fields.node_type == node_type &&
				node->fields.binding.driver_data == driver_data) {
				return node;
			}
			if (auto* found = find_device_node_by_driver_data(node, node_type, driver_data)) {
				return found;
			}
		}
		return nullptr;
	}

	DeviceNode* find_usb_device_node_by_driver_data(DeviceNode* parent, void* driver_data) {
		return find_device_node_by_driver_data(parent, uint16(DeviceNodeType::UsbDevice), driver_data);
	}

	DeviceNode* find_pci_device_node_by_driver_data(DeviceNode* parent, void* driver_data) {
		return find_device_node_by_driver_data(parent, uint16(DeviceNodeType::PciDevice), driver_data);
	}

	void ensure_usb_hub_downstream_ports(DeviceNode* usb_dev_node, uint8 num_ports) {
		if (!usb_dev_node || num_ports == 0) return;
		for (uint8 downstream_port = 1; downstream_port <= num_ports; ++downstream_port) {
			auto usb_port_name = String::newFormat("usb-port@%u", (stduint)downstream_port);
			Devsman::RegisterUSBPort(usb_dev_node, usb_port_name.reference(), downstream_port);
		}
	}

	void ensure_usb_hub_downstream_ports_for_device(uni::device::SpaceUSB3::HostController& xhc,
		uni::device::SpaceUSB3::USBHostDevice_v3& dev) {
		if (dev.DeviceClass() != 0x09u) return;
		auto* usb_root_hub_node = ensure_xhci_root_hub_node(xhc);
		if (!usb_root_hub_node) return;
		auto* usb_hub_node = find_usb_device_node_by_driver_data(usb_root_hub_node, &dev);
		ensure_usb_hub_downstream_ports(usb_hub_node, dev.HubNumPorts());
	}

	void register_single_usb_device_for_xhci(DeviceNode* usb_parent_node,
		uint8 port_num, uni::device::SpaceUSB3::USBHostDevice_v3& dev) {
		if (!usb_parent_node || !dev.IsInitialized()) return;
		const auto slot_id = dev.SlotID();
		auto dev_info = classify_usb_device(dev);
		auto usb_port_name = String::newFormat("usb-port@%u", (stduint)port_num);
		auto* usb_port_node = Devsman::RegisterUSBPort(usb_parent_node, usb_port_name.reference(), port_num);
		auto usb_dev_name = String::newFormat("usb-dev@port%u.slot%u", (stduint)port_num, (stduint)slot_id);
		auto* usb_dev_node = Devsman::RegisterUSBDevice(usb_port_node, usb_dev_name.reference(),
			dev.VendorID(), dev.ProductID(),
			dev.ManufacturerString(), dev.ProductString(), dev.SerialString(),
			dev.DeviceClass(), dev.DeviceSubClass(), dev.DeviceProtocol(),
			port_num, slot_id,
			dev_info.driver_name, &dev);
		if (dev.DeviceClass() == 0x09u) {
			ensure_usb_hub_downstream_ports(usb_dev_node, dev.HubNumPorts());
		}
		auto* desc = dev.Buffer();
		if (!usb_dev_node || !desc) return;
		const auto* conf_desc = uni::device::SpaceUSB::DescriptorDynamicCast<uni::device::SpaceUSB::ConfigurationDescriptor>(desc);
		if (!conf_desc || conf_desc->total_length < conf_desc->length) return;
		const auto* p = desc + conf_desc->length;
		const auto* end = desc + conf_desc->total_length;
		while (p + 2 <= end) {
			const uint8 len = p[0];
			if (len == 0 || p + len > end) break;
			if (auto* if_desc = uni::device::SpaceUSB::DescriptorDynamicCast<uni::device::SpaceUSB::InterfaceDescriptor>(p)) {
				auto info = classify_usb_interface(*if_desc);
				auto usb_if_name = String::newFormat("usb-if@%u", (stduint)if_desc->interface_number);
				auto* usb_if_node = Devsman::RegisterUSBInterface(usb_dev_node, usb_if_name.reference(),
					info.class_base, info.class_sub, info.class_if,
					info.driver_name, &dev);
				const auto* q = p + len;
				uint32 ep_index = 0;
				while (q + 2 <= end) {
					const uint8 qlen = q[0];
					if (qlen == 0 || q + qlen > end) break;
					if (uni::device::SpaceUSB::DescriptorDynamicCast<uni::device::SpaceUSB::InterfaceDescriptor>(q)) {
						break;
					}
					if (auto* ep_desc = uni::device::SpaceUSB::DescriptorDynamicCast<uni::device::SpaceUSB::EndpointDescriptor>(q)) {
						Devsman::AddUSBEndpointResource(usb_if_node, ep_index++,
							ep_desc->endpoint_address.data,
							ep_desc->attributes.bits.transfer_type,
							ep_desc->max_packet_size,
							ep_desc->interval);
					}
					q += qlen;
				}
			}
			p += len;
		}
	}

	DeviceNode* ensure_xhci_root_hub_node(uni::device::SpaceUSB3::HostController& xhc) {
		auto* root = Devsman::Root();
		if (!root) return nullptr;
		auto* xhc_node = find_pci_device_node_by_driver_data(root, &xhc);
		if (!xhc_node) return nullptr;
		auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x", 0,
			(stduint)xhc_node->fields.pci_bus,
			(stduint)xhc_node->fields.pci_device,
			(stduint)xhc_node->fields.pci_function);
		auto* usb_bus_node = Devsman::RegisterUSBBus(usb_bus_name.reference(), "xhci", &xhc);
		return Devsman::RegisterUSBRootHub(usb_bus_node, "usb-root-hub@0", 0x09u, 0x00u, 0x03u, "usb-root-hub", &xhc);
	}

	void on_xhci_complete_configuration(uni::device::SpaceUSB3::HostController& xhc, uint8 port_id, uint8, uni::device::SpaceUSB3::USBHostDevice_v3& dev) {
		auto* usb_root_hub_node = ensure_xhci_root_hub_node(xhc);
		if (!usb_root_hub_node) return;
		if (dev.ParentHubSlotID() != 0) {
			auto* parent_hub_dev = xhc.GetDeviceManager()->FindBySlot(dev.ParentHubSlotID());
			auto* parent_hub_node = find_usb_device_node_by_driver_data(usb_root_hub_node, parent_hub_dev);
			register_single_usb_device_for_xhci(parent_hub_node, dev.UpstreamPortNum(), dev);
			return;
		}
		register_single_usb_device_for_xhci(usb_root_hub_node, port_id, dev);
	}

	void on_usb_hub_descriptor_complete(uni::device::SpaceUSB::USBHostDevice& base_dev) {
		auto& dev = static_cast<uni::device::SpaceUSB3::USBHostDevice_v3&>(base_dev);
		auto* xhc = dev.Controller();
		if (!xhc) return;
		ensure_usb_hub_downstream_ports_for_device(*xhc, dev);
	}

	void on_usb_hub_port_status(uni::device::SpaceUSB::USBHostDevice& base_dev,
		uint8 downstream_port, uint16 status, uint16 change) {
		(void)change;
		auto& dev = static_cast<uni::device::SpaceUSB3::USBHostDevice_v3&>(base_dev);
		if ((status & 0x0001u) == 0) return;
	}

	void on_xhci_device_disconnect(uni::device::SpaceUSB3::HostController& xhc, uint8 port_id, uint8 slot_id) {
		auto* usb_root_node = ensure_xhci_root_hub_node(xhc);
		if (!usb_root_node) return;
		auto* dev = xhc.GetDeviceManager()->FindBySlot(slot_id);
		DeviceNode* usb_parent_node = usb_root_node;
		uint8 upstream_port_num = port_id;
		if (dev && dev->ParentHubSlotID() != 0) {
			auto* parent_hub_dev = xhc.GetDeviceManager()->FindBySlot(dev->ParentHubSlotID());
			usb_parent_node = find_usb_device_node_by_driver_data(usb_root_node, parent_hub_dev);
			upstream_port_num = dev->UpstreamPortNum();
		}
		auto usb_port_name = String::newFormat("usb-port@%u", (stduint)upstream_port_num);
		auto* usb_port_node = Devsman::RegisterUSBPort(usb_parent_node, usb_port_name.reference(), upstream_port_num);
		auto usb_dev_name = String::newFormat("usb-dev@port%u.slot%u", (stduint)upstream_port_num, (stduint)slot_id);
		if (Devsman::RemoveUSBDevice(usb_port_node, usb_dev_name.reference())) {
			if (dev && dev->ParentHubSlotID() != 0) {
				ploginfo("USB device detached from xHC root-port=%u hub-slot=%u downstream-port=%u child-slot=%u",
					(stduint)port_id,
					(stduint)dev->ParentHubSlotID(),
					(stduint)upstream_port_num,
					(stduint)slot_id);
			} else {
				ploginfo("USB device detached from xHC root-port=%u slot=%u",
					(stduint)port_id, (stduint)slot_id);
			}
		}
	}
	#endif

}

using namespace Devs;


DeviceNode* Devsman::RegisterUSBBus(const char* name, const char* driver_name, void* driver_data) {
	initialize_device_tree();
	auto* root = get_bus_root(DeviceBusType::USB);
	if (!root || !name) return nullptr;
	if (auto* node = find_named_child(root, DeviceNodeType::UsbBus, name)) {
		if (driver_name) {
			set_driver_binding(node, driver_name);
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.driver_data = driver_data;
		}
		return node;
	}
	auto* node = append_plain_device(root, DeviceNodeType::UsbBus, DeviceBusType::USB, StrHeap(name));
	if (driver_name) {
		set_driver_binding(node, driver_name);
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.driver_data = driver_data;
	}
	return node;
}

DeviceNode* Devsman::RegisterUSBRootHub(DeviceNode* parent, const char* name,
	uint8 class_base, uint8 class_sub, uint8 class_if,
	const char* driver_name, void* driver_data) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbRootHub, name)) {
		node->fields.class_base = class_base;
		node->fields.class_sub = class_sub;
		node->fields.class_if = class_if;
		node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
		if (driver_name) {
			set_driver_binding(node, driver_name);
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.driver_data = driver_data;
		}
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::UsbRootHub, DeviceBusType::USB, StrHeap(name));
	node->fields.class_base = class_base;
	node->fields.class_sub = class_sub;
	node->fields.class_if = class_if;
	node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
	if (driver_name) {
		set_driver_binding(node, driver_name);
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.driver_data = driver_data;
	}
	return node;
}

DeviceNode* Devsman::RegisterUSBPort(DeviceNode* parent, const char* name, uint8 port_num) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbPort, name)) {
		if (!find_resource(node, DeviceResourceType::UsbLocation, 0)) {
			append_resource(node, DeviceResourceType::UsbLocation, DeviceResourceFlag_None, 0, port_num, 1, 0);
		}
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::UsbPort, DeviceBusType::USB, StrHeap(name));
	append_resource(node, DeviceResourceType::UsbLocation, DeviceResourceFlag_None, 0, port_num, 1, 0);
	return node;
}

DeviceNode* Devsman::RegisterUSBDevice(DeviceNode* parent, const char* name,
	uint16 vendor_id, uint16 product_id,
	const char* text_manufacturer, const char* text_product, const char* text_serial,
	uint8 class_base, uint8 class_sub, uint8 class_if,
	uint8 port_num, uint8 slot_id,
	const char* driver_name, void* driver_data) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbDevice, name)) {
		node->fields.vendor_id = vendor_id;
		node->fields.device_id = product_id;
		node->fields.text_manufacturer = text_manufacturer ? StrHeap(text_manufacturer) : nullptr;
		node->fields.text_product = text_product ? StrHeap(text_product) : nullptr;
		node->fields.text_serial = text_serial ? StrHeap(text_serial) : nullptr;
		node->fields.class_base = class_base;
		node->fields.class_sub = class_sub;
		node->fields.class_if = class_if;
		node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
		if (!find_resource(node, DeviceResourceType::UsbLocation, 0)) {
			append_resource(node, DeviceResourceType::UsbLocation, DeviceResourceFlag_None, 0, port_num, 1, slot_id);
		}
		if (driver_name) {
			set_driver_binding(node, driver_name);
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.driver_data = driver_data;
		}
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::UsbDevice, DeviceBusType::USB, StrHeap(name));
	node->fields.vendor_id = vendor_id;
	node->fields.device_id = product_id;
	node->fields.text_manufacturer = text_manufacturer ? StrHeap(text_manufacturer) : nullptr;
	node->fields.text_product = text_product ? StrHeap(text_product) : nullptr;
	node->fields.text_serial = text_serial ? StrHeap(text_serial) : nullptr;
	node->fields.class_base = class_base;
	node->fields.class_sub = class_sub;
	node->fields.class_if = class_if;
	node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
	append_resource(node, DeviceResourceType::UsbLocation, DeviceResourceFlag_None, 0, port_num, 1, slot_id);
	if (driver_name) {
		set_driver_binding(node, driver_name);
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.driver_data = driver_data;
	}
	return node;
}

DeviceNode* Devsman::RegisterUSBInterface(DeviceNode* parent, const char* name,
	uint8 class_base, uint8 class_sub, uint8 class_if,
	const char* driver_name, void* driver_data) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbInterface, name)) {
		node->fields.class_base = class_base;
		node->fields.class_sub = class_sub;
		node->fields.class_if = class_if;
		node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
		if (driver_name) {
			set_driver_binding(node, driver_name);
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.driver_data = driver_data;
		}
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::UsbInterface, DeviceBusType::USB, StrHeap(name));
	node->fields.class_base = class_base;
	node->fields.class_sub = class_sub;
	node->fields.class_if = class_if;
	node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
	if (driver_name) {
		set_driver_binding(node, driver_name);
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.driver_data = driver_data;
	}
	return node;
}

bool Devsman::AddUSBEndpointResource(DeviceNode* node, uint32 index,
	uint8 endpoint_addr, uint8 transfer_type, uint16 max_packet_size, uint8 interval) {
	if (!node) return false;
	if (find_resource(node, DeviceResourceType::UsbEndpoint, index)) return true;
	return append_resource(node, DeviceResourceType::UsbEndpoint, DeviceResourceFlag_None,
		index, endpoint_addr, max_packet_size, uint64(transfer_type) | (uint64(interval) << 8));
}

bool Devsman::RemoveUSBDevice(DeviceNode* parent, const char* name) {
	initialize_device_tree();
	if (!parent || !name) return false;
	auto* node = find_named_child(parent, DeviceNodeType::UsbDevice, name);
	if (!node) return false;
	if (!detach_child(parent, node)) return false;
	release_device_subtree(node);
	return true;
}

void Devsman::RegisterXHCIDeviceTreeHook() {
	#if _MCCA == 0x8664
	uni::device::SpaceUSB3::g_configuration_complete_hook = on_xhci_complete_configuration;
	uni::device::SpaceUSB3::g_device_disconnect_hook = on_xhci_device_disconnect;
	uni::device::SpaceUSB::g_hub_descriptor_complete_hook = on_usb_hub_descriptor_complete;
	uni::device::SpaceUSB::g_hub_port_status_hook = on_usb_hub_port_status;
	#endif
}

#endif
