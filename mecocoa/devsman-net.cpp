// ASCII g++ TAB4 LF
// ModuTitle: [Service] Device Management - Network
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include <cpp/System/Network/Layer/Link.hpp>
#include <cpp/System/Network/Layer/Link/Ethernet.hpp>
#include <cpp/System/Network/Layer/Network/ARP.hpp>
#include <cpp/System/Network/Layer/Network/IPv4.hpp>
#include <cpp/System/Network/Layer/Network/IPv4/ICMP.hpp>

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	constexpr stduint NetLinkDeviceCapacity = 8;
	constexpr stduint NetFrameBufferSize = 2048;
	uni::Network::LinkDevice* net_link_devices[NetLinkDeviceCapacity]{};
	stduint net_link_device_count = 0;

	struct NetInterfaceConfig {
		uni::Network::IPv4Address ipv4_address;
		uni::Network::IPv4Address ipv4_netmask;
		uni::Network::IPv4Address ipv4_gateway;
	};

	NetInterfaceConfig net_config{
		.ipv4_address = {{ 10, 0, 2, 15 }},
		.ipv4_netmask = {{ 255, 255, 255, 0 }},
		.ipv4_gateway = {{ 10, 0, 2, 1 }},
	};

	struct NetServiceBuffers {
		uint8* rx;
		uint8* tx;

		bool IsReady() const {
			return rx && tx;
		}
	};

	NetServiceBuffers net_buffers{};

	bool EnsureNetServiceBuffers() {
		if (!net_buffers.rx) net_buffers.rx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		if (!net_buffers.tx) net_buffers.tx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		return net_buffers.IsReady();
	}

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

	void LogIPv4Address(const char* prefix, const uni::Network::IPv4Address& address) {
		ploginfo("%s%u.%u.%u.%u", prefix,
			(unsigned)address.octet[0], (unsigned)address.octet[1],
			(unsigned)address.octet[2], (unsigned)address.octet[3]);
	}

	void HandleArpFrame(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		uni::Network::ArpEthernetIPv4View arp{};
		if (!uni::Network::ParseArpEthernetIPv4(frame, arp)) {
			plogwarn("[Net] arp malformed");
			return;
		}
		if (arp.operation != uint16(uni::Network::ArpOperation::Request)) return;
		if (!(arp.target_protocol == net_config.ipv4_address)) return;
		const auto local_mac = dev.GetAddress();
		const stduint reply_len = uni::Network::BuildArpEthernetIPv4Reply(net_buffers.tx, NetFrameBufferSize,
			local_mac, net_config.ipv4_address, arp.sender_hardware, arp.sender_protocol);
		if (!reply_len) {
			plogwarn("[Net] arp reply build failed");
			return;
		}
		uni::Network::LinkFrameView reply{
			net_buffers.tx,
			reply_len,
		};
		const stdsint sent = dev.Send(reply);
		if (sent <= 0) {
			plogwarn("[Net] arp reply send failed");
		}
		// if (sent > 0) LogIPv4Address("[Net] arp reply ", arp.sender_protocol);
	}

	void HandleIPv4Frame(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		uni::Network::IPv4PacketView ipv4{};
		if (!uni::Network::ParseIPv4Packet(frame, ipv4)) {
			plogwarn("[Net] ipv4 malformed");
			return;
		}
		if (!(ipv4.destination == net_config.ipv4_address)) return;
		if (ipv4.protocol != uint8(uni::Network::IPv4Protocol::ICMP)) return;
		uni::Network::ICMPv4EchoView echo{};
		if (!uni::Network::ParseICMPv4EchoRequest(ipv4, echo)) return;
		const auto local_mac = dev.GetAddress();
		const stduint reply_len = uni::Network::BuildICMPv4EchoReply(net_buffers.tx, NetFrameBufferSize,
			local_mac, frame.source, net_config.ipv4_address, ipv4);
		if (!reply_len) {
			plogwarn("[Net] icmp echo reply build failed");
			return;
		}
		uni::Network::LinkFrameView reply{
			net_buffers.tx,
			reply_len,
		};
		const stdsint sent = dev.Send(reply);
		if (sent <= 0) {
			plogwarn("[Net] icmp echo reply send failed");
		}
		// if (sent > 0) LogIPv4Address("[Net] icmp echo reply ", ipv4.source);
	}

	void DispatchEthernetFrame(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		// LogEthernetFrame(dev, frame);
		switch (uni::Network::EthernetType(frame.type)) {
		case uni::Network::EthernetType::ARP:
			HandleArpFrame(dev, frame);
			return;
		case uni::Network::EthernetType::IPv4:
			HandleIPv4Frame(dev, frame);
			return;
		case uni::Network::EthernetType::Experiment:
			return;
		default:
			return;
		}
	}

	void NetServicePoll() {
		if (!EnsureNetServiceBuffers()) {
			plogwarn("[Net] frame buffer allocation failed");
			return;
		}
		for0(i, net_link_device_count) {
			auto* dev = net_link_devices[i];
			if (!dev || dev->GetState() != uni::Network::LinkState::Up) continue;
			uni::Network::LinkMutableFrameView frame{
				net_buffers.rx,
				NetFrameBufferSize,
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
