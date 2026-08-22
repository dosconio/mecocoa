// ASCII g++ TAB4 LF
// ModuTitle: [Service] Device Management - Network
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include <cpp/System/Network/Layer/Link.hpp>
#include <cpp/System/Network/Layer/Link/Ethernet.hpp>

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	constexpr stduint NetLinkDeviceCapacity = 8;
	uni::Network::LinkDevice* net_link_devices[NetLinkDeviceCapacity]{};
	stduint net_link_device_count = 0;
	uint8 net_rx_buffer[2048];

	const char* EthernetTypeName(uint16 type) {
		switch (uni::Network::EthernetType(type)) {
		case uni::Network::EthernetType::ARP:
			return "arp";
		case uni::Network::EthernetType::IPv4:
			return "ipv4";
		case uni::Network::EthernetType::IPv6:
			return "ipv6";
		case uni::Network::EthernetType::Experiment:
			return "test";
		default:
			return "drop";
		}
	}

	void LogEthernetFrame(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		ploginfo("[Net] rx dev=%s dst=%[8H]:%[8H]:%[8H]:%[8H]:%[8H]:%[8H] src=%[8H]:%[8H]:%[8H]:%[8H]:%[8H]:%[8H] type=%[16H] %s payload=%u",
			dev.GetName() ? dev.GetName() : "(unnamed)",
			(stduint)frame.destination.octet[0], (stduint)frame.destination.octet[1],
			(stduint)frame.destination.octet[2], (stduint)frame.destination.octet[3],
			(stduint)frame.destination.octet[4], (stduint)frame.destination.octet[5],
			(stduint)frame.source.octet[0], (stduint)frame.source.octet[1],
			(stduint)frame.source.octet[2], (stduint)frame.source.octet[3],
			(stduint)frame.source.octet[4], (stduint)frame.source.octet[5],
			(stduint)frame.type, EthernetTypeName(frame.type), (unsigned)frame.payload_length);
	}

	void DispatchEthernetFrame(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		LogEthernetFrame(dev, frame);
		switch (uni::Network::EthernetType(frame.type)) {
		case uni::Network::EthernetType::ARP:
		case uni::Network::EthernetType::IPv4:
		case uni::Network::EthernetType::Experiment:
			return;
		default:
			return;
		}
	}

	void NetServicePoll() {
		for0(i, net_link_device_count) {
			auto* dev = net_link_devices[i];
			if (!dev || dev->GetState() != uni::Network::LinkState::Up) continue;
			uni::Network::LinkMutableFrameView frame{
				net_rx_buffer,
				sizeof(net_rx_buffer),
				0,
			};
			const stdsint len = dev->Receive(frame);
			if (len <= 0) continue;
			frame.length = stduint(len);
			uni::Network::LinkFrameView raw_frame{
				frame.data,
				frame.length,
			};
			uni::Network::EthernetFrameView eth_frame{};
			if (!uni::Network::ParseEthernetFrame(raw_frame, eth_frame)) {
				plogwarn("[Net] rx dev=%s malformed ethernet len=%u",
					dev->GetName() ? dev->GetName() : "(unnamed)",
					(unsigned)frame.length);
				continue;
			}
			DispatchEthernetFrame(*dev, eth_frame);
		}
	}

	void NetServiceIdleWait() {
		syscall(syscall_t::REST, 1, 10);
	}
}

bool Devsman::RegisterLinkDevice(uni::Network::LinkDevice* device) {
	if (!device) return false;
	for0(i, net_link_device_count) {
		if (net_link_devices[i] == device) return true;
	}
	if (net_link_device_count >= NetLinkDeviceCapacity) return false;
	net_link_devices[net_link_device_count++] = device;
	ploginfo("[Net] registered link device %s mtu=%u",
		device->GetName() ? device->GetName() : "(unnamed)",
		(unsigned)device->GetMtu());
	return true;
}

stduint Devsman::LinkDeviceCount() {
	return net_link_device_count;
}

uni::Network::LinkDevice* Devsman::GetLinkDevice(stduint index) {
	return index < net_link_device_count ? net_link_devices[index] : nullptr;
}

void serv_dev_net_loop() {
	ploginfo("[Net] Service thread start pid=%u", Taskman::CurrentPID());
	while (true) {
		NetServicePoll();
		NetServiceIdleWait();
	}
}

#endif
