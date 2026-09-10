// ASCII g++ TAB4 LF
// ModuTitle: [Service] Device Management - Network
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include <cpp/System/Network/Layer/Link.hpp>
#include <cpp/System/Network/Layer/Link/Ethernet.hpp>
#include <cpp/System/Network/Layer/Network/ARP.hpp>
#include <cpp/System/Network/Layer/Network/IPv4.hpp>
#include <cpp/System/Network/Layer/Network/IPv4/ICMP.hpp>
#include <cpp/System/Network/Layer/Transport/TCP.hpp>
#include <cpp/System/Network/Layer/Transport/UDP.hpp>

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	constexpr stduint NetLinkDeviceCapacity = 8;
	constexpr stduint NetFrameBufferSize = 2048;
	constexpr stduint NetUdpPortCapacity = 8;
	constexpr stduint NetUdpInboxPortCapacity = 4;
	constexpr stduint NetUdpInboxDepth = 4;
	constexpr stduint NetUdpInboxWaiterCapacity = 4;
	constexpr stduint NetUdpInboxPayloadSize = 512;
	constexpr stduint NetArpCacheCapacity = 8;
	constexpr stduint NetArpCacheTtlTicks = 120 * CONFIG_SysTickFreq;
	constexpr stduint NetPendingUdpCapacity = 4;
	constexpr stduint NetPendingUdpPayloadSize = 1472;
	constexpr stduint NetPendingUdpTimeoutTicks = 5 * CONFIG_SysTickFreq;
	constexpr stduint NetPendingUdpArpRetryTicks = CONFIG_SysTickFreq;
	constexpr stduint NetPendingUdpArpRetryLimit = 3;
	constexpr stduint NetTcpListenCapacity = 4;
	constexpr stduint NetTcpConnectionCapacity = 8;
	constexpr stduint NetTcpAcceptQueueDepth = 4;
	constexpr stduint NetTcpAcceptWaiterCapacity = 4;
	constexpr stduint NetTcpConnectTimeoutTicks = 3 * CONFIG_SysTickFreq;
	constexpr stduint NetTcpConnectRetryTicks = CONFIG_SysTickFreq;
	constexpr stduint NetTcpConnectRetryLimit = 3;
	constexpr stduint NetTcpControlRetryTicks = CONFIG_SysTickFreq;
	constexpr stduint NetTcpControlRetryLimit = 3;
	constexpr stduint NetTcpRxStreamSize = 2048;
	constexpr stduint NetTcpRxWaiterCapacity = 2;
	constexpr stduint NetTcpTxPendingCapacity = 4;
	constexpr stduint NetTcpTxPayloadSize = 1460;
	constexpr stduint NetTcpTxRetryTicks = CONFIG_SysTickFreq;
	constexpr stduint NetTcpTxRetryLimit = 3;
	constexpr stduint NetTcpTimeWaitTicks = 2 * CONFIG_SysTickFreq;
	constexpr stduint NetTcpOptionMssLength = 4;
	constexpr uint8 NetTcpOptionEnd = 0;
	constexpr uint8 NetTcpOptionNoop = 1;
	constexpr uint8 NetTcpOptionMss = 2;
	constexpr uint16 NetTcpWindowSize = 4096;
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
		uint8* tcp_tx;
		FMT_NetworkMsg_DRV_FRAME* driver_frame;

		bool IsReady() const {
			return rx && tx && driver_frame;
		}

		bool IsUdpTxReady() const {
			return udp_tx != nullptr;
		}

		bool IsTcpTxReady() const {
			return tcp_tx != nullptr;
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
		stduint updated_tick;
		bool valid;
	};

	struct NetPendingUdpPacket {
		uni::Network::IPv4Address target_ip;
		uni::Network::IPv4Address next_hop;
		uint16 source_port;
		uint16 destination_port;
		stduint payload_length;
		stduint queued_tick;
		stduint last_arp_request_tick;
		stduint arp_request_count;
		bool valid;
	};

	using NetUdpPortHandler = bool (*)(uni::Network::LinkDevice& dev, const NetUdpPacket& packet);

	struct NetUdpPortBinding {
		uint16 port;
		NetUdpPortHandler handler;
	};

	struct NetTcpListener {
		uint16 port;
		stduint backlog;
		uni::Network::TCPObject* tcp;
		uni::Network::TCPConnectionContext pending[NetTcpAcceptQueueDepth];
		ThreadBlock* accept_waiters[NetTcpAcceptWaiterCapacity];
		stduint accept_waiter_count;
	};

	struct NetTcpTxPending {
		uint32 sequence;
		stduint length;
		stduint last_tick;
		stduint retry_count;
		bool valid;
	};

	enum class NetTcpClosePhase : uint8 {
		None,
		FinWait1,
		FinWait2,
		Closing,
		TimeWait,
		Reset,
	};

	struct NetTcpConnection {
		uni::Network::TCPObject* tcp;
		uint8* rx_stream;
		stduint rx_head;
		stduint rx_tail;
		stduint rx_count;
		uint8* tx_payloads;
		NetTcpTxPending tx_pending[NetTcpTxPendingCapacity];
		stduint tx_head;
		stduint tx_tail;
		stduint tx_count;
		ThreadBlock* rx_waiters[NetTcpRxWaiterCapacity];
		stduint rx_waiter_count;
		stduint connect_started_tick;
		stduint last_syn_tick;
		stduint syn_retry_count;
		stduint last_synack_tick;
		stduint synack_retry_count;
		stduint last_fin_tick;
		stduint fin_retry_count;
		stduint time_wait_tick;
		NetTcpClosePhase close_phase;
		uint16 local_mss;
		uint16 peer_mss;
		bool active_open;
		bool local_fin_acknowledged;
		bool reset_received;
		bool valid;
	};

	struct NetUdpInboxEntry {
		uni::Network::UDPDatagramContext context;
	};

	struct NetUdpInbox {
		stduint id;
		uint16 port;
		bool reuse_address;
		NetUdpInboxEntry* entries;
		uint8* payloads;
		stduint head;
		stduint tail;
		stduint count;
		stduint drops;
		ThreadBlock* read_waiters[NetUdpInboxWaiterCapacity];
		stduint read_waiter_count;

		bool IsReady() const {
			return entries && payloads;
		}
	};

	NetUdpPortBinding net_udp_ports[NetUdpPortCapacity]{};
	stduint net_udp_port_count = 0;
	NetUdpInbox net_udp_inboxes[NetUdpInboxPortCapacity]{};
	stduint net_udp_inbox_count = 0;
	stduint net_udp_inbox_next_id = 1;
	NetArpCacheEntry net_arp_cache[NetArpCacheCapacity]{};
	stduint net_arp_cache_next = 0;
	NetPendingUdpPacket net_pending_udp[NetPendingUdpCapacity]{};
	uint8* net_pending_udp_payloads = nullptr;
	NetTcpListener net_tcp_listeners[NetTcpListenCapacity]{};
	NetTcpConnection net_tcp_connections[NetTcpConnectionCapacity]{};
	uint32 net_tcp_next_sequence = 0x10000000u;

	uint16 NetTcpLocalMss();
	stduint NetTcpSendMss(const NetTcpConnection& connection);

	struct NetRemoteLinkState {
		stduint owner_tid = 0;
		stduint dev_handle = 0;
		uint32 caps = 0;
		uint32 mtu = 0;
		uni::Network::MacAddress mac{};
		uni::Network::LinkState state = uni::Network::LinkState::Down;
		char name[NetworkDriverNameCapacity]{};
		bool registered = false;
	};

	NetRemoteLinkState net_remote_link{};

	void DispatchLinkFrame(uni::Network::LinkDevice& dev, const void* data, stduint length);

	class NetRemoteLinkDevice : public uni::Network::LinkDevice {
	public:
		virtual const char* getName() const override {
			return net_remote_link.name[0] ? net_remote_link.name : "e1000";
		}

		virtual uni::Network::LinkMedium getMedium() const override {
			return uni::Network::LinkMedium::Ethernet;
		}

		virtual uni::Network::LinkState getState() const override {
			return net_remote_link.state;
		}

		virtual uni::Network::MacAddress getAddress() const override {
			return net_remote_link.mac;
		}

		virtual stduint getMtu() const override {
			return net_remote_link.mtu ? net_remote_link.mtu : 1500;
		}

		virtual stdsint Send(const uni::Network::LinkFrameView& frame) override {
			if (!net_remote_link.owner_tid || !frame.data || !frame.length || frame.length > NetworkDriverFrameCapacity) {
				return -1;
			}
			FMT_NetworkMsg_DRV_FRAME req{};
			req.length = uint32(frame.length);
			req.capacity = NetworkDriverFrameCapacity;
			MemCopyN(req.data, frame.data, frame.length);
			if (syssend(net_remote_link.owner_tid, &req, sizeof(req), _IMM(NetworkMsg::DRV_SEND))) return -1;
			while (true) {
				FMT_NetworkMsg_DRV_FRAME reply{};
				stduint type = 0;
				if (sysrecv(net_remote_link.owner_tid, &reply, sizeof(reply), &type)) return -1;
				switch (NetworkMsg(type)) {
				case NetworkMsg::DRV_SEND:
					return reply.status;
				case NetworkMsg::DRV_RX:
					if (reply.length <= NetworkDriverFrameCapacity) {
						DispatchLinkFrame(*this, reply.data, reply.length);
					}
					break;
				default:
					break;
				}
			}
		}

		virtual stdsint Receive(uni::Network::LinkMutableFrameView& frame) override {
			(void)frame;
			return -1;
		}

		virtual stdsint Control(stduint command, void* args) override {
			(void)command;
			(void)args;
			return -1;
		}

		virtual void getStatistics(uni::Network::LinkStatistics& statistics) const override {
			MemSet(&statistics, 0, sizeof(statistics));
		}
	};

	NetRemoteLinkDevice net_remote_link_device;

	bool EnsureNetServiceBuffers() {
		if (!net_buffers.rx) net_buffers.rx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		if (!net_buffers.tx) net_buffers.tx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		if (!net_buffers.driver_frame) {
			net_buffers.driver_frame = (FMT_NetworkMsg_DRV_FRAME*)mempool.allocate(sizeof(FMT_NetworkMsg_DRV_FRAME), 4);
		}
		return net_buffers.IsReady();
	}

	bool EnsureUdpTxBuffer() {
		if (!net_buffers.udp_tx) net_buffers.udp_tx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		return net_buffers.IsUdpTxReady();
	}

	bool EnsureTcpTxBuffer() {
		if (!net_buffers.tcp_tx) net_buffers.tcp_tx = (uint8*)mempool.allocate(NetFrameBufferSize, 12);
		return net_buffers.IsTcpTxReady();
	}

	uni::Network::LinkDevice* FindDefaultLinkDevice() {
		if (net_remote_link.owner_tid &&
			net_remote_link.registered &&
			net_remote_link.state == uni::Network::LinkState::Up) {
			return &net_remote_link_device;
		}
		for0(i, net_link_device_count) {
			auto* dev = net_link_devices[i];
			if (dev && dev->getState() == uni::Network::LinkState::Up) return dev;
		}
		return nullptr;
	}

	stduint FindLinkDeviceIndex(const uni::Network::LinkDevice* target) {
		for0(i, net_link_device_count) if (net_link_devices[i] == target) return i;
		return stduint(-1);
	}

	bool FillIPv4Interface(stduint index, syscall_net_interface_ipv4_t& output) {
		auto* dev = Devsman::GetLinkDevice(index);
		if (!dev) return false;
		output = {};
		for0(i, uni::Network::IPv4AddressLength) {
			output.address[i] = net_config.ipv4_address.octet[i];
			output.netmask[i] = net_config.ipv4_netmask.octet[i];
		}
		const auto mac = dev->getAddress();
		for0(i, numsof(output.hardware)) output.hardware[i] = mac.octet[i];
		if (dev->getState() == uni::Network::LinkState::Up) output.flags |= syscall_net_route_flag_up;
		output.mtu = uint16(dev->getMtu());
		output.link_index = uint16(index);
		output.link_state = uint16(dev->getState());
		const char* name = dev->getName();
		if (name) {
			for0(i, numsof(output.name) - 1) {
				if (!name[i]) break;
				output.name[i] = name[i];
			}
		}
		return true;
	}

	bool IsSameIPv4Subnet(const uni::Network::IPv4Address& lhs, const uni::Network::IPv4Address& rhs,
		const uni::Network::IPv4Address& mask) {
		for0(i, uni::Network::IPv4AddressLength) {
			if ((lhs.octet[i] & mask.octet[i]) != (rhs.octet[i] & mask.octet[i])) return false;
		}
		return true;
	}

	struct NetIPv4Route {
		uni::Network::LinkDevice* dev;
		uni::Network::IPv4Address source;
		uni::Network::IPv4Address next_hop;
		stduint link_index;
		bool gateway;
	};

	bool ResolveIPv4Route(const uni::Network::IPv4Address& target_ip, NetIPv4Route& route) {
		if (target_ip.isZero()) return false;
		auto* dev = FindDefaultLinkDevice();
		if (!dev) return false;
		const stduint index = FindLinkDeviceIndex(dev);
		if (index == stduint(-1)) return false;
		route = {};
		route.dev = dev;
		route.source = net_config.ipv4_address;
		route.link_index = index;
		if (IsSameIPv4Subnet(net_config.ipv4_address, target_ip, net_config.ipv4_netmask)) {
			route.next_hop = target_ip;
			return true;
		}
		if (net_config.ipv4_gateway.isZero()) return false;
		route.next_hop = net_config.ipv4_gateway;
		route.gateway = true;
		return true;
	}

	bool IsArpCacheEntryExpired(const NetArpCacheEntry& entry) {
		return entry.valid && tick - entry.updated_tick >= NetArpCacheTtlTicks;
	}

	void ExpireArpCache() {
		for0(i, NetArpCacheCapacity) {
			auto& entry = net_arp_cache[i];
			if (IsArpCacheEntryExpired(entry)) entry.valid = false;
		}
	}

	bool LookupArpCache(const uni::Network::IPv4Address& protocol, uni::Network::MacAddress& hardware) {
		ExpireArpCache();
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
		if (protocol.isZero() || hardware.isZero() || hardware.isBroadcast()) return;
		for0(i, NetArpCacheCapacity) {
			auto& entry = net_arp_cache[i];
			if (entry.valid && entry.protocol == protocol) {
				entry.hardware = hardware;
				entry.updated_tick = tick;
				return;
			}
		}
		ExpireArpCache();
		for0(i, NetArpCacheCapacity) {
			auto& entry = net_arp_cache[i];
			if (!entry.valid) {
				entry.protocol = protocol;
				entry.hardware = hardware;
				entry.updated_tick = tick;
				entry.valid = true;
				return;
			}
		}
		auto& entry = net_arp_cache[net_arp_cache_next];
		entry.protocol = protocol;
		entry.hardware = hardware;
		entry.updated_tick = tick;
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

	bool IsPendingUdpExpired(const NetPendingUdpPacket& packet) {
		return packet.valid && tick - packet.queued_tick >= NetPendingUdpTimeoutTicks;
	}

	void ExpirePendingUdp() {
		for0(i, NetPendingUdpCapacity) {
			if (IsPendingUdpExpired(net_pending_udp[i])) ClearPendingUdp(i);
		}
	}

	bool IsSamePendingUdp(const NetPendingUdpPacket& packet,
		const uni::Network::IPv4Address& target_ip, const uni::Network::IPv4Address& next_hop,
		uint16 source_port, uint16 destination_port) {
		return packet.valid &&
			packet.target_ip == target_ip &&
			packet.next_hop == next_hop &&
			packet.source_port == source_port &&
			packet.destination_port == destination_port;
	}

	bool ShouldSendPendingArpRequest(const uni::Network::IPv4Address& next_hop) {
		ExpirePendingUdp();
		for0(i, NetPendingUdpCapacity) {
			const auto& packet = net_pending_udp[i];
			if (!packet.valid || !(packet.next_hop == next_hop)) continue;
			if (!packet.arp_request_count) return true;
			if (packet.arp_request_count < NetPendingUdpArpRetryLimit &&
				tick - packet.last_arp_request_tick >= NetPendingUdpArpRetryTicks) {
				return true;
			}
		}
		return false;
	}

	void MarkPendingArpRequest(const uni::Network::IPv4Address& next_hop) {
		for0(i, NetPendingUdpCapacity) {
			auto& packet = net_pending_udp[i];
			if (!packet.valid || !(packet.next_hop == next_hop)) continue;
			packet.last_arp_request_tick = tick;
			if (packet.arp_request_count < NetPendingUdpArpRetryLimit) packet.arp_request_count++;
		}
	}

	stdsint EnqueuePendingUdp(const uni::Network::IPv4Address& target_ip, const uni::Network::IPv4Address& next_hop,
		uint16 source_port, uint16 destination_port, const void* payload, stduint length) {
		if ((!payload && length) || length > NetPendingUdpPayloadSize) return -1;
		if (!EnsurePendingUdpStorage()) return -1;
		ExpirePendingUdp();
		for0(i, NetPendingUdpCapacity) {
			auto& packet = net_pending_udp[i];
			if (!IsSamePendingUdp(packet, target_ip, next_hop, source_port, destination_port)) continue;
			packet.payload_length = length;
			auto* slot = GetPendingUdpPayloadSlot(i);
			const auto* payload_bytes = reinterpret_cast<const uint8*>(payload);
			for0(j, length) slot[j] = payload_bytes[j];
			return stdsint(i);
		}
		for0(i, NetPendingUdpCapacity) {
			auto& packet = net_pending_udp[i];
			if (packet.valid) continue;
			packet.target_ip = target_ip;
			packet.next_hop = next_hop;
			packet.source_port = source_port;
			packet.destination_port = destination_port;
			packet.payload_length = length;
			packet.queued_tick = tick;
			packet.last_arp_request_tick = 0;
			packet.arp_request_count = 0;
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
			dev.getAddress(), net_config.ipv4_address, target_ip);
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

	NetTcpListener* FindTcpListener(uint16 port) {
		if (!port) return nullptr;
		for0(i, NetTcpListenCapacity) {
			if (net_tcp_listeners[i].port == port) return &net_tcp_listeners[i];
		}
		return nullptr;
	}

	bool EnsureTcpListenerObject(NetTcpListener& listener, stduint backlog) {
		if (!listener.tcp) {
			auto* storage = mempool.allocate(sizeof(uni::Network::TCPObject), 4);
			if (!storage) return false;
			listener.tcp = new (storage) uni::Network::TCPObject();
		}
		listener.tcp->Reset();
		listener.tcp->setAcceptQueue(listener.pending, NetTcpAcceptQueueDepth);
		return listener.tcp->Listen(backlog) == 0;
	}

	stduint TcpListenerBacklogLimit(const NetTcpListener& listener) {
		if (!listener.backlog) return 1;
		return listener.backlog < NetTcpAcceptQueueDepth ? listener.backlog : NetTcpAcceptQueueDepth;
	}

	bool IsTcpAcceptBacklogFull(const NetTcpListener& listener) {
		if (!listener.tcp) return false;
		return listener.tcp->getPendingAcceptCount() >= TcpListenerBacklogLimit(listener);
	}

	NetTcpConnection* FindTcpConnection(const uni::Network::IPv4Address& local_ip, uint16 local_port,
		const uni::Network::IPv4Address& remote_ip, uint16 remote_port) {
		for0(i, NetTcpConnectionCapacity) {
			auto& connection = net_tcp_connections[i];
			if (!connection.valid || !connection.tcp) continue;
			const auto& context = connection.tcp->getControl().context;
			if (context.local.port != local_port || context.remote.port != remote_port) continue;
			if (!(context.local.address == local_ip) || !(context.remote.address == remote_ip)) continue;
			return &connection;
		}
		return nullptr;
	}

	bool EnsureTcpObject(NetTcpConnection& connection) {
		if (!connection.tcp) {
			auto* storage = mempool.allocate(sizeof(uni::Network::TCPObject), 4);
			if (!storage) return false;
			connection.tcp = new (storage) uni::Network::TCPObject();
		}
		connection.tcp->Reset();
		connection.tcp->setWindow(NetTcpWindowSize);
		return true;
	}

	NetTcpConnection* AllocateTcpConnection(const uni::Network::IPv4Address& local_ip, uint16 local_port,
		const uni::Network::IPv4Address& remote_ip, uint16 remote_port) {
		for0(i, NetTcpConnectionCapacity) {
			auto& connection = net_tcp_connections[i];
			if (connection.valid) continue;
			auto* tcp = connection.tcp;
			uint8* rx_stream = connection.rx_stream;
			uint8* tx_payloads = connection.tx_payloads;
			connection = {};
			connection.tcp = tcp;
			connection.rx_stream = rx_stream;
			connection.tx_payloads = tx_payloads;
			if (!EnsureTcpObject(connection)) return nullptr;
			connection.tcp->Bind({ uni::Network::NetworkAddressIPv4(local_ip), local_port });
			connection.tcp->Connect({ uni::Network::NetworkAddressIPv4(remote_ip), remote_port });
			connection.rx_head = 0;
			connection.rx_tail = 0;
			connection.rx_count = 0;
			connection.rx_waiter_count = 0;
			connection.connect_started_tick = 0;
			connection.last_syn_tick = 0;
			connection.syn_retry_count = 0;
			connection.last_synack_tick = 0;
			connection.synack_retry_count = 0;
			connection.last_fin_tick = 0;
			connection.fin_retry_count = 0;
			connection.local_mss = NetTcpLocalMss();
			connection.peer_mss = 0;
			connection.active_open = false;
			connection.local_fin_acknowledged = false;
			connection.valid = true;
			return &connection;
		}
		return nullptr;
	}

	bool IsTcpLocalPortAvailable(uint16 port) {
		if (!port || FindTcpListener(port)) return false;
		for0(i, NetTcpConnectionCapacity) {
			if (!net_tcp_connections[i].valid || !net_tcp_connections[i].tcp) continue;
			if (net_tcp_connections[i].tcp->getControl().context.local.port == port) return false;
		}
		return true;
	}

	bool AllocateTcpPort(uint16& port) {
		if (port) return IsTcpLocalPortAvailable(port);
		for (uint32 candidate = NetUdpEphemeralPortBegin; candidate <= NetUdpEphemeralPortEnd; candidate++) {
			const uint16 trial = uint16(candidate);
			if (!IsTcpLocalPortAvailable(trial)) continue;
			port = trial;
			return true;
		}
		return false;
	}

	void WakeTcpAcceptWaiters(NetTcpListener& listener) {
		for0(i, listener.accept_waiter_count) {
			if (listener.accept_waiters[i]) {
				listener.accept_waiters[i]->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
				listener.accept_waiters[i] = nullptr;
			}
		}
		listener.accept_waiter_count = 0;
	}

	bool IsTcpConnectReady(const NetTcpConnection& connection) {
		return connection.tcp &&
			connection.tcp->getControl().state == uni::Network::TCPConnectionState::Established;
	}

	bool AddTcpAcceptWaiter(NetTcpListener& listener, ThreadBlock* th) {
		if (!th) return false;
		for0(i, listener.accept_waiter_count) if (listener.accept_waiters[i] == th) return true;
		if (listener.accept_waiter_count >= NetTcpAcceptWaiterCapacity) return false;
		listener.accept_waiters[listener.accept_waiter_count++] = th;
		return true;
	}

	bool EnqueueTcpAccept(NetTcpListener& listener, const NetTcpConnection& connection) {
		if (!connection.tcp || !listener.tcp) return false;
		if (IsTcpAcceptBacklogFull(listener)) return false;
		if (!listener.tcp->EnqueueAccept(connection.tcp->getControl().context)) return false;
		WakeTcpAcceptWaiters(listener);
		return true;
	}

	bool DequeueTcpAccept(NetTcpListener& listener, uni::Network::TCPConnectionContext& context) {
		return listener.tcp && listener.tcp->Accept(context);
	}

	void WakeTcpRxReaders(NetTcpConnection& connection);

	void ReleaseTcpConnectionsByLocalPort(uint16 port) {
		for0(i, NetTcpConnectionCapacity) {
			if (net_tcp_connections[i].valid && net_tcp_connections[i].tcp &&
				net_tcp_connections[i].tcp->getControl().context.local.port == port) {
				WakeTcpRxReaders(net_tcp_connections[i]);
				auto* tcp = net_tcp_connections[i].tcp;
				uint8* rx_stream = net_tcp_connections[i].rx_stream;
				uint8* tx_payloads = net_tcp_connections[i].tx_payloads;
				net_tcp_connections[i] = {};
				net_tcp_connections[i].tcp = tcp;
				net_tcp_connections[i].tcp->Reset();
				net_tcp_connections[i].rx_stream = rx_stream;
				net_tcp_connections[i].tx_payloads = tx_payloads;
			}
		}
	}

	bool ReleaseTcpConnection(const uni::Network::TCPConnectionContext& context) {
		auto* connection = FindTcpConnection(context.local.address, context.local.port,
			context.remote.address, context.remote.port);
		if (!connection) return false;
		WakeTcpRxReaders(*connection);
		auto* tcp = connection->tcp;
		uint8* rx_stream = connection->rx_stream;
		uint8* tx_payloads = connection->tx_payloads;
		*connection = {};
		connection->tcp = tcp;
		connection->tcp->Reset();
		connection->rx_stream = rx_stream;
		connection->tx_payloads = tx_payloads;
		return true;
	}

	bool EnsureTcpRxStorage(NetTcpConnection& connection) {
		if (!connection.rx_stream) {
			connection.rx_stream = (uint8*)mempool.allocate(NetTcpRxStreamSize, 12);
		}
		return connection.rx_stream != nullptr;
	}

	void WakeTcpRxReaders(NetTcpConnection& connection) {
		for0(i, connection.rx_waiter_count) {
			if (connection.rx_waiters[i]) {
				connection.rx_waiters[i]->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
				connection.rx_waiters[i] = nullptr;
			}
		}
		connection.rx_waiter_count = 0;
	}

	bool AddTcpRxReader(NetTcpConnection& connection, ThreadBlock* th) {
		if (!th) return false;
		for0(i, connection.rx_waiter_count) if (connection.rx_waiters[i] == th) return true;
		if (connection.rx_waiter_count >= NetTcpRxWaiterCapacity) return false;
		connection.rx_waiters[connection.rx_waiter_count++] = th;
		return true;
	}

	bool IsTcpReceiveReady(const NetTcpConnection& connection) {
		if (connection.rx_count) return true;
		if (!connection.tcp) return false;
		if (connection.reset_received) return true;
		return connection.tcp->getControl().state == uni::Network::TCPConnectionState::CloseWait;
	}

	bool EnqueueTcpRx(NetTcpConnection& connection, const uni::Network::TCPSegmentView& tcp) {
		if (!tcp.payload_length) return true;
		if (tcp.payload_length > NetTcpRxStreamSize - connection.rx_count) return false;
		if (!EnsureTcpRxStorage(connection)) return false;
		for0(i, tcp.payload_length) {
			connection.rx_stream[connection.rx_tail] = tcp.payload[i];
			connection.rx_tail = (connection.rx_tail + 1) % NetTcpRxStreamSize;
		}
		connection.rx_count += tcp.payload_length;
		WakeTcpRxReaders(connection);
		return true;
	}

	stdsint DequeueTcpRx(NetTcpConnection& connection, void* payload, stduint capacity) {
		if (!connection.rx_count) return 0;
		if (!capacity) return 0;
		if (!payload) return -1;
		const stduint copied = capacity < connection.rx_count ? capacity : connection.rx_count;
		uint8* output = reinterpret_cast<uint8*>(payload);
		for0(i, copied) {
			output[i] = connection.rx_stream[connection.rx_head];
			connection.rx_head = (connection.rx_head + 1) % NetTcpRxStreamSize;
		}
		connection.rx_count -= copied;
		return stdsint(copied);
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

	NetUdpInbox* FindUdpInbox(uint16 port, stduint inbox_id) {
		for0(i, net_udp_inbox_count) {
			if (net_udp_inboxes[i].port == port && net_udp_inboxes[i].id == inbox_id) {
				return &net_udp_inboxes[i];
			}
		}
		return nullptr;
	}

	bool IsUdpPortAvailable(uint16 port) {
		return port && !FindUdpPortHandler(port) && !FindUdpInbox(port);
	}

	bool UdpPortAllowsReuse(uint16 port) {
		if (!port) return false;
		for0(i, net_udp_inbox_count) {
			if (net_udp_inboxes[i].port == port && !net_udp_inboxes[i].reuse_address) return false;
		}
		return true;
	}

	NetUdpInbox* AllocateUdpInbox(uint16 port, bool reuse_address) {
		if (!port) return nullptr;
		if (net_udp_inbox_count >= NetUdpInboxPortCapacity) return nullptr;
		auto* inbox = &net_udp_inboxes[net_udp_inbox_count++];
		inbox->id = net_udp_inbox_next_id++;
		if (!net_udp_inbox_next_id) net_udp_inbox_next_id = 1;
		inbox->port = port;
		inbox->reuse_address = reuse_address;
		inbox->entries = nullptr;
		inbox->payloads = nullptr;
		inbox->head = 0;
		inbox->tail = 0;
		inbox->count = 0;
		inbox->drops = 0;
		inbox->read_waiter_count = 0;
		return inbox;
	}

	void WakeUdpInboxReaders(NetUdpInbox& inbox) {
		for0(i, inbox.read_waiter_count) {
			if (inbox.read_waiters[i]) {
				inbox.read_waiters[i]->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
				inbox.read_waiters[i] = nullptr;
			}
		}
		inbox.read_waiter_count = 0;
	}

	bool AddUdpInboxReader(NetUdpInbox& inbox, ThreadBlock* th) {
		if (!th) return false;
		for0(i, inbox.read_waiter_count) if (inbox.read_waiters[i] == th) return true;
		if (inbox.read_waiter_count >= NetUdpInboxWaiterCapacity) return false;
		inbox.read_waiters[inbox.read_waiter_count++] = th;
		return true;
	}

	bool ReleaseUdpInbox(uint16 port, stduint inbox_id) {
		for0(i, net_udp_inbox_count) {
			if (net_udp_inboxes[i].port != port || net_udp_inboxes[i].id != inbox_id) continue;
			WakeUdpInboxReaders(net_udp_inboxes[i]);
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
		WakeUdpInboxReaders(inbox);
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
			dev.getName() ? dev.getName() : "(unnamed)",
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

	stdsint SendIPv4PacketFrame(uni::Network::LinkDevice& dev, const uni::Network::MacAddress& target_mac,
		const uni::Network::NetworkPacketContext& packet) {
		if (!net_buffers.tx || !packet.payload || target_mac.isZero()) return -1;
		uni::Network::IPv4Address source{};
		uni::Network::IPv4Address destination{};
		if (!uni::Network::NetworkReadIPv4Address(packet.source, source)) return -1;
		if (!uni::Network::NetworkReadIPv4Address(packet.destination, destination)) return -1;
		const stduint ipv4_length = uni::Network::IPv4MinHeaderLength + packet.payload_length;
		const stduint frame_length = uni::Network::EthernetHeaderLength + ipv4_length;
		if (frame_length > NetFrameBufferSize || ipv4_length > dev.getMtu()) return -1;

		const auto local_mac = dev.getAddress();
		auto* ethernet = reinterpret_cast<uni::Network::EthernetHeader*>(net_buffers.tx);
		uni::Network::EthernetWriteAddress(ethernet->destination, target_mac);
		uni::Network::EthernetWriteAddress(ethernet->source, local_mac);
		uni::Network::EthernetWrite16(ethernet->type, uint16(uni::Network::EthernetType::IPv4));

		auto* ipv4 = reinterpret_cast<uni::Network::IPv4Header*>(
			net_buffers.tx + uni::Network::EthernetHeaderLength);
		uni::Network::BuildIPv4Header(*ipv4, source, destination,
			uint8(packet.protocol), uint16(ipv4_length), packet.identification, packet.ttl ? packet.ttl : 64);

		auto* payload = net_buffers.tx + uni::Network::EthernetHeaderLength + uni::Network::IPv4MinHeaderLength;
		const auto* bytes = reinterpret_cast<const uint8*>(packet.payload);
		for0(i, packet.payload_length) payload[i] = bytes[i];

		uni::Network::LinkFrameView frame{
			net_buffers.tx,
			frame_length,
		};
		return dev.Send(frame);
	}

	class NetIPv4Interface : public uni::Network::NetworkInterface {
	public:
		uni::Network::NetworkAddress getAddress() const override {
			return uni::Network::NetworkAddressIPv4(net_config.ipv4_address);
		}

		stduint getPayloadMtu() const override {
			auto* dev = FindDefaultLinkDevice();
			if (!dev || dev->getMtu() <= uni::Network::IPv4MinHeaderLength) return 0;
			return dev->getMtu() - uni::Network::IPv4MinHeaderLength;
		}

		stdsint SendPacket(const uni::Network::NetworkPacketContext& packet) override {
			uni::Network::IPv4Address target{};
			if (!uni::Network::NetworkReadIPv4Address(packet.destination, target)) return -1;
			NetIPv4Route route{};
			if (!ResolveIPv4Route(target, route)) return -1;
			uni::Network::MacAddress target_mac{};
			if (!LookupArpCache(route.next_hop, target_mac)) return 0;
			return SendIPv4PacketFrame(*route.dev, target_mac, packet);
		}
	};

	NetIPv4Interface net_ipv4_interface;

	uint16 NetTcpLocalMss() {
		const stduint payload_mtu = net_ipv4_interface.getPayloadMtu();
		if (payload_mtu <= uni::Network::TCPMinHeaderLength) return 0;
		stduint mss = payload_mtu - uni::Network::TCPMinHeaderLength;
		if (mss > 0xFFFFu) mss = 0xFFFFu;
		return uint16(mss);
	}

	stduint NetTcpSendMss(const NetTcpConnection& connection) {
		stduint mss = connection.local_mss ? connection.local_mss : NetTcpLocalMss();
		if (connection.peer_mss && (!mss || connection.peer_mss < mss)) mss = connection.peer_mss;
		if (!mss || mss > NetTcpTxPayloadSize) mss = NetTcpTxPayloadSize;
		return mss;
	}

	uint16 ParseTcpMssOption(const uni::Network::TCPSegmentView& tcp) {
		if (!tcp.header || tcp.header_length <= uni::Network::TCPMinHeaderLength) return 0;
		const auto* option = reinterpret_cast<const uint8*>(tcp.header) + uni::Network::TCPMinHeaderLength;
		stduint remaining = tcp.header_length - uni::Network::TCPMinHeaderLength;
		while (remaining) {
			const uint8 kind = option[0];
			if (kind == NetTcpOptionEnd) return 0;
			if (kind == NetTcpOptionNoop) {
				option++;
				remaining--;
				continue;
			}
			if (remaining < 2) return 0;
			const uint8 length = option[1];
			if (length < 2 || length > remaining) return 0;
			if (kind == NetTcpOptionMss && length == NetTcpOptionMssLength) {
				return uint16((uint16(option[2]) << 8) | uint16(option[3]));
			}
			option += length;
			remaining -= length;
		}
		return 0;
	}

	void WriteTcpMssOption(uni::Network::TCPHeader& tcp, uint16 mss) {
		auto* option = reinterpret_cast<uint8*>(&tcp) + uni::Network::TCPMinHeaderLength;
		option[0] = NetTcpOptionMss;
		option[1] = NetTcpOptionMssLength;
		option[2] = uint8(mss >> 8);
		option[3] = uint8(mss);
	}

	stduint BuildTcpControlHeader(NetTcpConnection& connection,
		uint8 flags, uint32 sequence, uint32 acknowledgment) {
		if (!connection.tcp) return 0;
		auto& control = connection.tcp->getControl();
		const auto& local = control.context.local;
		const auto& remote = control.context.remote;
		auto* tcp = reinterpret_cast<uni::Network::TCPHeader*>(net_buffers.tcp_tx);
		const uint16 local_mss = connection.local_mss ? connection.local_mss : NetTcpLocalMss();
		const bool use_mss = (flags & uni::Network::TCPFlagSYN) && local_mss;
		const uint8 header_length = uint8(uni::Network::TCPMinHeaderLength +
			(use_mss ? NetTcpOptionMssLength : 0));
		uni::Network::BuildTCPHeader(*tcp, local.port, remote.port,
			sequence, acknowledgment, flags, NetTcpWindowSize, 0, 0, header_length);
		if (use_mss) WriteTcpMssOption(*tcp, local_mss);
		const uint16 checksum = uni::Network::TCPIPv4Checksum(local.address, remote.address,
			tcp, header_length);
		uni::Network::EthernetWrite16(tcp->checksum, checksum);
		return header_length;
	}

	bool SendTcpSyn(NetTcpConnection& connection) {
		if (!connection.tcp || !EnsureTcpTxBuffer()) return false;
		connection.last_syn_tick = tick;
		connection.syn_retry_count++;
		auto& control = connection.tcp->getControl();
		const auto& local = control.context.local;
		const auto& remote = control.context.remote;
		if (!local.port || !remote.port || local.address.isZero() || remote.address.isZero()) return false;
		if (!control.local_next_sequence) return false;

		NetIPv4Route route{};
		if (!ResolveIPv4Route(remote.address, route)) return false;
		uni::Network::MacAddress target_mac{};
		if (!LookupArpCache(route.next_hop, target_mac)) {
			return SendArpRequest(*route.dev, net_buffers.tcp_tx, route.next_hop);
		}

		const stduint tcp_length = BuildTcpControlHeader(connection,
			uni::Network::TCPFlagSYN, control.local_next_sequence - 1, 0);
		if (!tcp_length) return false;

		uni::Network::NetworkPacketContext packet{
			uni::Network::NetworkAddressIPv4(local.address),
			uni::Network::NetworkAddressIPv4(remote.address),
			uint8(uni::Network::IPv4Protocol::TCP),
			net_buffers.tcp_tx,
			tcp_length,
			net_ipv4_identification++,
			64,
		};
		const stdsint sent = SendIPv4PacketFrame(*route.dev, target_mac, packet);
		return sent > 0;
	}

	bool SendTcpControl(NetTcpConnection& connection, uint8 flags, uint32 sequence, uint32 acknowledgment) {
		if (!connection.tcp || !EnsureTcpTxBuffer()) return false;
		auto& control = connection.tcp->getControl();
		const auto& local = control.context.local;
		const auto& remote = control.context.remote;
		if (!local.port || !remote.port || local.address.isZero() || remote.address.isZero()) return false;

		NetIPv4Route route{};
		if (!ResolveIPv4Route(remote.address, route)) return false;
		uni::Network::MacAddress target_mac{};
		if (!LookupArpCache(route.next_hop, target_mac)) {
			return SendArpRequest(*route.dev, net_buffers.tcp_tx, route.next_hop);
		}

		const stduint tcp_length = BuildTcpControlHeader(connection,
			flags, sequence, acknowledgment);
		if (!tcp_length) return false;

		uni::Network::NetworkPacketContext packet{
			uni::Network::NetworkAddressIPv4(local.address),
			uni::Network::NetworkAddressIPv4(remote.address),
			uint8(uni::Network::IPv4Protocol::TCP),
			net_buffers.tcp_tx,
			tcp_length,
			net_ipv4_identification++,
			64,
		};
		const stdsint sent = SendIPv4PacketFrame(*route.dev, target_mac, packet);
		return sent > 0;
	}

	bool SendTcpSynAck(NetTcpConnection& connection) {
		if (!connection.tcp) return false;
		const auto& control = connection.tcp->getControl();
		if (!control.local_next_sequence) return false;
		if (!SendTcpControl(connection, uni::Network::TCPFlagSYN | uni::Network::TCPFlagACK,
			control.local_next_sequence - 1, control.remote_next_sequence)) return false;
		connection.last_synack_tick = tick;
		connection.synack_retry_count++;
		return true;
	}

	bool SendTcpFin(NetTcpConnection& connection) {
		if (!connection.tcp) return false;
		const auto& control = connection.tcp->getControl();
		if (!control.local_next_sequence) return false;
		if (!SendTcpControl(connection, uni::Network::TCPFlagFIN | uni::Network::TCPFlagACK,
			control.local_next_sequence - 1, control.remote_next_sequence)) return false;
		connection.last_fin_tick = tick;
		connection.fin_retry_count++;
		return true;
	}

	uint8* GetTcpTxPayloadSlot(NetTcpConnection& connection, stduint index) {
		return connection.tx_payloads + index * NetTcpTxPayloadSize;
	}

	bool EnsureTcpTxStorage(NetTcpConnection& connection) {
		if (!connection.tx_payloads) {
			connection.tx_payloads = (uint8*)mempool.allocate(NetTcpTxPayloadSize * NetTcpTxPendingCapacity, 12);
		}
		return connection.tx_payloads != nullptr;
	}

	bool HasTcpTxPendingSpace(NetTcpConnection& connection) {
		return connection.tx_count < NetTcpTxPendingCapacity && EnsureTcpTxStorage(connection);
	}

	void MarkTcpReset(NetTcpConnection& connection) {
		connection.reset_received = true;
		connection.close_phase = NetTcpClosePhase::Reset;
		for0(i, NetTcpTxPendingCapacity) connection.tx_pending[i] = {};
		connection.tx_head = 0;
		connection.tx_tail = 0;
		connection.tx_count = 0;
		WakeTcpRxReaders(connection);
	}

	bool IsTcpTxRetryExhausted(const NetTcpConnection& connection) {
		if (!connection.tx_count) return false;
		const auto& pending = connection.tx_pending[connection.tx_head];
		return pending.valid && pending.length && pending.retry_count >= NetTcpTxRetryLimit;
	}

	void MarkTcpFinAcknowledged(NetTcpConnection& connection) {
		connection.local_fin_acknowledged = true;
		if (connection.close_phase == NetTcpClosePhase::FinWait1) {
			connection.close_phase = NetTcpClosePhase::FinWait2;
		}
		else if (connection.close_phase == NetTcpClosePhase::Closing) {
			connection.close_phase = NetTcpClosePhase::TimeWait;
			connection.time_wait_tick = tick;
		}
	}

	void MarkTcpActiveFinReceived(NetTcpConnection& connection) {
		if (connection.close_phase == NetTcpClosePhase::FinWait1 &&
			!connection.local_fin_acknowledged) {
			connection.close_phase = NetTcpClosePhase::Closing;
			return;
		}
		if (connection.close_phase == NetTcpClosePhase::FinWait2 ||
			connection.local_fin_acknowledged) {
			connection.close_phase = NetTcpClosePhase::TimeWait;
			connection.time_wait_tick = tick;
		}
	}

	bool EnqueueTcpTxPending(NetTcpConnection& connection,
		uint32 sequence, const void* payload, stduint length) {
		if (!length) return true;
		if (length > NetTcpTxPayloadSize) return false;
		if (!HasTcpTxPendingSpace(connection)) return false;
		auto& pending = connection.tx_pending[connection.tx_tail];
		uint8* slot = GetTcpTxPayloadSlot(connection, connection.tx_tail);
		const auto* input = reinterpret_cast<const uint8*>(payload);
		for0(i, length) slot[i] = input[i];
		pending.sequence = sequence;
		pending.length = length;
		pending.last_tick = tick;
		pending.retry_count = 0;
		pending.valid = true;
		connection.tx_tail = (connection.tx_tail + 1) % NetTcpTxPendingCapacity;
		connection.tx_count++;
		return true;
	}

	void CancelTcpNewestTxPending(NetTcpConnection& connection) {
		if (!connection.tx_count) return;
		connection.tx_tail = (connection.tx_tail + NetTcpTxPendingCapacity - 1) % NetTcpTxPendingCapacity;
		connection.tx_pending[connection.tx_tail] = {};
		connection.tx_count--;
		if (!connection.tx_count) connection.tx_head = connection.tx_tail;
	}

	void AcknowledgeTcpTxPending(NetTcpConnection& connection, uint32 acknowledgment) {
		while (connection.tx_count) {
			auto& pending = connection.tx_pending[connection.tx_head];
			if (!pending.valid || !pending.length) {
				pending = {};
				connection.tx_head = (connection.tx_head + 1) % NetTcpTxPendingCapacity;
				connection.tx_count--;
				continue;
			}
			const uint32 end_sequence = pending.sequence + uint32(pending.length);
			if (acknowledgment <= pending.sequence) return;
			if (acknowledgment >= end_sequence) {
				pending = {};
				connection.tx_head = (connection.tx_head + 1) % NetTcpTxPendingCapacity;
				connection.tx_count--;
				continue;
			}
			const stduint consumed = stduint(acknowledgment - pending.sequence);
			uint8* slot = GetTcpTxPayloadSlot(connection, connection.tx_head);
			const stduint remaining = pending.length - consumed;
			for0(i, remaining) slot[i] = slot[consumed + i];
			pending.sequence = acknowledgment;
			pending.length = remaining;
			return;
		}
	}

	bool SendTcpPendingData(NetTcpConnection& connection, stduint index) {
		if (index >= NetTcpTxPendingCapacity) return false;
		auto& pending = connection.tx_pending[index];
		if (!connection.tcp || !pending.valid || !pending.length) return false;
		if (!EnsureTcpTxBuffer()) return false;
		auto& control = connection.tcp->getControl();
		const auto& local = control.context.local;
		const auto& remote = control.context.remote;
		if (!local.port || !remote.port || local.address.isZero() || remote.address.isZero()) return false;

		NetIPv4Route route{};
		if (!ResolveIPv4Route(remote.address, route)) return false;
		uni::Network::MacAddress target_mac{};
		if (!LookupArpCache(route.next_hop, target_mac)) {
			return SendArpRequest(*route.dev, net_buffers.tcp_tx, route.next_hop);
		}

		auto* tcp = reinterpret_cast<uni::Network::TCPHeader*>(net_buffers.tcp_tx);
		uni::Network::BuildTCPHeader(*tcp, local.port, remote.port,
			pending.sequence, control.remote_next_sequence,
			uint8(uni::Network::TCPFlagACK | uni::Network::TCPFlagPSH), NetTcpWindowSize);
		uint8* target = net_buffers.tcp_tx + uni::Network::TCPMinHeaderLength;
		uint8* source = GetTcpTxPayloadSlot(connection, index);
		for0(i, pending.length) target[i] = source[i];
		const stduint tcp_length = uni::Network::TCPMinHeaderLength + pending.length;
		const uint16 checksum = uni::Network::TCPIPv4Checksum(local.address, remote.address, tcp, tcp_length);
		uni::Network::EthernetWrite16(tcp->checksum, checksum);

		uni::Network::NetworkPacketContext packet{
			uni::Network::NetworkAddressIPv4(local.address),
			uni::Network::NetworkAddressIPv4(remote.address),
			uint8(uni::Network::IPv4Protocol::TCP),
			net_buffers.tcp_tx,
			tcp_length,
			net_ipv4_identification++,
			64,
		};
		const stdsint sent = SendIPv4PacketFrame(*route.dev, target_mac, packet);
		if (sent <= 0) return false;
		pending.last_tick = tick;
		pending.retry_count++;
		return true;
	}

	bool IsTcpConnectExpired(const NetTcpConnection& connection) {
		return connection.connect_started_tick &&
			tick - connection.connect_started_tick >= NetTcpConnectTimeoutTicks;
	}

	bool ShouldRetryTcpConnect(const NetTcpConnection& connection) {
		if (connection.syn_retry_count >= NetTcpConnectRetryLimit) return false;
		return !connection.last_syn_tick ||
			tick - connection.last_syn_tick >= NetTcpConnectRetryTicks;
	}

	void ProcessTcpConnectTimers() {
		for0(i, NetTcpConnectionCapacity) {
			auto& connection = net_tcp_connections[i];
			if (!connection.valid || !connection.active_open || !connection.tcp) continue;
			if (connection.reset_received) {
				const auto context = connection.tcp->getControl().context;
				ReleaseTcpConnection(context);
				continue;
			}
			if (IsTcpConnectReady(connection)) continue;
			if (IsTcpConnectExpired(connection)) {
				const auto context = connection.tcp->getControl().context;
				ReleaseTcpConnection(context);
				continue;
			}
			if (ShouldRetryTcpConnect(connection)) {
				(void)SendTcpSyn(connection);
			}
		}
	}

	void ProcessTcpControlTimers() {
		for0(i, NetTcpConnectionCapacity) {
			auto& connection = net_tcp_connections[i];
			if (!connection.valid || !connection.tcp) continue;
			const auto& control = connection.tcp->getControl();
			if (connection.close_phase == NetTcpClosePhase::TimeWait &&
				tick - connection.time_wait_tick >= NetTcpTimeWaitTicks) {
				const auto context = control.context;
				ReleaseTcpConnection(context);
				continue;
			}
			if (connection.tx_count) {
				auto& pending = connection.tx_pending[connection.tx_head];
				if (pending.valid && pending.length &&
					pending.retry_count < NetTcpTxRetryLimit &&
					tick - pending.last_tick >= NetTcpTxRetryTicks) {
					(void)SendTcpPendingData(connection, connection.tx_head);
					continue;
				}
			}
			if (!connection.active_open &&
				control.state == uni::Network::TCPConnectionState::SynReceived &&
				connection.synack_retry_count < NetTcpControlRetryLimit &&
				(!connection.last_synack_tick || tick - connection.last_synack_tick >= NetTcpControlRetryTicks)) {
				(void)SendTcpSynAck(connection);
				continue;
			}
			if (control.state == uni::Network::TCPConnectionState::LastAck &&
				connection.fin_retry_count < NetTcpControlRetryLimit &&
				(!connection.last_fin_tick || tick - connection.last_fin_tick >= NetTcpControlRetryTicks)) {
				(void)SendTcpFin(connection);
			}
		}
	}

	bool HasTcpTimersPending() {
		for0(i, NetTcpConnectionCapacity) {
			auto& connection = net_tcp_connections[i];
			if (!connection.valid || !connection.tcp) continue;
			const auto& control = connection.tcp->getControl();
			if (connection.active_open && !IsTcpConnectReady(connection)) return true;
			if (!connection.active_open &&
				control.state == uni::Network::TCPConnectionState::SynReceived &&
				connection.synack_retry_count < NetTcpControlRetryLimit) return true;
			if (control.state == uni::Network::TCPConnectionState::LastAck &&
				connection.fin_retry_count < NetTcpControlRetryLimit) return true;
			if (connection.close_phase == NetTcpClosePhase::TimeWait) return true;
			if (connection.tx_count &&
				connection.tx_pending[connection.tx_head].retry_count < NetTcpTxRetryLimit) return true;
		}
		return false;
	}

	void FlushPendingTcpConnectsFor(const uni::Network::IPv4Address& target_ip) {
		for0(i, NetTcpConnectionCapacity) {
			auto& connection = net_tcp_connections[i];
			if (!connection.valid || !connection.active_open || !connection.tcp) continue;
			if (IsTcpConnectReady(connection)) continue;
			NetIPv4Route route{};
			if (!ResolveIPv4Route(connection.tcp->getControl().context.remote.address, route)) continue;
			if (!(route.next_hop == target_ip)) continue;
			(void)SendTcpSyn(connection);
		}
	}

	bool SendTcpAckForSegment(uni::Network::LinkDevice& dev,
		const uni::Network::EthernetFrameView& frame, const uni::Network::IPv4PacketView& ipv4,
		const uni::Network::TCPSegmentView& tcp, uint32 sequence, uint32 acknowledgment,
		const char* warning) {
		const auto local_mac = dev.getAddress();
		const stduint ack_len = uni::Network::BuildTCPIPv4Ack(net_buffers.tx, NetFrameBufferSize,
			local_mac, frame.source, net_config.ipv4_address, ipv4, tcp,
			sequence, acknowledgment, NetTcpWindowSize, net_ipv4_identification++);
		if (!ack_len) {
			plogwarn("%s build failed", warning);
			return false;
		}
		uni::Network::LinkFrameView ack{
			net_buffers.tx,
			ack_len,
		};
		const stdsint sent = dev.Send(ack);
		if (sent <= 0) {
			plogwarn("%s send failed", warning);
			return false;
		}
		return true;
	}

	bool SendTcpResetForSegment(uni::Network::LinkDevice& dev,
		const uni::Network::EthernetFrameView& frame, const uni::Network::IPv4PacketView& ipv4,
		const uni::Network::TCPSegmentView& tcp) {
		const auto local_mac = dev.getAddress();
		const stduint reset_len = uni::Network::BuildTCPIPv4Reset(net_buffers.tx, NetFrameBufferSize,
			local_mac, frame.source, net_config.ipv4_address, ipv4, tcp, net_ipv4_identification++);
		if (!reset_len) {
			plogwarn("[Net] tcp reset build failed");
			return false;
		}
		uni::Network::LinkFrameView reset{
			net_buffers.tx,
			reset_len,
		};
		const stdsint sent = dev.Send(reset);
		if (sent <= 0) {
			plogwarn("[Net] tcp reset send failed");
			return false;
		}
		return true;
	}

	bool IsTcpAcknowledgmentInRange(const NetTcpConnection& connection,
		const uni::Network::TCPSegmentView& tcp) {
		if (!(tcp.flags & uni::Network::TCPFlagACK)) return true;
		if (!connection.tcp) return false;
		return tcp.acknowledgment <= connection.tcp->getControl().local_next_sequence;
	}

	bool PrepareTcpTransmit(NetTcpConnection& connection) {
		if (!connection.tcp) return false;
		if (!EnsureTcpTxBuffer()) return false;
		connection.tcp->setNetwork(&net_ipv4_interface);
		connection.tcp->setPacketBuffer(net_buffers.tcp_tx, NetFrameBufferSize);
		connection.tcp->setIdentification(net_ipv4_identification++);
		return true;
	}

	stdsint SendUdpDatagram(uni::Network::NetworkInterface& network, uint8* buffer,
		const uni::Network::IPv4Address& source_ip, const uni::Network::IPv4Address& target_ip,
		uint16 source_port, uint16 destination_port,
		const void* payload, stduint length, uint16 identification) {
		uni::Network::UDPObject udp(&network, buffer, NetFrameBufferSize);
		udp.setIdentification(identification);
		udp.Bind({ uni::Network::NetworkAddressIPv4(source_ip), source_port });
		udp.Connect({ uni::Network::NetworkAddressIPv4(target_ip), destination_port });
		uni::Network::TransportPayloadContext context{
			{
				{ uni::Network::NetworkAddressIPv4(source_ip), source_port },
				{ uni::Network::NetworkAddressIPv4(target_ip), destination_port },
			},
			payload,
			length,
		};
		const stdsint sent = udp.Send(context);
		return sent > 0 ? stdsint(length) : sent;
	}

	void FlushPendingUdpFor(const uni::Network::IPv4Address& target_ip, const uni::Network::MacAddress& target_mac) {
		LearnArpCache(target_ip, target_mac);
		ExpirePendingUdp();
		for0(i, NetPendingUdpCapacity) {
			auto& packet = net_pending_udp[i];
			if (!packet.valid || !(packet.next_hop == target_ip)) continue;
			const stdsint sent = SendUdpDatagram(net_ipv4_interface, net_buffers.udp_tx,
				net_config.ipv4_address, packet.target_ip, packet.source_port, packet.destination_port,
				GetPendingUdpPayloadSlot(i), packet.payload_length, net_ipv4_identification++);
			if (sent > 0) packet.valid = false;
		}
	}

	bool HandleUdpInboxDatagram(uni::Network::LinkDevice& dev, const NetUdpPacket& packet) {
		(void)dev;
		bool accepted = false;
		for0(i, net_udp_inbox_count) {
			auto& inbox = net_udp_inboxes[i];
			if (inbox.port != packet.context.destination.port) continue;
			accepted = EnqueueUdpInbox(inbox, packet) || accepted;
		}
		return accepted;
	}

	void HandleArpFrame(uni::Network::LinkDevice& dev, const uni::Network::EthernetFrameView& frame) {
		uni::Network::ArpEthernetIPv4View arp{};
		if (!uni::Network::ParseArpEthernetIPv4(frame, arp)) {
			plogwarn("[Net] arp malformed");
			return;
		}
		LearnArpCache(arp.sender_protocol, arp.sender_hardware);
		FlushPendingUdpFor(arp.sender_protocol, arp.sender_hardware);
		FlushPendingTcpConnectsFor(arp.sender_protocol);
		if (arp.operation != uint16(uni::Network::ArpOperation::Request)) return;
		if (!(arp.target_protocol == net_config.ipv4_address)) return;
		const auto local_mac = dev.getAddress();
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
		const auto local_mac = dev.getAddress();
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

	void SendICMPv4PortUnreachable(uni::Network::LinkDevice& dev,
		const uni::Network::EthernetFrameView& frame, const uni::Network::IPv4PacketView& ipv4) {
		const stduint quoted_payload = minof(stduint(8), ipv4.payload_length);
		const stduint quoted_length = ipv4.header_length + quoted_payload;
		const stduint icmp_length = uni::Network::ICMPv4HeaderLength + quoted_length;
		const stduint ipv4_length = uni::Network::IPv4MinHeaderLength + icmp_length;
		const stduint frame_length = uni::Network::EthernetHeaderLength + ipv4_length;
		if (!net_buffers.tx || frame_length > NetFrameBufferSize) return;

		const auto local_mac = dev.getAddress();
		auto* ethernet = reinterpret_cast<uni::Network::EthernetHeader*>(net_buffers.tx);
		uni::Network::EthernetWriteAddress(ethernet->destination, frame.source);
		uni::Network::EthernetWriteAddress(ethernet->source, local_mac);
		uni::Network::EthernetWrite16(ethernet->type, uint16(uni::Network::EthernetType::IPv4));

		auto* response_ipv4 = reinterpret_cast<uni::Network::IPv4Header*>(
			net_buffers.tx + uni::Network::EthernetHeaderLength);
		uni::Network::BuildIPv4Header(*response_ipv4, net_config.ipv4_address, ipv4.source,
			uint8(uni::Network::IPv4Protocol::ICMP), uint16(ipv4_length), net_ipv4_identification++);

		auto* icmp = net_buffers.tx + uni::Network::EthernetHeaderLength + uni::Network::IPv4MinHeaderLength;
		auto* icmp_header = reinterpret_cast<uni::Network::ICMPv4Header*>(icmp);
		icmp_header->type = 3;
		icmp_header->code = 3;
		uni::Network::EthernetWrite16(icmp_header->checksum, 0);
		uni::Network::EthernetWrite16(icmp_header->identifier, 0);
		uni::Network::EthernetWrite16(icmp_header->sequence, 0);
		auto* quoted = icmp + uni::Network::ICMPv4HeaderLength;
		const auto* original = reinterpret_cast<const uint8*>(ipv4.header);
		for0(i, quoted_length) quoted[i] = original[i];
		uni::Network::EthernetWrite16(icmp_header->checksum,
			uni::Network::NetworkChecksum(icmp, icmp_length));

		uni::Network::LinkFrameView reply{
			net_buffers.tx,
			frame_length,
		};
		const stdsint sent = dev.Send(reply);
		if (sent <= 0) plogwarn("[Net] icmp port unreachable send failed");
	}

	bool HandleUdpEchoDatagram(uni::Network::LinkDevice& dev, const NetUdpPacket& packet) {
		(void)dev;
		const auto& frame = *packet.ethernet;
		const auto& ipv4 = *packet.ipv4;
		const auto& udp = *packet.udp;
		const uint16 identification = uni::Network::EthernetRead16(ipv4.header->identification);
		LearnArpCache(ipv4.source, frame.source);
		const stdsint sent = SendUdpDatagram(net_ipv4_interface, net_buffers.udp_tx,
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
		if (!handler) {
			SendICMPv4PortUnreachable(dev, frame, ipv4);
			return;
		}
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

	void HandleTCPv4Frame(uni::Network::LinkDevice& dev,
		const uni::Network::EthernetFrameView& frame, const uni::Network::IPv4PacketView& ipv4) {
		uni::Network::TCPSegmentView tcp{};
		if (!uni::Network::ParseTCPSegment(ipv4, tcp)) {
			plogwarn("[Net] tcp malformed");
			return;
		}
		if (!uni::Network::ValidateTCPIPv4Checksum(ipv4, tcp)) {
			plogwarn("[Net] tcp checksum invalid");
			return;
		}
		auto* listener_for_port = FindTcpListener(tcp.destination_port);
		auto* connection = FindTcpConnection(ipv4.destination, tcp.destination_port,
			ipv4.source, tcp.source_port);
		LearnArpCache(ipv4.source, frame.source);
		if (tcp.flags & uni::Network::TCPFlagRST) {
			if (connection) {
				MarkTcpReset(*connection);
			}
			return;
		}
		if (connection && !IsTcpAcknowledgmentInRange(*connection, tcp)) return;
		if (connection && (tcp.flags & uni::Network::TCPFlagACK)) {
			AcknowledgeTcpTxPending(*connection, tcp.acknowledgment);
			if (connection->active_open &&
				connection->tcp->getControl().state == uni::Network::TCPConnectionState::LastAck &&
				tcp.acknowledgment == connection->tcp->getControl().local_next_sequence) {
				MarkTcpFinAcknowledged(*connection);
			}
		}
		if (connection && connection->active_open &&
			(tcp.flags & (uni::Network::TCPFlagSYN | uni::Network::TCPFlagACK)) ==
			(uni::Network::TCPFlagSYN | uni::Network::TCPFlagACK)) {
			auto& control = connection->tcp->getControl();
			if (tcp.acknowledgment != control.local_next_sequence) {
				plogwarn("[Net] tcp syn-ack invalid ack");
				return;
			}
			const uint16 peer_mss = ParseTcpMssOption(tcp);
			if (peer_mss) connection->peer_mss = peer_mss;
			control.remote_next_sequence = tcp.sequence + 1;
			control.state = uni::Network::TCPConnectionState::Established;
			SendTcpAckForSegment(dev, frame, ipv4, tcp,
				control.local_next_sequence, control.remote_next_sequence,
				"[Net] tcp connect ack");
			return;
		}
		if (listener_for_port) {
			if ((tcp.flags & uni::Network::TCPFlagSYN) && !(tcp.flags & uni::Network::TCPFlagACK)) {
				bool new_connection = false;
				if (!connection) {
					if (IsTcpAcceptBacklogFull(*listener_for_port)) {
						SendTcpResetForSegment(dev, frame, ipv4, tcp);
						return;
					}
					connection = AllocateTcpConnection(ipv4.destination, tcp.destination_port,
						ipv4.source, tcp.source_port);
					new_connection = connection != nullptr;
				}
				if (!connection) {
					plogwarn("[Net] tcp connection table full");
					SendTcpResetForSegment(dev, frame, ipv4, tcp);
					return;
				}
				auto& control = connection->tcp->getControl();
				if (!new_connection && control.state == uni::Network::TCPConnectionState::Established) {
					const auto local_mac = dev.getAddress();
					const stduint ack_len = uni::Network::BuildTCPIPv4Ack(net_buffers.tx, NetFrameBufferSize,
						local_mac, frame.source, net_config.ipv4_address, ipv4, tcp,
						control.local_next_sequence, control.remote_next_sequence,
						NetTcpWindowSize, net_ipv4_identification++);
					if (!ack_len) {
						plogwarn("[Net] tcp duplicate syn ack build failed");
						return;
					}
					uni::Network::LinkFrameView ack{
						net_buffers.tx,
						ack_len,
					};
					const stdsint sent = dev.Send(ack);
					if (sent <= 0) plogwarn("[Net] tcp duplicate syn ack send failed");
					return;
				}
				if (control.state != uni::Network::TCPConnectionState::SynReceived) return;
				uint32 initial_sequence = control.local_next_sequence ?
					control.local_next_sequence - 1 : net_tcp_next_sequence++;
				if (new_connection) {
					connection->tcp->BeginPassiveConnection(
						ipv4.destination, tcp.destination_port, ipv4.source, tcp.source_port,
						tcp, initial_sequence);
				}
				const uint16 peer_mss = ParseTcpMssOption(tcp);
				if (peer_mss) connection->peer_mss = peer_mss;
				if (!SendTcpSynAck(*connection)) {
					plogwarn("[Net] tcp syn-ack send failed");
				}
				return;
			}
			if (connection && tcp.payload_length) {
				auto& control = connection->tcp->getControl();
				const bool in_order = connection->tcp->isExpectedSegment(tcp);
				bool release_active_close = false;
				if (in_order) {
					if (!EnqueueTcpRx(*connection, tcp)) {
						plogwarn("[Net] tcp rx queue full");
						SendTcpAckForSegment(dev, frame, ipv4, tcp,
							control.local_next_sequence, control.remote_next_sequence,
							"[Net] tcp rx full ack");
						return;
					}
					connection->tcp->ConsumeExpectedSegment(tcp);
					release_active_close = (tcp.flags & uni::Network::TCPFlagFIN) &&
						connection->active_open &&
						connection->tcp->getControl().state == uni::Network::TCPConnectionState::LastAck &&
						(connection->local_fin_acknowledged ||
							((tcp.flags & uni::Network::TCPFlagACK) &&
								tcp.acknowledgment == connection->tcp->getControl().local_next_sequence));
					if (tcp.flags & uni::Network::TCPFlagFIN) WakeTcpRxReaders(*connection);
				}
				const auto local_mac = dev.getAddress();
				const stduint ack_len = uni::Network::BuildTCPIPv4Ack(net_buffers.tx, NetFrameBufferSize,
					local_mac, frame.source, net_config.ipv4_address, ipv4, tcp,
					control.local_next_sequence, control.remote_next_sequence,
					NetTcpWindowSize, net_ipv4_identification++);
				if (!ack_len) {
					plogwarn("[Net] tcp data ack build failed");
					return;
				}
				uni::Network::LinkFrameView ack{
					net_buffers.tx,
					ack_len,
				};
				const stdsint sent = dev.Send(ack);
				if (sent <= 0) plogwarn("[Net] tcp data ack send failed");
				if (release_active_close) {
					MarkTcpActiveFinReceived(*connection);
				}
				return;
			}
			if (tcp.flags & uni::Network::TCPFlagFIN) {
				const auto local_mac = dev.getAddress();
				const uint32 remote_next_sequence = tcp.sequence + uint32(tcp.payload_length) + 1;
				uint32 local_next_sequence = tcp.acknowledgment;
				bool release_active_close = false;
				if (connection) {
					const bool in_order = connection->tcp->ConsumeExpectedSegment(tcp);
					local_next_sequence = connection->tcp->getControl().local_next_sequence;
					release_active_close = connection->active_open &&
						connection->tcp->getControl().state == uni::Network::TCPConnectionState::LastAck &&
						(connection->local_fin_acknowledged ||
							((tcp.flags & uni::Network::TCPFlagACK) &&
								tcp.acknowledgment == connection->tcp->getControl().local_next_sequence));
					if (in_order) WakeTcpRxReaders(*connection);
				}
				const stduint ack_len = uni::Network::BuildTCPIPv4Ack(net_buffers.tx, NetFrameBufferSize,
					local_mac, frame.source, net_config.ipv4_address, ipv4, tcp,
					local_next_sequence, connection ? connection->tcp->getControl().remote_next_sequence : remote_next_sequence,
					NetTcpWindowSize, net_ipv4_identification++);
				if (!ack_len) {
					plogwarn("[Net] tcp ack build failed");
					return;
				}
				uni::Network::LinkFrameView ack{
					net_buffers.tx,
					ack_len,
				};
				const stdsint sent = dev.Send(ack);
				if (sent <= 0) plogwarn("[Net] tcp ack send failed");
				if (connection && release_active_close) {
					MarkTcpActiveFinReceived(*connection);
				}
				else if (connection && !connection->active_open && connection->tcp->AcceptCloseAck(tcp)) {
					const auto context = connection->tcp->getControl().context;
					ReleaseTcpConnection(context);
				}
				return;
			}
			if (connection && (tcp.flags & uni::Network::TCPFlagACK)) {
				if (connection->tcp->AcceptHandshakeAck(tcp)) {
					auto* listener = FindTcpListener(connection->tcp->getControl().context.local.port);
					if (listener && !EnqueueTcpAccept(*listener, *connection)) {
						plogwarn("[Net] tcp accept queue full");
						SendTcpResetForSegment(dev, frame, ipv4, tcp);
						const auto context = connection->tcp->getControl().context;
						ReleaseTcpConnection(context);
					}
					return;
				}
				if (connection->active_open &&
					connection->tcp->getControl().state == uni::Network::TCPConnectionState::LastAck &&
					tcp.acknowledgment == connection->tcp->getControl().local_next_sequence) {
					MarkTcpFinAcknowledged(*connection);
					return;
				}
				if (!connection->active_open && connection->tcp->AcceptCloseAck(tcp)) {
					const auto context = connection->tcp->getControl().context;
					ReleaseTcpConnection(context);
				}
				return;
			}
		}
		if (connection && tcp.payload_length) {
			auto& control = connection->tcp->getControl();
			const bool in_order = connection->tcp->isExpectedSegment(tcp);
			bool release_active_close = false;
			if (in_order) {
				if (!EnqueueTcpRx(*connection, tcp)) {
					plogwarn("[Net] tcp rx queue full");
					SendTcpAckForSegment(dev, frame, ipv4, tcp,
						control.local_next_sequence, control.remote_next_sequence,
						"[Net] tcp rx full ack");
					return;
				}
				connection->tcp->ConsumeExpectedSegment(tcp);
				release_active_close = (tcp.flags & uni::Network::TCPFlagFIN) &&
					connection->active_open &&
					connection->tcp->getControl().state == uni::Network::TCPConnectionState::LastAck &&
					(connection->local_fin_acknowledged ||
						((tcp.flags & uni::Network::TCPFlagACK) &&
							tcp.acknowledgment == connection->tcp->getControl().local_next_sequence));
				if (tcp.flags & uni::Network::TCPFlagFIN) WakeTcpRxReaders(*connection);
			}
			SendTcpAckForSegment(dev, frame, ipv4, tcp,
				control.local_next_sequence, control.remote_next_sequence,
				"[Net] tcp data ack");
			if (release_active_close) {
				MarkTcpActiveFinReceived(*connection);
			}
			return;
		}
		if (connection && (tcp.flags & uni::Network::TCPFlagFIN)) {
			const bool in_order = connection->tcp->ConsumeExpectedSegment(tcp);
			if (in_order) WakeTcpRxReaders(*connection);
			auto& control = connection->tcp->getControl();
			SendTcpAckForSegment(dev, frame, ipv4, tcp,
				control.local_next_sequence, control.remote_next_sequence,
				"[Net] tcp ack");
			if (connection->active_open &&
				control.state == uni::Network::TCPConnectionState::LastAck &&
				(connection->local_fin_acknowledged ||
					((tcp.flags & uni::Network::TCPFlagACK) &&
						tcp.acknowledgment == control.local_next_sequence))) {
				MarkTcpActiveFinReceived(*connection);
			}
			else if (!connection->active_open && connection->tcp->AcceptCloseAck(tcp)) {
				const auto context = connection->tcp->getControl().context;
				ReleaseTcpConnection(context);
			}
			return;
		}
		if (connection && (tcp.flags & uni::Network::TCPFlagACK)) {
			if (connection->active_open &&
				connection->tcp->getControl().state == uni::Network::TCPConnectionState::LastAck &&
				tcp.acknowledgment == connection->tcp->getControl().local_next_sequence) {
				MarkTcpFinAcknowledged(*connection);
				return;
			}
			if (!connection->active_open && connection->tcp->AcceptCloseAck(tcp)) {
				const auto context = connection->tcp->getControl().context;
				ReleaseTcpConnection(context);
			}
			return;
		}
		SendTcpResetForSegment(dev, frame, ipv4, tcp);
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
		case uni::Network::IPv4Protocol::TCP:
			HandleTCPv4Frame(dev, frame, ipv4);
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

	void DispatchLinkFrame(uni::Network::LinkDevice& dev, const void* data, stduint length) {
		if (!data || !length) return;
		uni::Network::LinkFrameView raw_frame{
			data,
			length,
		};
		uni::Network::EthernetFrameView eth_frame{};
		if (!uni::Network::ParseEthernetFrame(raw_frame, eth_frame)) {
			plogwarn("[Net] rx dev=%s malformed ethernet len=%u",
				dev.getName() ? dev.getName() : "(unnamed)",
				(unsigned)length);
			return;
		}
		DispatchEthernetFrame(dev, eth_frame);
	}

	void NetServicePoll() {
		if (!EnsureNetServiceBuffers()) {
			plogwarn("[Net] frame buffer allocation failed");
			return;
		}
		ProcessTcpConnectTimers();
		ProcessTcpControlTimers();
		for0(i, net_link_device_count) {
			auto* dev = net_link_devices[i];
			if (!dev || dev->getState() != uni::Network::LinkState::Up) continue;
			uni::Network::LinkMutableFrameView frame{
				net_buffers.rx,
				NetFrameBufferSize,
				0,
			};
			const stdsint len = dev->Receive(frame);
			if (len <= 0) continue;
			frame.length = stduint(len);
			DispatchLinkFrame(*dev, frame.data, frame.length);
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

	bool NetServiceHandleAttach(stduint sig_src, const FMT_NetworkMsg_DRV_ATTACH& attach) {
		ProcessBlock* safe_pb = ProcessBlock::Acquire(sig_src);
		if (!safe_pb) return false;
		const bool allowed = safe_pb->ring == RING_S;
		ProcessBlock::Release(safe_pb);
		if (!allowed) return false;
		if (attach.version != NetworkDriverProtocolVersion || !attach.mtu) return false;

		net_remote_link.owner_tid = sig_src;
		net_remote_link.dev_handle = attach.dev_handle;
		net_remote_link.caps = attach.caps;
		net_remote_link.mtu = attach.mtu;
		for0(i, numsof(net_remote_link.mac.octet)) net_remote_link.mac.octet[i] = attach.mac[i];
		net_remote_link.state = attach.link_state ? uni::Network::LinkState::Up : uni::Network::LinkState::Down;
		MemSet(net_remote_link.name, 0, sizeof(net_remote_link.name));
		for0(i, NetworkDriverNameCapacity - 1) {
			net_remote_link.name[i] = attach.name[i];
			if (!attach.name[i]) break;
		}
		if (!net_remote_link.registered) {
			if (!Devsman::RegisterLinkDevice(&net_remote_link_device)) return false;
			net_remote_link.registered = true;
		}
		ploginfo("[Net] attached driver %s tid=%u mtu=%u",
			net_remote_link.name[0] ? net_remote_link.name : "(unnamed)",
			(unsigned)sig_src, (unsigned)attach.mtu);
		return true;
	}

	void NetServiceHandleDetach(stduint sig_src) {
		if (net_remote_link.owner_tid != sig_src) return;
		net_remote_link.owner_tid = 0;
		net_remote_link.dev_handle = 0;
		net_remote_link.caps = 0;
		net_remote_link.mtu = 0;
		net_remote_link.state = uni::Network::LinkState::Down;
		MemSet(&net_remote_link.mac, 0, sizeof(net_remote_link.mac));
		MemSet(net_remote_link.name, 0, sizeof(net_remote_link.name));
	}

	void NetServiceHandleMessage() {
		if (!EnsureNetServiceBuffers()) {
			plogwarn("[Net] frame buffer allocation failed");
			syscall(syscall_t::REST, 1, 10);
			return;
		}
		stduint sig_type = 0;
		stduint sig_src = 0;
		auto* msgbuf = net_buffers.driver_frame;
		if (!syscall(syscall_t::TMSG) && HasTcpTimersPending()) {
			ProcessTcpConnectTimers();
			ProcessTcpControlTimers();
			syscall(syscall_t::REST, 1, 10);
			return;
		}
		if (sysrecv(ANYPROC, msgbuf, sizeof(*msgbuf), &sig_type, &sig_src)) return;
		stdsint ret = -1;
		switch (NetworkMsg(sig_type)) {
		case NetworkMsg::DRV_ATTACH:
			ret = NetServiceHandleAttach(sig_src, *reinterpret_cast<FMT_NetworkMsg_DRV_ATTACH*>(msgbuf)) ? 0 : -1;
			syssend_async(sig_src, &ret, sizeof(ret));
			return;
		case NetworkMsg::DRV_DETACH:
			NetServiceHandleDetach(sig_src);
			ret = 0;
			syssend_async(sig_src, &ret, sizeof(ret));
			return;
		case NetworkMsg::DRV_RX:
			if (sig_src == net_remote_link.owner_tid && msgbuf->length <= NetworkDriverFrameCapacity) {
				DispatchLinkFrame(net_remote_link_device, msgbuf->data, msgbuf->length);
			}
			else {
				plogwarn("[Net] driver rx rejected src=%u owner=%u len=%u",
					(unsigned)sig_src, (unsigned)net_remote_link.owner_tid, (unsigned)msgbuf->length);
			}
			return;
		default:
			syssend_async(sig_src, &ret, sizeof(ret));
			return;
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
		device->getName() ? device->getName() : "(unnamed)",
		(unsigned)device->getMtu());
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
	auto* inbox = AllocateUdpInbox(port, false);
	if (!inbox) return false;
	if (!EnsureUdpInboxStorage(*inbox)) {
		(void)ReleaseUdpInbox(port, inbox->id);
		return false;
	}
	return RegisterUdpPortHandler(port, HandleUdpInboxDatagram);
}

bool Devsman::BindUdpPort(uint16 port) {
	stduint inbox_id = stduint(-1);
	return BindUdpPort(port, false, inbox_id);
}

bool Devsman::BindUdpPort(uint16 port, bool reuse_address, stduint& inbox_id) {
	inbox_id = stduint(-1);
	if (!port) return false;
	auto handler = FindUdpPortHandler(port);
	if (handler && handler != HandleUdpInboxDatagram) return false;
	if (FindUdpInbox(port) && (!reuse_address || !UdpPortAllowsReuse(port))) return false;
	auto* inbox = AllocateUdpInbox(port, reuse_address);
	if (!inbox) return false;
	if (!EnsureUdpInboxStorage(*inbox)) {
		(void)ReleaseUdpInbox(port, inbox->id);
		return false;
	}
	if (!handler && !RegisterUdpPortHandler(port, HandleUdpInboxDatagram)) {
		(void)ReleaseUdpInbox(port, inbox->id);
		return false;
	}
	inbox_id = inbox->id;
	return true;
}

bool Devsman::AllocateUdpPort(uint16& port) {
	stduint inbox_id = stduint(-1);
	return AllocateUdpPort(port, inbox_id);
}

bool Devsman::AllocateUdpPort(uint16& port, stduint& inbox_id) {
	inbox_id = stduint(-1);
	if (port && !IsUdpPortAvailable(port)) return false;
	if (port) return BindUdpPort(port, false, inbox_id);
	for (uint32 candidate = NetUdpEphemeralPortBegin; candidate <= NetUdpEphemeralPortEnd; candidate++) {
		const uint16 trial = uint16(candidate);
		if (!IsUdpPortAvailable(trial)) continue;
		if (!BindUdpPort(trial, false, inbox_id)) continue;
		port = trial;
		return true;
	}
	return false;
}

bool Devsman::CloseUdpPort(uint16 port) {
	auto* inbox = FindUdpInbox(port);
	if (!inbox) return false;
	return CloseUdpPort(port, inbox->id);
}

bool Devsman::CloseUdpPort(uint16 port, stduint inbox_id) {
	if (!port) return false;
	auto handler = FindUdpPortHandler(port);
	if (handler != HandleUdpInboxDatagram) return false;
	if (!ReleaseUdpInbox(port, inbox_id)) return false;
	if (!FindUdpInbox(port)) (void)UnregisterUdpPortHandler(port, HandleUdpInboxDatagram);
	return true;
}

bool Devsman::ListenTcpPort(uint16 port, stduint backlog) {
	if (!port) return false;
	auto* listener = FindTcpListener(port);
	if (listener) {
		ReleaseTcpConnectionsByLocalPort(port);
		listener->backlog = backlog;
		return EnsureTcpListenerObject(*listener, backlog);
	}
	ReleaseTcpConnectionsByLocalPort(port);
	for0(i, NetTcpListenCapacity) {
		if (net_tcp_listeners[i].port) continue;
		auto* tcp = net_tcp_listeners[i].tcp;
		net_tcp_listeners[i] = {};
		net_tcp_listeners[i].tcp = tcp;
		net_tcp_listeners[i].port = port;
		net_tcp_listeners[i].backlog = backlog;
		if (EnsureTcpListenerObject(net_tcp_listeners[i], backlog)) return true;
		net_tcp_listeners[i].port = 0;
		return false;
	}
	return false;
}

bool Devsman::CloseTcpPort(uint16 port) {
	for0(i, NetTcpListenCapacity) {
		if (net_tcp_listeners[i].port != port) continue;
		WakeTcpAcceptWaiters(net_tcp_listeners[i]);
		ReleaseTcpConnectionsByLocalPort(port);
		auto* tcp = net_tcp_listeners[i].tcp;
		net_tcp_listeners[i] = {};
		net_tcp_listeners[i].tcp = tcp;
		if (net_tcp_listeners[i].tcp) {
			net_tcp_listeners[i].tcp->Reset();
			net_tcp_listeners[i].tcp->setAcceptQueue(net_tcp_listeners[i].pending, NetTcpAcceptQueueDepth);
		}
		return true;
	}
	return false;
}

bool Devsman::IsTcpPortListening(uint16 port) {
	return FindTcpListener(port) != nullptr;
}

bool Devsman::WaitTcpAccept(uint16 port) {
	auto* listener = FindTcpListener(port);
	if (!listener) return false;
	if (listener->tcp && listener->tcp->getPendingAcceptCount()) return true;
	auto* th = Taskman::CurrentTB();
	if (!th) return false;
	if (!AddTcpAcceptWaiter(*listener, th)) return false;
	th->Block(ThreadBlock::BlockReason::BR_RecvMsg);
	Taskman::Schedule(true);
	listener = FindTcpListener(port);
	return listener && listener->tcp && listener->tcp->getPendingAcceptCount() != 0;
}

bool Devsman::HasTcpAccept(uint16 port) {
	auto* listener = FindTcpListener(port);
	return listener && listener->tcp && listener->tcp->getPendingAcceptCount() != 0;
}

stdsint Devsman::AcceptTcpConnection(uint16 port, uni::Network::TCPConnectionContext& context) {
	auto* listener = FindTcpListener(port);
	if (!listener) return -1;
	if (!DequeueTcpAccept(*listener, context)) return 0;
	return 1;
}

stdsint Devsman::ConnectTcp(const uni::Network::IPv4Address& target_ip,
	uint16 destination_port, uint16& source_port, uni::Network::TCPConnectionContext& context) {
	if (target_ip.isZero() || !destination_port) return -1;
	if (!AllocateTcpPort(source_port)) return -1;
	auto* connection = AllocateTcpConnection(net_config.ipv4_address, source_port,
		target_ip, destination_port);
	if (!connection || !connection->tcp) return -1;
	connection->active_open = true;
	auto& control = connection->tcp->getControl();
	const uint32 initial_sequence = net_tcp_next_sequence++;
	control.local_next_sequence = initial_sequence + 1;
	control.remote_next_sequence = 0;
	control.state = uni::Network::TCPConnectionState::SynReceived;
	connection->connect_started_tick = tick;
	context = control.context;
	if (!SendTcpSyn(*connection)) {
		ReleaseTcpConnection(context);
		return -1;
	}
	while (!IsTcpConnectReady(*connection)) {
		ProcessTcpConnectTimers();
		connection = FindTcpConnection(context.local.address, context.local.port,
			context.remote.address, context.remote.port);
		if (!connection) return -1;
		if (connection->reset_received) {
			ReleaseTcpConnection(context);
			return -1;
		}
		if (IsTcpConnectReady(*connection)) break;
		syscall(syscall_t::REST, 1, 10);
	}
	context = connection->tcp->getControl().context;
	return 1;
}

bool Devsman::CloseTcpConnection(const uni::Network::TCPConnectionContext& context) {
	auto* connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	if (!connection || !connection->tcp) return false;
	if (connection->reset_received) return ReleaseTcpConnection(context);
	while (connection->tx_count && !IsTcpTxRetryExhausted(*connection)) {
		ProcessTcpControlTimers();
		connection = FindTcpConnection(context.local.address, context.local.port,
			context.remote.address, context.remote.port);
		if (!connection || !connection->tcp) return false;
		if (!connection->tx_count) break;
		syscall(syscall_t::REST, 1, 10);
	}
	if (connection->tx_count) return ReleaseTcpConnection(context);
	if (!PrepareTcpTransmit(*connection)) {
		plogwarn("[Net] close tcp tx buffer unavailable");
		return ReleaseTcpConnection(context);
	}
	const stdsint sent = connection->tcp->Close();
	if (sent > 0) {
		connection->last_fin_tick = tick;
		connection->fin_retry_count++;
		if (connection->active_open) connection->close_phase = NetTcpClosePhase::FinWait1;
		return true;
	}
	return ReleaseTcpConnection(context);
}

bool Devsman::WaitTcpReceive(const uni::Network::TCPConnectionContext& context) {
	auto* connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	if (!connection) return false;
	if (IsTcpReceiveReady(*connection)) return true;
	auto* th = Taskman::CurrentTB();
	if (!th) return false;
	if (!AddTcpRxReader(*connection, th)) return false;
	th->Block(ThreadBlock::BlockReason::BR_RecvMsg);
	Taskman::Schedule(true);
	connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	return connection && IsTcpReceiveReady(*connection);
}

bool Devsman::HasTcpReceive(const uni::Network::TCPConnectionContext& context) {
	auto* connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	return connection && IsTcpReceiveReady(*connection);
}

bool Devsman::IsTcpReceiveClosed(const uni::Network::TCPConnectionContext& context) {
	auto* connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	if (!connection || !connection->tcp) return true;
	if (connection->reset_received) return false;
	return !connection->rx_count &&
		connection->tcp->getControl().state == uni::Network::TCPConnectionState::CloseWait;
}

bool Devsman::HasTcpError(const uni::Network::TCPConnectionContext& context) {
	auto* connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	return connection && connection->reset_received;
}

bool Devsman::HasTcpSendSpace(const uni::Network::TCPConnectionContext& context) {
	auto* connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	if (!connection || !connection->tcp) return false;
	if (connection->reset_received) return false;
	if (connection->close_phase != NetTcpClosePhase::None) return false;
	if (connection->tcp->getControl().state == uni::Network::TCPConnectionState::LastAck) return false;
	return connection->tx_count < NetTcpTxPendingCapacity && !IsTcpTxRetryExhausted(*connection);
}

stdsint Devsman::ReceiveTcp(const uni::Network::TCPConnectionContext& context, void* payload, stduint capacity) {
	auto* connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	if (!connection) return -1;
	if (connection->reset_received) return -1;
	return DequeueTcpRx(*connection, payload, capacity);
}

stdsint Devsman::SendTcp(const uni::Network::TCPConnectionContext& context, const void* payload, stduint length) {
	auto* connection = FindTcpConnection(context.local.address, context.local.port,
		context.remote.address, context.remote.port);
	if (!connection || !connection->tcp) return -1;
	if (connection->reset_received) return -1;
	if (connection->tcp->getControl().state == uni::Network::TCPConnectionState::LastAck) return -1;
	if (!length) return 0;
	if (!payload) return -1;
	if (!PrepareTcpTransmit(*connection)) {
		plogwarn("[Net] send tcp tx buffer unavailable");
		return -1;
	}
	stduint chunk_capacity = NetTcpSendMss(*connection);
	if (!chunk_capacity) return -1;
	const stduint buffer_capacity = NetFrameBufferSize - uni::Network::TCPMinHeaderLength;
	if (chunk_capacity > buffer_capacity) chunk_capacity = buffer_capacity;
	const auto* bytes = reinterpret_cast<const uint8*>(payload);
	stduint total = 0;
	while (total < length) {
		while (connection->tx_count >= NetTcpTxPendingCapacity) {
			if (IsTcpTxRetryExhausted(*connection)) return total ? stdsint(total) : -1;
			ProcessTcpControlTimers();
			connection = FindTcpConnection(context.local.address, context.local.port,
				context.remote.address, context.remote.port);
			if (!connection || !connection->tcp) return total ? stdsint(total) : -1;
			if (!connection->tx_count) break;
			syscall(syscall_t::REST, 1, 10);
		}
		if (!EnsureTcpTxStorage(*connection)) return total ? stdsint(total) : -1;
		const stduint remaining = length - total;
		const stduint chunk = remaining < chunk_capacity ? remaining : chunk_capacity;
		const uint32 sequence = connection->tcp->getControl().local_next_sequence;
		uni::Network::TransportPayloadContext packet{
			{
				{ uni::Network::NetworkAddressIPv4(context.local.address), context.local.port },
				{ uni::Network::NetworkAddressIPv4(context.remote.address), context.remote.port },
			},
			bytes + total,
			chunk,
		};
		if (!EnqueueTcpTxPending(*connection, sequence, bytes + total, chunk)) {
			return total ? stdsint(total) : -1;
		}
		const stdsint sent = connection->tcp->Send(packet);
		if (sent <= 0) {
			CancelTcpNewestTxPending(*connection);
			return total ? stdsint(total) : sent;
		}
		if (stduint(sent) < chunk) {
			CancelTcpNewestTxPending(*connection);
			if (!EnqueueTcpTxPending(*connection, sequence, bytes + total, stduint(sent))) {
				return total ? stdsint(total) : -1;
			}
		}
		total += stduint(sent);
		if (stduint(sent) < chunk) break;
	}
	return stdsint(total);
}

stdsint Devsman::ReceiveUdp(uint16 port, uni::Network::UDPDatagramContext& context, void* payload, stduint capacity) {
	auto* inbox = FindUdpInbox(port);
	if (!inbox) return 0;
	return ReceiveUdp(port, inbox->id, context, payload, capacity);
}

stdsint Devsman::ReceiveUdp(uint16 port, stduint inbox_id,
	uni::Network::UDPDatagramContext& context, void* payload, stduint capacity) {
	auto* inbox = FindUdpInbox(port, inbox_id);
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

bool Devsman::WaitUdp(uint16 port) {
	auto* inbox = FindUdpInbox(port);
	if (!inbox) return false;
	return WaitUdp(port, inbox->id);
}

bool Devsman::WaitUdp(uint16 port, stduint inbox_id) {
	auto* inbox = FindUdpInbox(port, inbox_id);
	if (!inbox || !inbox->IsReady()) return false;
	if (inbox->count) return true;
	auto* th = Taskman::CurrentTB();
	if (!th) return false;
	if (!AddUdpInboxReader(*inbox, th)) return false;
	th->Block(ThreadBlock::BlockReason::BR_RecvMsg);
	Taskman::Schedule(true);
	inbox = FindUdpInbox(port, inbox_id);
	return inbox && inbox->IsReady() && inbox->count != 0;
}

bool Devsman::HasUdp(uint16 port, stduint inbox_id) {
	auto* inbox = FindUdpInbox(port, inbox_id);
	return inbox && inbox->IsReady() && inbox->count != 0;
}

stdsint Devsman::SendUdp(const uni::Network::MacAddress& target_mac, const uni::Network::IPv4Address& target_ip,
	uint16 source_port, uint16 destination_port, const void* payload, stduint length) {
	if (!source_port || !destination_port) {
		plogwarn("[Net] send udp invalid ports src=%u dst=%u",
			(unsigned)source_port, (unsigned)destination_port);
		return -1;
	}
	if (target_mac.isZero() || target_ip.isZero()) {
		plogwarn("[Net] send udp invalid target mac/ip");
		return -1;
	}
	if (!EnsureUdpTxBuffer()) {
		plogwarn("[Net] send udp tx buffer unavailable");
		return -1;
	}
	auto* dev = FindDefaultLinkDevice();
	if (!dev) {
		plogwarn("[Net] send udp no default link device");
		return -1;
	}
	uni::Network::UDPObject udp(&net_ipv4_interface, net_buffers.udp_tx, NetFrameBufferSize);
	udp.setIdentification(net_ipv4_identification++);
	udp.Bind({ uni::Network::NetworkAddressIPv4(net_config.ipv4_address), source_port });
	udp.Connect({ uni::Network::NetworkAddressIPv4(target_ip), destination_port });
	uni::Network::TransportPayloadContext context{
		{
			{ uni::Network::NetworkAddressIPv4(net_config.ipv4_address), source_port },
			{ uni::Network::NetworkAddressIPv4(target_ip), destination_port },
		},
		payload,
		length,
	};
	uni::Network::NetworkPacketContext packet{};
	if (udp.BuildPacket(packet, context) < 0) {
		return -1;
	}
	const stdsint sent = SendIPv4PacketFrame(*dev, target_mac, packet);
	return sent > 0 ? stdsint(length) : sent;
}

stdsint Devsman::SendUdp(const uni::Network::IPv4Address& target_ip,
	uint16 source_port, uint16 destination_port, const void* payload, stduint length) {
	if (!source_port || !destination_port || target_ip.isZero()) {
		plogwarn("[Net] send udp invalid args src=%u dst=%u ip=%u.%u.%u.%u",
			(unsigned)source_port, (unsigned)destination_port,
			(unsigned)target_ip.octet[0], (unsigned)target_ip.octet[1],
			(unsigned)target_ip.octet[2], (unsigned)target_ip.octet[3]);
		return -1;
	}
	if (!EnsureUdpTxBuffer()) {
		plogwarn("[Net] send udp tx buffer unavailable");
		return -1;
	}
	NetIPv4Route route{};
	if (!ResolveIPv4Route(target_ip, route)) {
		plogwarn("[Net] send udp no route target=%u.%u.%u.%u",
			(unsigned)target_ip.octet[0], (unsigned)target_ip.octet[1],
			(unsigned)target_ip.octet[2], (unsigned)target_ip.octet[3]);
		return -1;
	}
	uni::Network::MacAddress target_mac{};
	if (!LookupArpCache(route.next_hop, target_mac)) {
		const stdsint pending_index = EnqueuePendingUdp(target_ip, route.next_hop,
			source_port, destination_port, payload, length);
		if (pending_index < 0) {
			plogwarn("[Net] send udp pending queue full/invalid len=%u", (unsigned)length);
			return -1;
		}
		if (!ShouldSendPendingArpRequest(route.next_hop)) return stdsint(length);
		if (SendArpRequest(*route.dev, net_buffers.udp_tx, route.next_hop)) {
			MarkPendingArpRequest(route.next_hop);
			return stdsint(length);
		}
		plogwarn("[Net] send udp arp request failed for %u.%u.%u.%u",
			(unsigned)route.next_hop.octet[0], (unsigned)route.next_hop.octet[1],
			(unsigned)route.next_hop.octet[2], (unsigned)route.next_hop.octet[3]);
		ClearPendingUdp(stduint(pending_index));
		return -1;
	}
	const stdsint sent = SendUdpDatagram(net_ipv4_interface, net_buffers.udp_tx,
		route.source, target_ip, source_port, destination_port,
		payload, length, net_ipv4_identification++);
	return sent > 0 ? stdsint(length) : sent;
}

bool Devsman::GetDefaultIPv4Route(void* route, stduint length) {
	if (!route || length < sizeof(syscall_net_route_ipv4_t)) return false;
	auto* output = reinterpret_cast<syscall_net_route_ipv4_t*>(route);
	auto* dev = FindDefaultLinkDevice();
	if (!dev) return false;
	const stduint index = FindLinkDeviceIndex(dev);
	if (index == stduint(-1)) return false;
	*output = {};
	for0(i, uni::Network::IPv4AddressLength) {
		output->destination[i] = 0;
		output->netmask[i] = 0;
		output->gateway[i] = net_config.ipv4_gateway.octet[i];
	}
	output->flags = syscall_net_route_flag_gateway;
	if (dev->getState() == uni::Network::LinkState::Up) output->flags |= syscall_net_route_flag_up;
	output->link_index = uint16(index);
	return true;
}

bool Devsman::GetIPv4Interface(stduint index, void* iface, stduint length) {
	if (!iface || length < sizeof(syscall_net_interface_ipv4_t)) return false;
	auto* output = reinterpret_cast<syscall_net_interface_ipv4_t*>(iface);
	return FillIPv4Interface(index, *output);
}

stduint Devsman::IPv4ArpCacheCount() {
	ExpireArpCache();
	stduint count = 0;
	for0(i, NetArpCacheCapacity) if (net_arp_cache[i].valid) count++;
	return count;
}

bool Devsman::GetIPv4ArpCacheEntry(stduint index, void* entry, stduint length) {
	if (!entry || length < sizeof(syscall_net_arp_ipv4_t)) return false;
	ExpireArpCache();
	auto* output = reinterpret_cast<syscall_net_arp_ipv4_t*>(entry);
	stduint ordinal = 0;
	for0(i, NetArpCacheCapacity) {
		const auto& cached = net_arp_cache[i];
		if (!cached.valid) continue;
		if (ordinal++ != index) continue;
		*output = {};
		for0(j, uni::Network::IPv4AddressLength) output->address[j] = cached.protocol.octet[j];
		for0(j, numsof(output->hardware)) output->hardware[j] = cached.hardware.octet[j];
		output->flags = syscall_net_route_flag_up;
		output->entry_index = uint16(index);
		return true;
	}
	return false;
}

stduint Devsman::TcpListenerCount() {
	stduint count = 0;
	for0(i, NetTcpListenCapacity) {
		if (net_tcp_listeners[i].port && net_tcp_listeners[i].tcp) count++;
	}
	return count;
}

bool Devsman::GetTcpListenerEntry(stduint index, void* entry, stduint length) {
	if (!entry || length < sizeof(syscall_net_tcp_listener_t)) return false;
	auto* output = reinterpret_cast<syscall_net_tcp_listener_t*>(entry);
	stduint ordinal = 0;
	for0(i, NetTcpListenCapacity) {
		auto& listener = net_tcp_listeners[i];
		if (!listener.port || !listener.tcp) continue;
		if (ordinal++ != index) continue;
		*output = {};
		output->port = listener.port;
		output->flags = syscall_net_route_flag_up;
		output->backlog = uint16(TcpListenerBacklogLimit(listener));
		output->pending = uint16(listener.tcp->getPendingAcceptCount());
		output->entry_index = uint16(index);
		return true;
	}
	return false;
}

stduint Devsman::TcpConnectionCount() {
	stduint count = 0;
	for0(i, NetTcpConnectionCapacity) {
		if (net_tcp_connections[i].valid && net_tcp_connections[i].tcp) count++;
	}
	return count;
}

bool Devsman::GetTcpConnectionEntry(stduint index, void* entry, stduint length) {
	if (!entry || length < sizeof(syscall_net_tcp_connection_t)) return false;
	auto* output = reinterpret_cast<syscall_net_tcp_connection_t*>(entry);
	stduint ordinal = 0;
	for0(i, NetTcpConnectionCapacity) {
		auto& connection = net_tcp_connections[i];
		if (!connection.valid || !connection.tcp) continue;
		if (ordinal++ != index) continue;
		const auto& control = connection.tcp->getControl();
		*output = {};
		for0(j, uni::Network::IPv4AddressLength) {
			output->local_address[j] = control.context.local.address.octet[j];
			output->remote_address[j] = control.context.remote.address.octet[j];
		}
		output->local_port = control.context.local.port;
		output->remote_port = control.context.remote.port;
		switch (connection.close_phase) {
		case NetTcpClosePhase::FinWait1:
			output->state = 4;
			break;
		case NetTcpClosePhase::FinWait2:
			output->state = 5;
			break;
		case NetTcpClosePhase::Closing:
			output->state = 6;
			break;
		case NetTcpClosePhase::TimeWait:
			output->state = 7;
			break;
		case NetTcpClosePhase::Reset:
			output->state = 8;
			break;
		default:
			output->state = uint16(control.state);
			break;
		}
		output->flags = syscall_net_route_flag_up;
		if (connection.active_open) output->flags |= 0x0100u;
		if (connection.local_fin_acknowledged) output->flags |= 0x0200u;
		if (IsTcpTxRetryExhausted(connection)) output->flags |= 0x0400u;
		if (connection.reset_received) output->flags |= 0x0800u;
		output->entry_index = uint16(index);
		output->rx_bytes = uint16(connection.rx_count);
		output->tx_pending = uint16(connection.tx_count);
		output->tx_retry_count = connection.tx_count ?
			uint16(connection.tx_pending[connection.tx_head].retry_count) : 0;
		output->local_mss = connection.local_mss;
		output->peer_mss = connection.peer_mss;
		output->send_mss = uint16(NetTcpSendMss(connection));
		return true;
	}
	return false;
}

void serv_dev_net_loop() {
	RegisterBuiltinUdpPorts();
	ploginfo("[Net] Service thread start pid=%u", Taskman::CurrentPID());
	while (true) {
		NetServiceHandleMessage();
	}
}

#endif
