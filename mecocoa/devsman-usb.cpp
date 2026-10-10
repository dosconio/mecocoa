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
#include <cpp/Device/USB/USB.hpp>
#include <cpp/Device/USB/USBHost-MSC.hpp>
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

	DeviceNode* ensure_usb_root_hub_node(const uni::device::SpaceUSB::USBHostControllerIdentity& controller);

	USBDeviceNodeInfo classify_usb_device(const uni::device::SpaceUSB::USBHostDevice& dev) {
		if (dev.DeviceClass() == 0x09u) {
			return {"usb-hub", "hub"};
		}
		return {"usb-device", "device"};
	}

	USBNodeClassInfo classify_usb_interface(const uni::device::SpaceUSB::InterfaceDescriptor& if_desc) {
		if (if_desc.interface_class == 0x08u && if_desc.interface_sub_class == 0x06u &&
			if_desc.interface_protocol == 0x50u) {
			return {if_desc.interface_class, if_desc.interface_sub_class, if_desc.interface_protocol,
				"usb-mass-storage", "mass-storage"};
		}
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

	constexpr int32 USBDisconnectedProbeResult = -0x5848;

	bool has_usb_device_descendant(DeviceNode* node) {
		if (!node) return false;
		for (auto* child = reinterpret_cast<DeviceNode*>(node->link.subf); child;
			child = reinterpret_cast<DeviceNode*>(child->link.next)) {
			if (DeviceNodeType(child->fields.node_type) == DeviceNodeType::UsbDevice ||
				has_usb_device_descendant(child)) return true;
		}
		return false;
	}

	void clear_disconnected_usb_binding(DeviceNode* node, void* driver_data) {
		if (!node) return;
		if (!driver_data) {
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Failed);
			node->fields.binding.probe_result = USBDisconnectedProbeResult;
			return;
		}
		if (node->fields.binding.driver_data == driver_data) {
			node->fields.binding.driver_data = nullptr;
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Failed);
			node->fields.binding.probe_result = USBDisconnectedProbeResult;
		}
		for (auto* child = reinterpret_cast<DeviceNode*>(node->link.subf); child;
			child = reinterpret_cast<DeviceNode*>(child->link.next)) {
			if (DeviceNodeType(child->fields.node_type) == DeviceNodeType::UsbDevice) continue;
			clear_disconnected_usb_binding(child, driver_data);
		}
	}

	bool is_deferred_disconnected_usb_device(DeviceNode* node) {
		return node && DeviceNodeType(node->fields.node_type) == DeviceNodeType::UsbDevice &&
			node->fields.binding.driver_data == nullptr &&
			node->fields.binding.probe_result == USBDisconnectedProbeResult;
	}

	DeviceNode* find_reusable_usb_device(DeviceNode* parent, const char* name) {
		if (!parent || !name) return nullptr;
		for (auto* child = reinterpret_cast<DeviceNode*>(parent->link.subf); child;
			child = reinterpret_cast<DeviceNode*>(child->link.next)) {
			if (DeviceNodeType(child->fields.node_type) != DeviceNodeType::UsbDevice ||
				!child->link.addr || StrCompare(child->link.addr, name) ||
				is_deferred_disconnected_usb_device(child)) continue;
			return child;
		}
		return nullptr;
	}

	void prune_disconnected_usb_ancestors(DeviceNode* node) {
		auto* current = node;
		while (current) {
			if (DeviceNodeType(current->fields.node_type) != DeviceNodeType::UsbDevice ||
				!is_deferred_disconnected_usb_device(current) ||
				has_usb_device_descendant(current)) {
				current = reinterpret_cast<DeviceNode*>(current->link.getParent());
				continue;
			}
			auto* parent = reinterpret_cast<DeviceNode*>(current->link.getParent());
			if (!parent || !detach_child(parent, current)) return;
			release_device_subtree(current);
			current = parent;
		}
	}

	void handle_usb_msc_event(uni::device::SpaceUSB::USBHost_MSC& driver,
		uni::device::SpaceUSB::USBHost_MSC::Event event, uint32 command_id,
		DeviceNode* interface_node);
	bool disconnect_usb_msc_storage(uni::device::SpaceUSB::USBHostDevice& device);
	void on_usb_msc_event(uni::device::SpaceUSB::USBHost_MSC& driver,
		uni::device::SpaceUSB::USBHost_MSC::Event event, uint32 command_id,
		void* context) {
		auto* dev = driver.ParentDevice();
		auto* node = find_usb_device_node_by_driver_data(Devsman::Root(), dev);
		const auto* location = find_resource(node, DeviceResourceType::UsbLocation, 0);
		const stduint port = location ? stduint(location->start) : 0;
		const stduint slot = location ? stduint(location->extra) : 0;
		handle_usb_msc_event(driver, event, command_id,
			static_cast<DeviceNode*>(context));
		if (event == uni::device::SpaceUSB::USBHost_MSC::Event::BringUpComplete) {
			if (driver.IsReady()) {
				ploginfo("USB MSC observer ready port=%u slot=%u interface=%u max-lun=%u blocks=%u block-size=%u commands=%u completions=%u",
					port, slot, (stduint)driver.InterfaceNumber(),
					(stduint)driver.MaxLun(), (stduint)driver.BlockCount(),
					(stduint)driver.BlockSize(), (stduint)driver.CommandCount(),
					(stduint)driver.CompletionCount());
			} else {
				plogwarn("USB MSC observer bring-up failed port=%u slot=%u interface=%u result=%s phase=%s stage=%s commands=%u completions=%u",
					port, slot, (stduint)driver.InterfaceNumber(), driver.ResultName(),
					driver.PhaseName(), driver.StageName(),
					(stduint)driver.CommandCount(),
					(stduint)driver.CompletionCount());
			}
			return;
		}
		// ploginfo("USB MSC observer command complete port=%u slot=%u interface=%u command=%u tag=%[32H] result=%s ready=%u",
		// 	port, slot, (stduint)driver.InterfaceNumber(), (stduint)command_id,
		// 	(stduint)driver.CommandTag(), driver.ResultName(),
		// 	driver.IsReady() ? 1u : 0u);
	}

	void register_usb_msc_observer(
		uni::device::SpaceUSB::USBHostDevice& dev,
		const uni::device::SpaceUSB::InterfaceDescriptor& descriptor,
		DeviceNode* interface_node) {
		if (descriptor.alternate_setting || descriptor.interface_class != 0x08u ||
			descriptor.interface_sub_class != 0x06u ||
			descriptor.interface_protocol != 0x50u) return;
		auto* class_driver = dev.FindClassDriver(
			uni::device::SpaceUSB::ClassDriverType::MassStorage,
			descriptor.interface_number);
		if (!class_driver) {
			plogwarn("USB MSC class driver missing interface=%u",
				(stduint)descriptor.interface_number);
			return;
		}
		static_cast<uni::device::SpaceUSB::USBHost_MSC*>(class_driver)->SetObserver(
			on_usb_msc_event, interface_node);
	}

	void ensure_usb_hub_downstream_ports(DeviceNode* usb_dev_node, uint8 num_ports) {
		if (!usb_dev_node || num_ports == 0) return;
		for (uint8 downstream_port = 1; downstream_port <= num_ports; ++downstream_port) {
			auto usb_port_name = String::newFormat("usb-port@%u", (stduint)downstream_port);
			Devsman::RegisterUSBPort(usb_dev_node, usb_port_name.reference(), downstream_port);
		}
	}

	void ensure_usb_hub_downstream_ports_for_device(uni::device::SpaceUSB::USBHostDevice& dev) {
		if (dev.DeviceClass() != 0x09u) return;
		auto* usb_hub_node = find_usb_device_node_by_driver_data(Devsman::Root(), &dev);
		ensure_usb_hub_downstream_ports(usb_hub_node, dev.HubNumPorts());
	}

	void register_single_usb_device(DeviceNode* usb_parent_node,
		const uni::device::SpaceUSB::USBHostDeviceLocation& location,
		uni::device::SpaceUSB::USBHostDevice& dev) {
		if (!usb_parent_node || !dev.IsInitialized()) return;
		const auto device_id = location.device_id;
		const auto port_num = location.upstream_port;
		auto dev_info = classify_usb_device(dev);
		auto usb_port_name = String::newFormat("usb-port@%u", (stduint)port_num);
		auto* usb_port_node = Devsman::RegisterUSBPort(usb_parent_node, usb_port_name.reference(), port_num);
		auto usb_dev_name = String::newFormat("usb-dev@port%u.slot%u", (stduint)port_num, (stduint)device_id);
		auto* usb_dev_node = Devsman::RegisterUSBDevice(usb_port_node, usb_dev_name.reference(),
			dev.VendorID(), dev.ProductID(),
			dev.ManufacturerString(), dev.ProductString(), dev.SerialString(),
			dev.DeviceClass(), dev.DeviceSubClass(), dev.DeviceProtocol(),
			port_num, device_id,
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
				register_usb_msc_observer(dev, *if_desc, usb_if_node);
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

	DeviceNode* ensure_usb_root_hub_node(const uni::device::SpaceUSB::USBHostControllerIdentity& controller) {
		auto* root = Devsman::Root();
		if (!root || !controller.driver_name || !controller.driver_data) return nullptr;
		auto* host_node = find_pci_device_node_by_driver_data(root, controller.driver_data);
		if (!host_node) return nullptr;
		auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x", 0,
			(stduint)host_node->fields.pci_bus,
			(stduint)host_node->fields.pci_device,
			(stduint)host_node->fields.pci_function);
		auto* usb_bus_node = Devsman::RegisterUSBBus(usb_bus_name.reference(),
			controller.driver_name, controller.driver_data);
		return Devsman::RegisterUSBRootHub(usb_bus_node, "usb-root-hub@0",
			0x09u, 0x00u, controller.root_hub_protocol,
			"usb-root-hub", controller.driver_data);
	}

	void on_usb_host_device_configured(const uni::device::SpaceUSB::USBHostControllerIdentity& controller,
		const uni::device::SpaceUSB::USBHostDeviceLocation& location,
		uni::device::SpaceUSB::USBHostDevice& dev) {
		auto* usb_root_hub_node = ensure_usb_root_hub_node(controller);
		if (!usb_root_hub_node) return;
		auto* usb_parent_node = usb_root_hub_node;
		if (location.parent_hub) {
			usb_parent_node = find_usb_device_node_by_driver_data(usb_root_hub_node, location.parent_hub);
		}
		register_single_usb_device(usb_parent_node, location, dev);
	}

	void on_usb_hub_descriptor_complete(uni::device::SpaceUSB::USBHostDevice& dev) {
		ensure_usb_hub_downstream_ports_for_device(dev);
	}

	void on_usb_hub_port_status(uni::device::SpaceUSB::USBHostDevice& dev,
		uint8 downstream_port, uint16 status, uint16 change) {
		(void)dev;
		(void)downstream_port;
		(void)status;
		(void)change;
	}

	void on_usb_host_device_disconnected(const uni::device::SpaceUSB::USBHostControllerIdentity& controller,
		const uni::device::SpaceUSB::USBHostDeviceLocation& location,
		uni::device::SpaceUSB::USBHostDevice& dev) {
		if (disconnect_usb_msc_storage(dev)) return;
		auto* usb_root_node = ensure_usb_root_hub_node(controller);
		if (!usb_root_node) return;
		DeviceNode* usb_parent_node = usb_root_node;
		if (location.parent_hub) {
			usb_parent_node = find_usb_device_node_by_driver_data(usb_root_node, location.parent_hub);
		}
		const uint8 upstream_port_num = location.upstream_port;
		auto* usb_dev_node = find_usb_device_node_by_driver_data(usb_parent_node, &dev);
		if (Devsman::RemoveUSBDevice(usb_dev_node)) {
			ploginfo("USB device disconnect handled on %s root-port=%u downstream-port=%u device=%u",
				controller.driver_name,
				(stduint)location.root_hub_port,
				(stduint)upstream_port_num,
				(stduint)location.device_id);
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
	#if _MCCA == 0x8664
	if (auto* node = find_reusable_usb_device(parent, name)) {
	#else
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbDevice, name)) {
	#endif
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
	#if _MCCA == 0x8664
	auto* node = find_reusable_usb_device(parent, name);
	if (!node) node = find_named_child(parent, DeviceNodeType::UsbDevice, name);
	#else
	auto* node = find_named_child(parent, DeviceNodeType::UsbDevice, name);
	#endif
	return node ? RemoveUSBDevice(node) : false;
}

bool Devsman::RemoveUSBDevice(DeviceNode* node) {
	initialize_device_tree();
	if (!node || DeviceNodeType(node->fields.node_type) != DeviceNodeType::UsbDevice)
		return false;
	auto* parent = reinterpret_cast<DeviceNode*>(node->link.getParent());
	if (!parent) return false;
	#if _MCCA == 0x8664
	if (has_usb_device_descendant(node)) {
		clear_disconnected_usb_binding(node, node->fields.binding.driver_data);
		return true;
	}
	#endif
	if (!detach_child(parent, node)) return false;
	release_device_subtree(node);
	#if _MCCA == 0x8664
	prune_disconnected_usb_ancestors(parent);
	#endif
	return true;
}

void Devsman::RegisterUSBDeviceTreeHooks() {
	#if _MCCA == 0x8664
	uni::device::SpaceUSB::g_host_device_configured_hook = on_usb_host_device_configured;
	uni::device::SpaceUSB::g_host_device_disconnected_hook = on_usb_host_device_disconnected;
	uni::device::SpaceUSB::g_hub_descriptor_complete_hook = on_usb_hub_descriptor_complete;
	uni::device::SpaceUSB::g_hub_port_status_hook = on_usb_hub_port_status;
	#endif
}

#endif
