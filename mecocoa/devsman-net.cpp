// ASCII g++ TAB4 LF
// ModuTitle: [Service] Device Management - Network
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include <cpp/System/Network/Layer/Link.hpp>
#include <cpp/System/Network/Layer/Link/Ethernet.hpp>
#include <cpp/System/Network/Layer/Network/ARP.hpp>
#include <cpp/System/Network/Layer/Network/IPv4.hpp>
#include <cpp/System/Network/Layer/Network/IPv4/ICMP.hpp>
#include <cpp/System/Network/Layer/Transport/UDP.hpp>

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	constexpr stduint NetLinkDeviceCapacity = 8;
	constexpr stduint NetFrameBufferSize = 2048;
	constexpr stduint NetUdpPortCapacity = 8;
	constexpr stduint NetUdpInboxPortCapacity = 4;
	constexpr stduint NetUdpInboxDepth = 4;
	constexpr stduint NetUdpInboxPayloadSize = 512;
	constexpr stduint NetArpCacheCapacity = 8;
	constexpr stduint NetPendingUdpCapacity = 4;
	constexpr stduint NetPendingUdpPayloadSize = 1472;
	constexpr uint16 NetUdpEchoPort = 7;
	constexpr uint16 NetUdpEphemeralPortBegin = 49152;
	constexpr uint16 NetUdpEphemeralPortEnd = 65535;
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
		uint8* udp_tx;

		bool IsReady() const {
			return rx && tx;
		}

		bool IsUdpTxReady() const {
			return udp_tx != nullptr;
		}
	};

	NetServiceBuffers net_buffers{};
	uint16 net_ipv4_identification = 1;

	struct NetUdpPacket {
		const uni::Network::EthernetFrameView* ethernet;
		const uni::Network::IPv4PacketView* ipv4;
		const uni::Network::UDPDatagramView* udp;
		uni::Network::UDPDatagramContext context;
	};

	struct NetArpCacheEntry {
		uni::Network::IPv4Address protocol;
		uni::Network::MacAddress hardware;
		bool valid;
	};

	struct NetPendingUdpPacket {
		uni::Network::IPv4Address target_ip;
		uint16 source_port;
		uint16 destination_port;
		stduint payload_length;
		bool valid;
	};

	using NetUdpPortHandler = bool (*)(uni::Network::LinkDevice& dev, const NetUdpPacket& packet);

	struct NetUdpPortBinding {
		uint16 port;
		NetUdpPortHandler handler;
	};

	struct NetUdpInboxEntry {
		uni::Network::UDPDatagramContext context;
	};

	struct NetUdpInbox {
		uint16 port;
		NetUdpInboxEntry* entries;
		uint8* payloads;
		stduint head;
		stduint tail;
		stduint count;
		stduint drops;

		bool IsReady() const {
			return entries && payloads;
		}
	};

	NetUdpPortBinding net_udp_ports[NetUdpPortCapacity]{};
	stduint net_udp_port_count = 0;
	NetUdpInbox net_udp_inboxes[NetUdpInboxPortCapacity]{};
	stduint net_udp_inbox_count = 0;
	NetArpCacheEntry net_arp_cache[NetArpCacheCapacity]{};
	stduint net_arp_cache_next = 0;
	NetPendingUdpPacket net_pending_udp[NetPendingUdpCapacity]{};
	uint8* net_pending_udp_payloads = nullptr;

	bool EnsureNetServiceBuffers() {
		if (!net_buffers.rx) net_buffers.rx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		if (!net_buffers.tx) net_buffers.tx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		return net_buffers.IsReady();
	}

	bool EnsureUdpTxBuffer() {
		if (!net_buffers.udp_tx) net_buffers.udp_tx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		return net_buffers.IsUdpTxReady();
	}

	uni::Network::LinkDevice* FindDefaultLinkDevice() {
		for0(i, net_link_device_count) {
			auto* dev = net_link_devices[i];
			if (dev && dev->GetState() == uni::Network::LinkState::Up) return dev;
		}
		return nullptr;
	}

	bool IsSameIPv4Subnet(const uni::Network::IPv4Address& lhs, const uni::Network::IPv4Address& rhs,
		const uni::Network::IPv4Address& mask) {
		for0(i, uni::Network::IPv4AddressLength) {
			if ((lhs.octet[i] & mask.octet[i]) != (rhs.octet[i] & mask.octet[i])) return false;
		}
		return true;
	}

	bool LookupArpCache(const uni::Network::IPv4Address& protocol, uni::Network::MacAddress& hardware) {
		for0(i, NetArpCacheCapacity) {
			const auto& entry = net_arp_cache[i];
			if (entry.valid && entry.protocol == protocol) {
				hardware = entry.hardware;
				return true;
			}
		}
		return false;
	}

	void LearnArpCache(const uni::Network::IPv4Address& protocol, const uni::Network::MacAddress& hardware) {
		if (protocol.IsZero() || hardware.IsZero() || hardware.IsBroadcast()) return;
		for0(i, NetArpCacheCapacity) {
			auto& entry = net_arp_cache[i];
			if (entry.valid && entry.protocol == protocol) {
				entry.hardware = hardware;
				return;
			}
		}
		for0(i, NetArpCacheCapacity) {
			auto& entry = net_arp_cache[i];
			if (!entry.valid) {
				entry.protocol = protocol;
				entry.hardware = hardware;
				entry.valid = true;
				return;
			}
		}
		auto& entry = net_arp_cache[net_arp_cache_next];
		entry.protocol = protocol;
		entry.hardware = hardware;
		entry.valid = true;
		net_arp_cache_next = (net_arp_cache_next + 1) % NetArpCacheCapacity;
	}

	uint8* GetPendingUdpPayloadSlot(stduint index) {
		return net_pending_udp_payloads + index * NetPendingUdpPayloadSize;
	}

	bool EnsurePendingUdpStorage() {
		if (!net_pending_udp_payloads) {
			net_pending_udp_payloads = (uint8*)mempool.allocate(NetPendingUdpPayloadSize * NetPendingUdpCapacity, 12);
		}
		return net_pending_udp_payloads != nullptr;
	}

	void ClearPendingUdp(stduint index) {
		if (index < NetPendingUdpCapacity) net_pending_udp[index].valid = false;
	}

	stdsint EnqueuePendingUdp(const uni::Network::IPv4Address& target_ip,
		uint16 source_port, uint16 destination_port, const void* payload, stduint length) {
		if ((!payload && length) || length > NetPendingUdpPayloadSize) return -1;
		if (!EnsurePendingUdpStorage()) return -1;
		for0(i, NetPendingUdpCapacity) {
			auto& packet = net_pending_udp[i];
			if (packet.valid) continue;
			packet.target_ip = target_ip;
			packet.source_port = source_port;
			packet.destination_port = destination_port;
			packet.payload_length = length;
			packet.valid = true;
			auto* slot = GetPendingUdpPayloadSlot(i);
			const auto* payload_bytes = reinterpret_cast<const uint8*>(payload);
			for0(j, length) slot[j] = payload_bytes[j];
			return stdsint(i);
		}
		return -1;
	}

	bool SendArpRequest(uni::Network::LinkDevice& dev, uint8* buffer, const uni::Network::IPv4Address& target_ip) {
		if (!buffer) return false;
		const stduint request_len = uni::Network::BuildArpEthernetIPv4Request(buffer, NetFrameBufferSize,
			dev.GetAddress(), net_config.ipv4_address, target_ip);
		if (!request_len) return false;
		uni::Network::LinkFrameView request{
			buffer,
			request_len,
		};
		return dev.Send(request) > 0;
	}

	bool RegisterUdpPortHandler(uint16 port, NetUdpPortHandler handler) {
		if (!port || !handler) return false;
		for0(i, net_udp_port_count) {
			if (net_udp_ports[i].port == port) {
				net_udp_ports[i].handler = handler;
				return true;
			}
		}
		if (net_udp_port_count >= NetUdpPortCapacity) return false;
		net_udp_ports[net_udp_port_count++] = { port, handler };
		return true;
	}

	bool UnregisterUdpPortHandler(uint16 port, NetUdpPortHandler handler) {
		for0(i, net_udp_port_count) {
			if (net_udp_ports[i].port != port) continue;
			if (handler && net_udp_ports[i].handler != handler) return false;
			net_udp_ports[i] = net_udp_ports[net_udp_port_count - 1];
			net_udp_ports[net_udp_port_count - 1] = {};
			--net_udp_port_count;
			return true;
		}
		return false;
	}

	NetUdpPortHandler FindUdpPortHandler(uint16 port) {
		for0(i, net_udp_port_count) {
			if (net_udp_ports[i].port == port) return net_udp_ports[i].handler;
		}
		return nullptr;
	}

	uint8* GetUdpInboxPayloadSlot(NetUdpInbox& inbox, stduint index) {
		return inbox.payloads + index * NetUdpInboxPayloadSize;
	}

	bool EnsureUdpInboxStorage(NetUdpInbox& inbox) {
		if (!inbox.entries) inbox.entries = (NetUdpInboxEntry*)mempool.allocate(sizeof(NetUdpInboxEntry) * NetUdpInboxDepth, 4);
		if (!inbox.payloads) inbox.payloads = (uint8*)mempool.allocate(NetUdpInboxPayloadSize * NetUdpInboxDepth, 12);
		return inbox.IsReady();
	}

	NetUdpInbox* FindUdpInbox(uint16 port) {
		for0(i, net_udp_inbox_count) {
			if (net_udp_inboxes[i].port == port) return &net_udp_inboxes[i];
		}
		return nullptr;
	}

	bool IsUdpPortAvailable(uint16 port) {
		return port && !FindUdpPortHandler(port) && !FindUdpInbox(port);
	}

	NetUdpInbox* EnsureUdpInbox(uint16 port) {
		if (!port) return nullptr;
		if (auto* inbox = FindUdpInbox(port)) return inbox;
		if (net_udp_inbox_count >= NetUdpInboxPortCapacity) return nullptr;
		auto* inbox = &net_udp_inboxes[net_udp_inbox_count++];
		inbox->port = port;
		inbox->entries = nullptr;
		inbox->payloads = nullptr;
		inbox->head = 0;
		inbox->tail = 0;
		inbox->count = 0;
		inbox->drops = 0;
		return inbox;
	}

	bool ReleaseUdpInbox(uint16 port) {
		for0(i, net_udp_inbox_count) {
			if (net_udp_inboxes[i].port != port) continue;
			net_udp_inboxes[i].count = 0;
			net_udp_inboxes[i].head = 0;
			net_udp_inboxes[i].tail = 0;
			net_udp_inboxes[i].drops = 0;
			net_udp_inboxes[i].port = 0;
			net_udp_inboxes[i] = net_udp_inboxes[net_udp_inbox_count - 1];
			net_udp_inboxes[net_udp_inbox_count - 1] = {};
			--net_udp_inbox_count;
			return true;
		}
		return false;
	}

	bool EnqueueUdpInbox(NetUdpInbox& inbox, const NetUdpPacket& packet) {
		if (!EnsureUdpInboxStorage(inbox)) return false;
		if (packet.context.payload_length > NetUdpInboxPayloadSize) {
			++inbox.drops;
			return false;
		}
		if (inbox.count >= NetUdpInboxDepth) {
			++inbox.drops;
			return false;
		}
		const stduint slot = inbox.tail;
		auto* payload = GetUdpInboxPayloadSlot(inbox, slot);
		for0(i, packet.context.payload_length) payload[i] = packet.context.payload[i];
		inbox.entries[slot].context = packet.context;
		inbox.entries[slot].context.payload = payload;
		inbox.tail = (inbox.tail + 1) % NetUdpInboxDepth;
		++inbox.count;
		return true;
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

	stdsint SendUdpFrame(uni::Network::LinkDevice& dev, uint8* buffer,
		const uni::Network::MacAddress& target_mac, const uni::Network::IPv4Address& source_ip,
		const uni::Network::IPv4Address& target_ip, uint16 source_port, uint16 destination_port,
		const void* payload, stduint length, uint16 identification) {
		if (!buffer || (!payload && length)) return -1;
		const stduint udp_length = uni::Network::UDPHeaderLength + length;
		const stduint ipv4_length = uni::Network::IPv4MinHeaderLength + udp_length;
		const stduint frame_length = uni::Network::EthernetHeaderLength + ipv4_length;
		if (frame_length > NetFrameBufferSize || ipv4_length > dev.GetMtu()) return -1;

		const auto local_mac = dev.GetAddress();
		auto* ethernet = reinterpret_cast<uni::Network::EthernetHeader*>(buffer);
		uni::Network::EthernetWriteAddress(ethernet->destination, target_mac);
		uni::Network::EthernetWriteAddress(ethernet->source, local_mac);
		uni::Network::EthernetWrite16(ethernet->type, uint16(uni::Network::EthernetType::IPv4));

		auto* ipv4 = reinterpret_cast<uni::Network::IPv4Header*>(
			buffer + uni::Network::EthernetHeaderLength);
		uni::Network::BuildIPv4Header(*ipv4, source_ip, target_ip,
			uint8(uni::Network::IPv4Protocol::UDP), uint16(ipv4_length), identification);

		auto* udp = reinterpret_cast<uni::Network::UDPHeader*>(
			buffer + uni::Network::EthernetHeaderLength + uni::Network::IPv4MinHeaderLength);
		uni::Network::BuildUDPHeader(*udp, source_port, destination_port, uint16(udp_length));

		auto* udp_payload = buffer + uni::Network::EthernetHeaderLength +
			uni::Network::IPv4MinHeaderLength + uni::Network::UDPHeaderLength;
		const auto* payload_bytes = reinterpret_cast<const uint8*>(payload);
		for0(i, length) udp_payload[i] = payload_bytes[i];

		const uint16 checksum = uni::Network::UDPIPv4Checksum(source_ip, target_ip, udp, udp_length);
		uni::Network::EthernetWrite16(udp->checksum, checksum);

		uni::Network::LinkFrameView frame{
			buffer,
			frame_length,
		};
		return dev.Send(frame);
	}

	void FlushPendingUdpFor(uni::Network::LinkDevice& dev, const uni::Network::IPv4Address& target_ip,
		const uni::Network::MacAddress& target_mac) {
		for0(i, NetPendingUdpCapacity) {
			auto& packet = net_pending_udp[i];
			if (!packet.valid || !(packet.target_ip == target_ip)) continue;
			const stdsint sent = SendUdpFrame(dev, net_buffers.tx, target_mac, net_config.ipv4_address,
				packet.target_ip, packet.source_port, packet.destination_port,
				GetPendingUdpPayloadSlot(i), packet.payload_length, net_ipv4_identification++);
			if (sent > 0) packet.valid = false;
		}
	}

	bool HandleUdpInboxDatagram(uni::Network::LinkDevice& dev, const NetUdpPacket& packet) {
		(void)dev;
		auto* inbox = FindUdpInbox(packet.context.destination.port);
		if (!inbox) return false;
		return EnqueueUdpInbox(*inbox, packet);
	}

	void HandleArpFrame(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		uni::Network::ArpEthernetIPv4View arp{};
		if (!uni::Network::ParseArpEthernetIPv4(frame, arp)) {
			plogwarn("[Net] arp malformed");
			return;
		}
		LearnArpCache(arp.sender_protocol, arp.sender_hardware);
		FlushPendingUdpFor(dev, arp.sender_protocol, arp.sender_hardware);
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

	void HandleICMPv4Frame(uni::Network::LinkDevice& dev,
		const uni::Network::EthernetFrameView& frame, const uni::Network::IPv4PacketView& ipv4) {
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

	bool HandleUdpEchoDatagram(uni::Network::LinkDevice& dev, const NetUdpPacket& packet) {
		const auto& frame = *packet.ethernet;
		const auto& ipv4 = *packet.ipv4;
		const auto& udp = *packet.udp;
		const uint16 identification = uni::Network::EthernetRead16(ipv4.header->identification);
		const stdsint sent = SendUdpFrame(dev, net_buffers.tx, frame.source,
			net_config.ipv4_address, ipv4.source, NetUdpEchoPort, udp.source_port,
			packet.context.payload, packet.context.payload_length, identification);
		if (sent <= 0) {
			plogwarn("[Net] udp echo reply send failed");
			return false;
		}
		// if (sent > 0) LogIPv4Address("[Net] udp echo reply ", ipv4.source);
		return true;
	}

	void DispatchUDPFrame(uni::Network::LinkDevice& dev,
		const uni::Network::EthernetFrameView& frame, const uni::Network::IPv4PacketView& ipv4) {
		uni::Network::UDPDatagramView udp{};
		if (!uni::Network::ParseUDPDatagram(ipv4, udp)) {
			plogwarn("[Net] udp malformed");
			return;
		}
		if (!uni::Network::ValidateUDPIPv4Checksum(ipv4, udp)) {
			plogwarn("[Net] udp checksum invalid");
			return;
		}
		auto handler = FindUdpPortHandler(udp.destination_port);
		if (!handler) return;
		NetUdpPacket packet{
			&frame,
			&ipv4,
			&udp,
			{
				{ ipv4.source, udp.source_port },
				{ ipv4.destination, udp.destination_port },
				udp.payload,
				udp.payload_length,
			},
		};
		(void)handler(dev, packet);
	}

	void HandleIPv4FrameByProtocol(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		uni::Network::IPv4PacketView ipv4{};
		if (!uni::Network::ParseIPv4Packet(frame, ipv4)) {
			plogwarn("[Net] ipv4 malformed");
			return;
		}
		if (!(ipv4.destination == net_config.ipv4_address)) return;
		switch (uni::Network::IPv4Protocol(ipv4.protocol)) {
		case uni::Network::IPv4Protocol::ICMP:
			HandleICMPv4Frame(dev, frame, ipv4);
			return;
		case uni::Network::IPv4Protocol::UDP:
			DispatchUDPFrame(dev, frame, ipv4);
			return;
		default:
			return;
		}
	}

	void DispatchEthernetFrame(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		// LogEthernetFrame(dev, frame);
		switch (uni::Network::EthernetType(frame.type)) {
		case uni::Network::EthernetType::ARP:
			HandleArpFrame(dev, frame);
			return;
		case uni::Network::EthernetType::IPv4:
			HandleIPv4FrameByProtocol(dev, frame);
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

	void RegisterBuiltinUdpPorts() {
		if (!RegisterUdpPortHandler(NetUdpEchoPort, HandleUdpEchoDatagram)) {
			plogwarn("[Net] udp echo port registration failed");
		}
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

bool Devsman::OpenUdpPort(uint16 port) {
	if (!port) return false;
	if (auto handler = FindUdpPortHandler(port)) {
		return handler == HandleUdpInboxDatagram && FindUdpInbox(port) != nullptr;
	}
	auto* inbox = EnsureUdpInbox(port);
	if (!inbox || !EnsureUdpInboxStorage(*inbox)) return false;
	return RegisterUdpPortHandler(port, HandleUdpInboxDatagram);
}

bool Devsman::BindUdpPort(uint16 port) {
	if (!port) return false;
	if (FindUdpPortHandler(port) || FindUdpInbox(port)) return false;
	auto* inbox = EnsureUdpInbox(port);
	if (!inbox || !EnsureUdpInboxStorage(*inbox)) return false;
	return RegisterUdpPortHandler(port, HandleUdpInboxDatagram);
}

bool Devsman::AllocateUdpPort(uint16& port) {
	if (port && !IsUdpPortAvailable(port)) return false;
	if (port) return BindUdpPort(port);
	for (uint32 candidate = NetUdpEphemeralPortBegin; candidate <= NetUdpEphemeralPortEnd; candidate++) {
		const uint16 trial = uint16(candidate);
		if (!IsUdpPortAvailable(trial)) continue;
		if (!BindUdpPort(trial)) continue;
		port = trial;
		return true;
	}
	return false;
}

bool Devsman::CloseUdpPort(uint16 port) {
	if (!port) return false;
	auto handler = FindUdpPortHandler(port);
	if (handler != HandleUdpInboxDatagram) return false;
	if (!UnregisterUdpPortHandler(port, HandleUdpInboxDatagram)) return false;
	(void)ReleaseUdpInbox(port);
	return true;
}

stdsint Devsman::ReceiveUdp(uint16 port, uni::Network::UDPDatagramContext& context, void* payload, stduint capacity) {
	auto* inbox = FindUdpInbox(port);
	if (!inbox || !inbox->IsReady() || inbox->count == 0) return 0;
	auto& entry = inbox->entries[inbox->head];
	const stduint length = entry.context.payload_length;
	if (!payload || capacity < length) return -1;
	auto* output = reinterpret_cast<uint8*>(payload);
	for0(i, length) output[i] = entry.context.payload[i];
	context = entry.context;
	context.payload = output;
	context.payload_length = length;
	inbox->head = (inbox->head + 1) % NetUdpInboxDepth;
	--inbox->count;
	return stdsint(length);
}

stdsint Devsman::SendUdp(const uni::Network::MacAddress& target_mac, const uni::Network::IPv4Address& target_ip,
	uint16 source_port, uint16 destination_port, const void* payload, stduint length) {
	if (!source_port || !destination_port) return -1;
	if (target_mac.IsZero() || target_ip.IsZero()) return -1;
	if (!EnsureUdpTxBuffer()) return -1;
	auto* dev = FindDefaultLinkDevice();
	if (!dev) return -1;
	return SendUdpFrame(*dev, net_buffers.udp_tx, target_mac, net_config.ipv4_address,
		target_ip, source_port, destination_port, payload, length, net_ipv4_identification++);
}

stdsint Devsman::SendUdp(const uni::Network::IPv4Address& target_ip,
	uint16 source_port, uint16 destination_port, const void* payload, stduint length) {
	if (!source_port || !destination_port || target_ip.IsZero()) return -1;
	if (!IsSameIPv4Subnet(net_config.ipv4_address, target_ip, net_config.ipv4_netmask)) return -1;
	if (!EnsureUdpTxBuffer()) return -1;
	auto* dev = FindDefaultLinkDevice();
	if (!dev) return -1;
	uni::Network::MacAddress target_mac{};
	if (!LookupArpCache(target_ip, target_mac)) {
		const stdsint pending_index = EnqueuePendingUdp(target_ip, source_port, destination_port, payload, length);
		if (pending_index < 0) return -1;
		if (SendArpRequest(*dev, net_buffers.udp_tx, target_ip)) return 0;
		ClearPendingUdp(stduint(pending_index));
		return -1;
	}
	return SendUdpFrame(*dev, net_buffers.udp_tx, target_mac, net_config.ipv4_address,
		target_ip, source_port, destination_port, payload, length, net_ipv4_identification++);
}

bool Devsman::GetDefaultIPv4Route(void* route, stduint length) {
	if (!route || length < sizeof(syscall_net_route_ipv4_t)) return false;
	auto* output = reinterpret_cast<syscall_net_route_ipv4_t*>(route);
	*output = {};
	auto* dev = FindDefaultLinkDevice();
	if (!dev) return false;
	for0(i, uni::Network::IPv4AddressLength) {
		output->address[i] = net_config.ipv4_address.octet[i];
		output->netmask[i] = net_config.ipv4_netmask.octet[i];
		output->gateway[i] = net_config.ipv4_gateway.octet[i];
	}
	const auto mac = dev->GetAddress();
	for0(i, numsof(output->hardware)) output->hardware[i] = mac.octet[i];
	output->flags = syscall_net_route_flag_gateway;
	if (dev->GetState() == uni::Network::LinkState::Up) output->flags |= syscall_net_route_flag_up;
	output->mtu = uint16(dev->GetMtu());
	output->link_state = uint16(dev->GetState());
	const char* name = dev->GetName();
	if (name) {
		for0(i, numsof(output->name) - 1) {
			if (!name[i]) break;
			output->name[i] = name[i];
		}
	}
	return true;
}

void serv_dev_net_loop() {
	RegisterBuiltinUdpPorts();
	ploginfo("[Net] Service thread start pid=%u", Taskman::CurrentPID());
	while (true) {
		NetServicePoll();
		NetServiceIdleWait();
	}
}

#endif
