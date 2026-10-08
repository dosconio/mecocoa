#include "aaaaa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void PrintUsage() {
	printf("usage: netinfo [help]\n\r");
	printf("  list IPv4 network interfaces\n\r");
	printf("  fields: index device ip mask mac mtu state dhcp dns lease\n\r");
	printf("  also prints the default IPv4 route\n\r");
	printf("  also prints IPv4 ARP cache entries\n\r");
	printf("  also prints TCP listener and connection state\n\r");
	printf("  also prints network packet counters\n\r");
	printf("  views: netinfo --if | --route | --arp | --udp | --tcp | --dhcp | --pending | --stats | --config\n\r");
	printf("  dns: netinfo --dns example.com\n\r");
	printf("  dns6: netinfo --dns6 example.com\n\r");
	printf("  dns cache: netinfo --dns-cache [example.com]\n\r");
	printf("  dns clear: netinfo --dns-cache-clear [example.com]\n\r");
	printf("  dns srv: netinfo --dns-server 10.0.2.1 [1.1.1.1 ...]|none\n\r");
	printf("  dhcp: netinfo --dhcp-renew | --dhcp-release\n\r");
	printf("  fault: netinfo --fault | --fault-off | --fault-reset\n\r");
	printf("  fault set: netinfo --fault-set rx|tx|both any|arp|ipv4|icmp|udp|tcp dport drop duplicate reorder corrupt truncate [length] [delay] [start]\n\r");
}

static void PrintIPv4(const uint8 address[4]) {
	char text[16] = {};
	if (mcca_net_format_ipv4(text, sizeof(text), address)) printf("%s", text);
}

static void PrintMac(const uint8 address[6]) {
	printf("%[8H]:%[8H]:%[8H]:%[8H]:%[8H]:%[8H]",
		(stduint)address[0], (stduint)address[1], (stduint)address[2],
		(stduint)address[3], (stduint)address[4], (stduint)address[5]);
}

static bool ParseUInt32(const char* text, uint32& value) {
	if (!text || !text[0]) return false;
	value = 0;
	for (stduint i = 0; text[i]; i++) {
		if (text[i] < '0' || text[i] > '9') return false;
		const uint32 digit = uint32(text[i] - '0');
		if (value > (0xFFFFFFFFu - digit) / 10u) return false;
		value = value * 10u + digit;
	}
	return true;
}

static const char* FaultDirectionName(uint32 flags) {
	if (flags == (syscall_net_fault_rx | syscall_net_fault_tx)) return "both";
	if (flags == syscall_net_fault_rx) return "rx";
	if (flags == syscall_net_fault_tx) return "tx";
	return "off";
}

static const char* FaultProtocolName(const syscall_net_fault_t& fault) {
	if (fault.ether_type == 0x0806u) return "arp";
	if (fault.ether_type == 0x0800u && fault.ipv4_protocol == 1u) return "icmp";
	if (fault.ether_type == 0x0800u && fault.ipv4_protocol == 6u) return "tcp";
	if (fault.ether_type == 0x0800u && fault.ipv4_protocol == 17u) return "udp";
	if (fault.ether_type == 0x0800u) return "ipv4";
	return "any";
}

static int PrintFaultState() {
	syscall_net_fault_t fault{};
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::FaultGet),
		_IMM(&fault), sizeof(fault)) < 0) {
		printf("netinfo: fault query failed\n\r");
		return 1;
	}
	printf("netinfo: fault direction=%s protocol=%s ethertype=%u ipproto=%u sport=%u dport=%u\n\r",
		FaultDirectionName(fault.flags), FaultProtocolName(fault),
		(unsigned)fault.ether_type, (unsigned)fault.ipv4_protocol,
		(unsigned)fault.source_port, (unsigned)fault.destination_port);
	printf("netinfo: fault rules start=%u drop=%u duplicate=%u reorder=%u corrupt=%u truncate=%u length=%u delay=%u\n\r",
		(unsigned)fault.start_after, (unsigned)fault.drop_every,
		(unsigned)fault.duplicate_every, (unsigned)fault.reorder_every,
		(unsigned)fault.corrupt_every, (unsigned)fault.truncate_every,
		(unsigned)fault.truncate_length, (unsigned)fault.reorder_delay_ticks);
	printf("netinfo: fault rx seen=%u dropped=%u duplicated=%u reordered=%u corrupted=%u truncated=%u held=%u\n\r",
		(unsigned)fault.rx_seen, (unsigned)fault.rx_dropped,
		(unsigned)fault.rx_duplicated, (unsigned)fault.rx_reordered,
		(unsigned)fault.rx_corrupted, (unsigned)fault.rx_truncated,
		(unsigned)fault.rx_held);
	printf("netinfo: fault tx seen=%u dropped=%u duplicated=%u reordered=%u corrupted=%u truncated=%u held=%u\n\r",
		(unsigned)fault.tx_seen, (unsigned)fault.tx_dropped,
		(unsigned)fault.tx_duplicated, (unsigned)fault.tx_reordered,
		(unsigned)fault.tx_corrupted, (unsigned)fault.tx_truncated,
		(unsigned)fault.tx_held);
	return 0;
}

static bool ParseFaultDirection(const char* text, uint32& flags) {
	if (!StrCompare(text, "rx")) flags = syscall_net_fault_rx;
	else if (!StrCompare(text, "tx")) flags = syscall_net_fault_tx;
	else if (!StrCompare(text, "both")) flags = syscall_net_fault_rx | syscall_net_fault_tx;
	else return false;
	return true;
}

static bool ParseFaultProtocol(const char* text, syscall_net_fault_t& fault) {
	if (!StrCompare(text, "any")) return true;
	fault.ether_type = 0x0800u;
	if (!StrCompare(text, "ipv4")) return true;
	if (!StrCompare(text, "icmp")) fault.ipv4_protocol = 1;
	else if (!StrCompare(text, "tcp")) fault.ipv4_protocol = 6;
	else if (!StrCompare(text, "udp")) fault.ipv4_protocol = 17;
	else if (!StrCompare(text, "arp")) {
		fault.ether_type = 0x0806u;
		fault.ipv4_protocol = 0;
	}
	else return false;
	return true;
}

static int SetFaultState(int argc, char** argv) {
	if (argc < 10 || argc > 13) return -1;
	syscall_net_fault_t fault{};
	if (!ParseFaultDirection(argv[2], fault.flags) || !ParseFaultProtocol(argv[3], fault)) return -1;
	uint32 values[9]{};
	for (int i = 4; i < argc; i++) {
		if (!ParseUInt32(argv[i], values[i - 4])) return -1;
	}
	fault.destination_port = values[0];
	fault.drop_every = values[1];
	fault.duplicate_every = values[2];
	fault.reorder_every = values[3];
	fault.corrupt_every = values[4];
	fault.truncate_every = values[5];
	fault.truncate_length = argc > 10 ? values[6] : 0;
	fault.reorder_delay_ticks = argc > 11 ? values[7] : (fault.reorder_every ? 1u : 0u);
	fault.start_after = argc > 12 ? values[8] : 0;
	if (fault.destination_port > 0xFFFFu) return -1;
	if (fault.destination_port && fault.ipv4_protocol != 6u && fault.ipv4_protocol != 17u) return -1;
	if (fault.truncate_every && !fault.truncate_length) return -1;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::FaultSet),
		_IMM(&fault), sizeof(fault)) < 0) {
		printf("netinfo: fault set failed\n\r");
		return 1;
	}
	return PrintFaultState();
}

static const char* ConfigSourceName(uint16 source) {
	switch (source) {
	case syscall_net_config_source_dhcp_offered:
		return "dhcp-offered";
	case syscall_net_config_source_dhcp_bound:
		return "dhcp-bound";
	case syscall_net_config_source_dhcp_nak:
		return "dhcp-nak";
	case syscall_net_config_source_dhcp_failed:
		return "dhcp-failed";
	case syscall_net_config_source_temporary:
		return "temporary";
	default:
		return "static";
	}
}

static const char* ConfigSourceName(uni::Network::NetworkConfigSource source) {
	switch (source) {
	case uni::Network::NetworkConfigSource::DHCP:
		return "dhcp";
	case uni::Network::NetworkConfigSource::Temporary:
		return "temporary";
	case uni::Network::NetworkConfigSource::Failed:
		return "failed";
	case uni::Network::NetworkConfigSource::None:
		return "none";
	case uni::Network::NetworkConfigSource::Static:
	default:
		return "static";
	}
}

static const char* DhcpStateName(uint16 state) {
	switch (state) {
	case syscall_net_dhcp_state_init:
		return "init";
	case syscall_net_dhcp_state_discovering:
		return "discovering";
	case syscall_net_dhcp_state_offered:
		return "offered";
	case syscall_net_dhcp_state_requesting:
		return "requesting";
	case syscall_net_dhcp_state_bound:
		return "bound";
	case syscall_net_dhcp_state_nak:
		return "nak";
	default:
		return "none";
	}
}

static void PrintDefaultRoute() {
	syscall_net_route_ipv4_t route{};
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4Default),
		_IMM(&route), sizeof(route)) < 0) {
		printf("netinfo: default route query failed\n\r");
		return;
	}
	printf("netinfo: default dst=");
	PrintIPv4(route.destination);
	printf(" mask=");
	PrintIPv4(route.netmask);
	printf(" gw=");
	PrintIPv4(route.gateway);
	printf(" if%u %s\n\r", (unsigned)route.link_index,
		(route.flags & syscall_net_route_flag_up) ? "up" : "down");
}

static void PrintArpCache() {
	stduint count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4ArpCacheCount),
		_IMM(&count), sizeof(count)) < 0) {
		printf("netinfo: arp query failed\n\r");
		return;
	}
	printf("netinfo: arp entries=%u\n\r", (unsigned)count);
	for0(i, count) {
		syscall_net_arp_ipv4_t entry{};
		entry.entry_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4ArpCacheEntry),
			_IMM(&entry), sizeof(entry)) < 0) {
			printf("netinfo: arp%u query failed\n\r", (unsigned)i);
			continue;
		}
		printf("netinfo: arp%u ip=", (unsigned)i);
		PrintIPv4(entry.address);
		printf(" mac=");
		PrintMac(entry.hardware);
		printf("\n\r");
	}
}

static void PrintUdpState() {
	stduint inbox_count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::UDPInboxCount),
		_IMM(&inbox_count), sizeof(inbox_count)) < 0) {
		printf("netinfo: udp inbox query failed\n\r");
		return;
	}
	printf("netinfo: udp inboxes=%u\n\r", (unsigned)inbox_count);
	for0(i, inbox_count) {
		syscall_net_udp_inbox_t inbox{};
		inbox.entry_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::UDPInboxEntry),
			_IMM(&inbox), sizeof(inbox)) < 0) {
			printf("netinfo: udp inbox%u query failed\n\r", (unsigned)i);
			continue;
		}
		printf("netinfo: udp%u local=", (unsigned)i);
		PrintIPv4(inbox.local_address);
		printf(":%u queued=%u/%u drops=%u waiters=%u/%u payload=%u inbox=%u %s%s\n\r",
			(unsigned)inbox.port, (unsigned)inbox.queued,
			(unsigned)inbox.queue_capacity,
			(unsigned)inbox.drops, (unsigned)inbox.waiters,
			(unsigned)inbox.waiter_capacity,
			(unsigned)inbox.payload_capacity, (unsigned)inbox.inbox_id,
			(inbox.flags & syscall_net_route_flag_up) ? "up" : "down",
			(inbox.flags & 0x0100u) ? " reuse" : "");
	}

	stduint pending_count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::UDPPendingCount),
		_IMM(&pending_count), sizeof(pending_count)) < 0) {
		printf("netinfo: udp pending query failed\n\r");
		return;
	}
	printf("netinfo: udp pending=%u\n\r", (unsigned)pending_count);
	for0(i, pending_count) {
		syscall_net_pending_udp_t pending{};
		pending.entry_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::UDPPendingEntry),
			_IMM(&pending), sizeof(pending)) < 0) {
			printf("netinfo: udp pending%u query failed\n\r", (unsigned)i);
			continue;
		}
		printf("netinfo: udp-pending%u target=", (unsigned)i);
		PrintIPv4(pending.target_address);
		printf(" next-hop=");
		PrintIPv4(pending.next_hop);
		printf(" source=");
		PrintIPv4(pending.source_address);
		printf(":%u dst=%u len=%u arp=%u age=%u\n\r",
			(unsigned)pending.source_port, (unsigned)pending.destination_port,
			(unsigned)pending.payload_length, (unsigned)pending.arp_requests,
			(unsigned)pending.age_ticks);
	}
}

static void PrintUdpPendingOnly() {
	stduint pending_count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::UDPPendingCount),
		_IMM(&pending_count), sizeof(pending_count)) < 0) {
		printf("netinfo: udp pending query failed\n\r");
		return;
	}
	printf("netinfo: udp pending=%u\n\r", (unsigned)pending_count);
	for0(i, pending_count) {
		syscall_net_pending_udp_t pending{};
		pending.entry_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::UDPPendingEntry),
			_IMM(&pending), sizeof(pending)) < 0) {
			printf("netinfo: udp pending%u query failed\n\r", (unsigned)i);
			continue;
		}
		printf("netinfo: udp-pending%u target=", (unsigned)i);
		PrintIPv4(pending.target_address);
		printf(" next-hop=");
		PrintIPv4(pending.next_hop);
		printf(" source=");
		PrintIPv4(pending.source_address);
		printf(":%u dst=%u len=%u arp=%u age=%u\n\r",
			(unsigned)pending.source_port, (unsigned)pending.destination_port,
			(unsigned)pending.payload_length, (unsigned)pending.arp_requests,
			(unsigned)pending.age_ticks);
	}
}

static const char* TcpStateName(uint16 state) {
	switch (state) {
	case 0: return "syn-received";
	case 1: return "established";
	case 2: return "close-wait";
	case 3: return "last-ack";
	case 4: return "fin-wait1";
	case 5: return "fin-wait2";
	case 6: return "closing";
	case 7: return "time-wait";
	case 8: return "reset";
	case 9: return "closed";
	default: return "unknown";
	}
}

static const char* TcpStateDisplayName(const syscall_net_tcp_connection_t& connection) {
	if (connection.state == 0 && (connection.flags & 0x0100u)) return "syn-sent";
	return TcpStateName(connection.state);
}

static const char* TcpClosePhaseName(uint16 phase) {
	switch (phase) {
	case 0: return "none";
	case 1: return "fin-wait1";
	case 2: return "fin-wait2";
	case 3: return "closing";
	case 4: return "time-wait";
	case 5: return "reset";
	case 6: return "closed";
	default: return "unknown";
	}
}

static const char* TcpCloseReasonName(uint16 reason) {
	switch (reason) {
	case 0: return "none";
	case 1: return "peer-fin";
	case 2: return "active-fin";
	case 3: return "fin-ack";
	case 4: return "time-wait";
	case 5: return "reset";
	case 6: return "tx-timeout";
	case 7: return "connect-error";
	case 8: return "keepalive-timeout";
	case 9: return "fin-pending";
	default: return "unknown";
	}
}

static const char* TcpDirectionName(uint16 flags) {
	return (flags & 0x0100u) ? "active" : "passive";
}

static void PrintTcpState() {
	stduint listener_count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::TCPListenerCount),
		_IMM(&listener_count), sizeof(listener_count)) < 0) {
		printf("netinfo: tcp listener query failed\n\r");
		return;
	}
	printf("netinfo: tcp listeners=%u\n\r", (unsigned)listener_count);
	for0(i, listener_count) {
		syscall_net_tcp_listener_t listener{};
		listener.entry_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::TCPListenerEntry),
			_IMM(&listener), sizeof(listener)) < 0) {
			printf("netinfo: tcp listener%u query failed\n\r", (unsigned)i);
			continue;
		}
		printf("netinfo: tcp-listen%u local=", (unsigned)i);
		PrintIPv4(listener.local_address);
		printf(":%u backlog=%u pending=%u %s\n\r",
			(unsigned)listener.port, (unsigned)listener.backlog, (unsigned)listener.pending,
			(listener.flags & syscall_net_route_flag_up) ? "up" : "down");
	}

	stduint connection_count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::TCPConnectionCount),
		_IMM(&connection_count), sizeof(connection_count)) < 0) {
		printf("netinfo: tcp connection query failed\n\r");
		return;
	}
	printf("netinfo: tcp connections=%u\n\r", (unsigned)connection_count);
	stduint count_connecting = 0;
	stduint count_established = 0;
	stduint count_close_wait = 0;
	stduint count_closing = 0;
	stduint count_fin_wait = 0;
	stduint count_time_wait = 0;
	stduint count_reset = 0;
	stduint count_closed = 0;
	for0(i, connection_count) {
		syscall_net_tcp_connection_t connection{};
		connection.entry_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::TCPConnectionEntry),
			_IMM(&connection), sizeof(connection)) < 0) {
			printf("netinfo: tcp connection%u query failed\n\r", (unsigned)i);
			continue;
		}
		if (connection.state == 0 && (connection.flags & 0x0100u)) count_connecting++;
		else if (connection.state == 1) count_established++;
		else if (connection.state == 2) count_close_wait++;
		else if (connection.state == 4 || connection.state == 5 || connection.state == 6) {
			count_closing++;
			if (connection.state == 4 || connection.state == 5) count_fin_wait++;
		}
		else if (connection.state == 7) count_time_wait++;
		else if (connection.state == 8) count_reset++;
		else if (connection.state == 9) count_closed++;
		printf("netinfo: tcp%u local=", (unsigned)i);
		PrintIPv4(connection.local_address);
		printf(":%u peer=", (unsigned)connection.local_port);
		PrintIPv4(connection.remote_address);
		printf(":%u state=%s phase=%s reason=%s dir=%s err=%u/%s rx=%u win=%u peerwin=%u tx=%u retry=%u rexmit=%u mss=%u/%u send=%u dup=%u ooo=%u full=%u idle=%u/%u",
			(unsigned)connection.remote_port, TcpStateDisplayName(connection),
			TcpClosePhaseName(connection.close_phase), TcpCloseReasonName(connection.close_reason),
			TcpDirectionName(connection.flags),
			(unsigned)connection.error, mcca_net_socket_error_name(connection.error),
			(unsigned)connection.rx_bytes, (unsigned)connection.rx_window,
			(unsigned)connection.peer_window,
			(unsigned)connection.tx_pending, (unsigned)connection.tx_retry_count,
			(unsigned)connection.tx_retransmit,
			(unsigned)connection.local_mss, (unsigned)connection.peer_mss,
			(unsigned)connection.send_mss,
			(unsigned)connection.rx_duplicate, (unsigned)connection.rx_out_of_order,
			(unsigned)connection.rx_window_full,
			(unsigned)connection.rx_idle_ticks, (unsigned)connection.tx_idle_ticks);
		if (connection.time_wait_age || connection.time_wait_remaining) {
			printf(" tw-age=%u tw-left=%u",
				(unsigned)connection.time_wait_age, (unsigned)connection.time_wait_remaining);
		}
		if (connection.fin_age_ticks) printf(" fin-age=%u", (unsigned)connection.fin_age_ticks);
		if (connection.fin_retry_count) printf(" fin-retry=%u", (unsigned)connection.fin_retry_count);
		if (connection.close_age_ticks || connection.close_remaining_ticks) {
			printf(" close-age=%u close-left=%u",
				(unsigned)connection.close_age_ticks, (unsigned)connection.close_remaining_ticks);
		}
		if (connection.keepalive_idle_ticks) {
			printf(" keepalive=on/%u/%u/%u probes=%u",
				(unsigned)connection.keepalive_idle_ticks,
				(unsigned)connection.keepalive_interval_ticks,
				(unsigned)connection.keepalive_probe_limit,
				(unsigned)connection.keepalive_probe_count);
		}
		else printf(" keepalive=off");
		if (connection.flags & 0x0200u) printf(" fin-ack");
		if (connection.flags & 0x0400u) printf(" tx-exhausted");
		if (connection.flags & 0x0800u) printf(" reset");
		if (connection.flags & 0x1000u) printf(" rx-shutdown");
		if (connection.flags & 0x2000u) printf(" peer-fin");
		if (connection.flags & 0x4000u) printf(" keepalive-timeout");
		if (connection.flags & 0x8000u) printf(" tx-shutdown");
		printf("\n\r");
	}
	if (connection_count) {
		printf("netinfo: tcp summary connecting=%u established=%u close-wait=%u fin-wait=%u closing=%u time-wait=%u reset=%u closed=%u\n\r",
			(unsigned)count_connecting, (unsigned)count_established,
			(unsigned)count_close_wait, (unsigned)count_fin_wait,
			(unsigned)count_closing, (unsigned)count_time_wait,
			(unsigned)count_reset, (unsigned)count_closed);
	}
	syscall_net_stats_t stats{};
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::NetStats),
		_IMM(&stats), sizeof(stats)) >= 0 && stats.tcp_connect_last_error) {
		printf("netinfo: tcp last-fail local=");
		PrintIPv4(stats.tcp_connect_last_local_address);
		printf(":%u peer=", (unsigned)stats.tcp_connect_last_local_port);
		PrintIPv4(stats.tcp_connect_last_remote_address);
		printf(":%u err=%u/%s age=%u\n\r", (unsigned)stats.tcp_connect_last_remote_port,
			(unsigned)stats.tcp_connect_last_error,
			mcca_net_socket_error_name(stats.tcp_connect_last_error),
			(unsigned)stats.tcp_connect_last_age);
	}
	else {
		printf("netinfo: tcp last-fail none\n\r");
	}
}

static void PrintTcpConnectingOnly() {
	stduint connection_count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::TCPConnectionCount),
		_IMM(&connection_count), sizeof(connection_count)) < 0) {
		printf("netinfo: tcp connection query failed\n\r");
		return;
	}
	stduint connecting = 0;
	for0(i, connection_count) {
		syscall_net_tcp_connection_t connection{};
		connection.entry_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::TCPConnectionEntry),
			_IMM(&connection), sizeof(connection)) < 0) continue;
		if (!(connection.state == 0 && (connection.flags & 0x0100u))) continue;
		if (!connecting++) printf("netinfo: tcp connecting\n\r");
		printf("netinfo: tcp-connect%u local=", (unsigned)i);
		PrintIPv4(connection.local_address);
		printf(":%u peer=", (unsigned)connection.local_port);
		PrintIPv4(connection.remote_address);
		printf(":%u err=%u/%s retry=%u\n\r", (unsigned)connection.remote_port,
			(unsigned)connection.error, mcca_net_socket_error_name(connection.error),
			(unsigned)connection.tx_retry_count);
	}
	printf("netinfo: tcp connecting=%u\n\r", (unsigned)connecting);
}

static int QueryInterface(stduint index, syscall_net_interface_ipv4_t& iface) {
	iface = {};
	iface.link_index = uint16(index);
	return syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4Interface),
		_IMM(&iface), sizeof(iface));
}

static void PrintDhcpState() {
	stduint count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4InterfaceCount),
		_IMM(&count), sizeof(count)) < 0) {
		printf("netinfo: dhcp interface query failed\n\r");
		return;
	}
	printf("netinfo: dhcp interfaces=%u\n\r", (unsigned)count);
	for0(i, count) {
		syscall_net_interface_ipv4_t iface{};
		if (QueryInterface(i, iface) < 0) {
			printf("netinfo: dhcp if%u query failed\n\r", (unsigned)i);
			continue;
		}
		printf("netinfo: dhcp%u dev=%s src=%s state=%s xid=%[32H] retry=%u lease=%u age=%u t1=%u t2=%u renew-in=%u",
			(unsigned)i, iface.name[0] ? iface.name : "(none)",
			ConfigSourceName(iface.config_source), DhcpStateName(iface.dhcp_state),
			(stduint)iface.dhcp_xid, (unsigned)iface.dhcp_retry_count,
			(unsigned)iface.dhcp_lease_time, (unsigned)iface.dhcp_bound_age,
			(unsigned)iface.dhcp_t1_time, (unsigned)iface.dhcp_t2_time,
			(unsigned)iface.dhcp_renew_in);
		if (iface.dhcp_server[0] || iface.dhcp_server[1] || iface.dhcp_server[2] || iface.dhcp_server[3]) {
			printf(" server=");
			PrintIPv4(iface.dhcp_server);
		}
		if (iface.dns[0] || iface.dns[1] || iface.dns[2] || iface.dns[3]) {
			printf(" dns=");
			PrintIPv4(iface.dns);
		}
		printf("\n\r");
	}
}

static void PrintNetStats() {
	syscall_net_stats_t stats{};
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::NetStats),
		_IMM(&stats), sizeof(stats)) < 0) {
		printf("netinfo: stats query failed\n\r");
		return;
	}
	printf("netinfo: stats rx=%u tx=%u arp=%u/%u ipv4=%u icmp=%u/%u udp=%u/%u tcp=%u/%u\n\r",
		(unsigned)stats.rx_frames, (unsigned)stats.tx_frames,
		(unsigned)stats.rx_arp, (unsigned)stats.tx_arp,
		(unsigned)stats.rx_ipv4,
		(unsigned)stats.rx_icmp, (unsigned)stats.tx_icmp,
		(unsigned)stats.rx_udp, (unsigned)stats.tx_udp,
		(unsigned)stats.rx_tcp, (unsigned)stats.tx_tcp);
	printf("netinfo: drops malformed=%u checksum=%u udp-no-port=%u udp-drop=%u icmp-unreach=%u/%u tcp-rst=%u/%u rexmit=%u conn-timeout=%u tw-expire=%u\n\r",
		(unsigned)stats.rx_malformed, (unsigned)stats.rx_checksum_error,
		(unsigned)stats.udp_no_port, (unsigned)stats.udp_drop,
		(unsigned)stats.icmp_unreachable_rx, (unsigned)stats.icmp_unreachable_tx,
		(unsigned)stats.tcp_rst_rx, (unsigned)stats.tcp_rst_tx,
		(unsigned)stats.tcp_retransmit, (unsigned)stats.tcp_connect_timeout,
		(unsigned)stats.tcp_timewait_expire);
	printf("netinfo: proto dhcp=%u/%u/%u/%u/%u tcp-accept=%u tcp-listen-close=%u tcp-connect-failed=%u\n\r",
		(unsigned)stats.dhcp_discover_tx, (unsigned)stats.dhcp_request_tx,
		(unsigned)stats.dhcp_offer_rx, (unsigned)stats.dhcp_ack_rx,
		(unsigned)stats.dhcp_nak_rx, (unsigned)stats.tcp_accept,
		(unsigned)stats.tcp_listener_close,
		(unsigned)stats.tcp_connect_failed);
	printf("netinfo: socket-errors icmp=%u rst=%u timeout=%u shutdown=%u\n\r",
		(unsigned)stats.socket_error_icmp, (unsigned)stats.socket_error_rst,
		(unsigned)stats.socket_error_timeout, (unsigned)stats.socket_error_shutdown);
	printf("netinfo: tcp-connect reasons refused=%u host=%u net=%u nobuf=%u addr=%u\n\r",
		(unsigned)stats.tcp_connect_refused,
		(unsigned)stats.tcp_connect_host_unreach,
		(unsigned)stats.tcp_connect_net_unreach,
		(unsigned)stats.tcp_connect_no_buffer,
		(unsigned)stats.tcp_connect_addr_in_use);
	printf("netinfo: tcp-connect summary failed=%u timeout=%u refused=%u unreachable=%u\n\r",
		(unsigned)stats.tcp_connect_failed,
		(unsigned)stats.tcp_connect_timeout,
		(unsigned)stats.tcp_connect_refused,
		(unsigned)(stats.tcp_connect_host_unreach + stats.tcp_connect_net_unreach));
	if (stats.tcp_connect_last_error) {
		printf("netinfo: tcp-connect recent err=%u/%s age=%u\n\r",
			(unsigned)stats.tcp_connect_last_error,
			mcca_net_socket_error_name(stats.tcp_connect_last_error),
			(unsigned)stats.tcp_connect_last_age);
	}
}

static void PrintNetworkConfig() {
	stduint count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4InterfaceCount),
		_IMM(&count), sizeof(count)) < 0) {
		printf("netinfo: config interface query failed\n\r");
		return;
	}
	printf("netinfo: config interfaces=%u\n\r", (unsigned)count);
	for0(i, count) {
		syscall_net_interface_ipv4_t iface{};
		if (QueryInterface(i, iface) < 0) {
			printf("netinfo: config if%u query failed\n\r", (unsigned)i);
			continue;
		}
		printf("netinfo: config if%u dev=%s ip=", (unsigned)i, iface.name[0] ? iface.name : "(none)");
		PrintIPv4(iface.address);
		printf(" mask=");
		PrintIPv4(iface.netmask);
		printf(" dns=");
		PrintIPv4(iface.dns);
		printf(" src=%s dhcp=%s lease=%u age=%u",
			ConfigSourceName(iface.config_source), DhcpStateName(iface.dhcp_state),
			(unsigned)iface.dhcp_lease_time, (unsigned)iface.dhcp_bound_age);
		if (iface.dns[0] || iface.dns[1] || iface.dns[2] || iface.dns[3]) {
			printf(" dns-source=%s", ConfigSourceName(iface.dns_source));
		}
		if (iface.gateway[0] || iface.gateway[1] || iface.gateway[2] || iface.gateway[3]) {
			printf(" route-source=%s", ConfigSourceName(iface.route_source));
		}
		if (iface.dhcp_server[0] || iface.dhcp_server[1] || iface.dhcp_server[2] || iface.dhcp_server[3]) {
			printf(" server=");
			PrintIPv4(iface.dhcp_server);
		}
		printf(" %s\n\r", (iface.flags & syscall_net_route_flag_up) ? "up" : "down");
	}
	uni::Network::NetworkConfigSnapshot config{};
	auto* config_interface = mcca_net_config();
	if (count && config_interface && config_interface->ReadConfig(config) > 0) {
		uint8 address[4] = {};
		uint8 netmask[4] = {};
		uint8 gateway[4] = {};
		uint8 dns[4] = {};
		uni::Network::IPv4WriteAddress(address, config.ipv4.address);
		uni::Network::IPv4WriteAddress(netmask, config.ipv4.netmask);
		uni::Network::IPv4WriteAddress(gateway, config.default_route.gateway);
		uni::Network::IPv4WriteAddress(dns, config.ipv4.dns);
		printf("netinfo: config snapshot if%u ip=", (unsigned)config.interface_index);
		PrintIPv4(address);
		printf(" mask=");
		PrintIPv4(netmask);
		printf(" gw=");
		PrintIPv4(gateway);
		printf(" dns=");
		PrintIPv4(dns);
		printf(" src=%s lease=%u %s\n\r", ConfigSourceName(config.ipv4.source),
			(unsigned)config.ipv4.lease_seconds,
			config.link_up ? "up" : "down");
	}
	PrintDefaultRoute();
}

static int PrintDnsLookup(const char* host) {
	struct in_addr address{};
	if (!mcca_net_resolve_ipv4(host, &address)) {
		printf("netinfo: dns host=%s status=%s\n\r", host, mcca_net_dns_status());
		mcca_net_print_dns_result("netinfo", host);
		return 1;
	}
	printf("netinfo: dns host=%s status=%s\n\r", host, mcca_net_dns_status());
	mcca_net_print_dns_result("netinfo", host);
	return 0;
}

static int PrintDns6Probe(const char* host) {
	stduint ttl = 0;
	const int ok = mcca_net_dns_probe_aaaa(host, &ttl);
	printf("netinfo: dns6 %s status=%s\n\r", host, mcca_net_dns_status());
	if (ok && ttl) printf("netinfo: dns6 ttl=%u\n\r", (unsigned)ttl);
	const stduint address_count = mcca_net_dns_ipv6_address_count();
	if (address_count) {
		printf("netinfo: dns6 addresses=%u\n\r", (unsigned)address_count);
		for0(a, address_count) {
			uint8 address[16]{};
			if (!mcca_net_dns_ipv6_address(a, address)) continue;
			printf("netinfo: dns6 AAAA%u=", (unsigned)a);
			for0(i, 8) {
				if (i) printf(":");
				printf("%x", (unsigned)((uint16(address[i * 2]) << 8) | address[i * 2 + 1]));
			}
			printf("\n\r");
		}
	}
	if (mcca_net_dns_answer_count()) {
		printf("netinfo: dns6 answers=%u\n\r", (unsigned)mcca_net_dns_answer_count());
	}
	if (mcca_net_dns_cname_count() || mcca_net_dns_non_a_count()) {
		printf("netinfo: dns6 cname=%u non-a=%u\n\r",
			(unsigned)mcca_net_dns_cname_count(), (unsigned)mcca_net_dns_non_a_count());
	}
	const char* cname = mcca_net_dns_cname_target();
	if (cname) {
		printf("netinfo: dns6 cname-target=%s\n\r", cname);
	}
	return ok ? 0 : 1;
}

static int PrintDnsCache(const char* host) {
	bool had_host = host && host[0];
	const stduint count = mcca_net_dns_cache_count();
	if (!count) {
		printf("netinfo: dns-cache %s\n\r", had_host ? "miss" : "empty");
		return 0;
	}
	stduint printed = 0;
	for0(i, count) {
		const char* cached_host = nullptr;
		struct in_addr address{};
		stduint ttl = 0;
		if (!mcca_net_dns_cache_entry(i, &cached_host, &address, &ttl)) continue;
		if (had_host && (!cached_host || StrCompare(cached_host, host))) continue;
		printed++;
	}
	if (!printed) {
		printf("netinfo: dns-cache %s\n\r", had_host ? "miss" : "empty");
		return 0;
	}
	printf("netinfo: dns-cache entries=%u\n\r", (unsigned)printed);
	stduint output_index = 0;
	for0(i, count) {
		const char* cached_host = nullptr;
		struct in_addr address{};
		stduint ttl = 0;
		if (!mcca_net_dns_cache_entry(i, &cached_host, &address, &ttl)) continue;
		if (had_host && (!cached_host || StrCompare(cached_host, host))) continue;
		const char* status = mcca_net_dns_cache_entry_status(i);
		const char* source = mcca_net_dns_cache_entry_source(i);
		const stduint age = mcca_net_dns_cache_entry_age(i);
		const stduint lifetime = ttl + age;
		const uint8* octet = (const uint8*)&address.s_addr;
		printf("netinfo: dns-cache%u host=%s src=%s status=%s A=%u.%u.%u.%u ttl=%u age=%u expire-in=%u\n\r",
			(unsigned)output_index, cached_host ? cached_host : "(none)",
			source ? source : "unknown",
			status ? status : "unknown",
			(unsigned)octet[0], (unsigned)octet[1], (unsigned)octet[2],
			(unsigned)octet[3], (unsigned)lifetime, (unsigned)age, (unsigned)ttl);
		printf("netinfo: dns-cache%u status-code=%d\n\r",
			(unsigned)output_index, mcca_net_dns_status_text_code(status));
		const char* cname = mcca_net_dns_cache_entry_cname_target(i);
		if (cname) printf("netinfo: dns-cache%u cname-target=%s\n\r", (unsigned)output_index, cname);
		const stduint address_count = mcca_net_dns_cache_entry_address_count(i);
		if (address_count > 1) {
			printf("netinfo: dns-cache%u addresses=%u\n\r", (unsigned)output_index, (unsigned)address_count);
			for0(j, address_count) {
				struct in_addr item{};
				if (!mcca_net_dns_cache_entry_address(i, j, &item)) continue;
				const uint8* item_octet = (const uint8*)&item.s_addr;
				printf("netinfo: dns-cache%u A%u=%u.%u.%u.%u\n\r",
					(unsigned)output_index, (unsigned)j,
					(unsigned)item_octet[0], (unsigned)item_octet[1],
					(unsigned)item_octet[2], (unsigned)item_octet[3]);
			}
		}
		output_index++;
	}
	return 0;
}

int main(int argc, char** argv) {
	if (argc >= 2 && (!StrCompare(argv[1], "-h") || !StrCompare(argv[1], "--help") ||
		!StrCompare(argv[1], "help"))) {
		PrintUsage();
		return 0;
	}
	if (argc == 2 && !StrCompare(argv[1], "--fault")) {
		return PrintFaultState();
	}
	if (argc == 2 && !StrCompare(argv[1], "--fault-off")) {
		syscall_net_fault_t fault{};
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::FaultSet),
			_IMM(&fault), sizeof(fault)) < 0) {
			printf("netinfo: fault disable failed\n\r");
			return 1;
		}
		printf("netinfo: fault disabled\n\r");
		return 0;
	}
	if (argc == 2 && !StrCompare(argv[1], "--fault-reset")) {
		if (stdsint(syscall(syscall_t::ROUT,
			stduint(syscall_net_route_func_t::FaultReset), 1, 0)) < 0) {
			printf("netinfo: fault reset failed\n\r");
			return 1;
		}
		printf("netinfo: fault counters reset\n\r");
		return PrintFaultState();
	}
	if (argc >= 2 && !StrCompare(argv[1], "--fault-set")) {
		const int result = SetFaultState(argc, argv);
		if (result < 0) {
			PrintUsage();
			return 1;
		}
		return result;
	}
	if (argc == 3 && !StrCompare(argv[1], "--dns")) {
		return PrintDnsLookup(argv[2]);
	}
	if (argc == 3 && !StrCompare(argv[1], "--dns6")) {
		return PrintDns6Probe(argv[2]);
	}
	if ((argc == 2 || argc == 3) && !StrCompare(argv[1], "--dns-cache")) {
		return PrintDnsCache(argc == 3 ? argv[2] : nullptr);
	}
	if ((argc == 2 || argc == 3) && !StrCompare(argv[1], "--dns-cache-clear")) {
		const char* host = argc == 3 ? argv[2] : nullptr;
		const int ok = host ? mcca_net_dns_cache_clear_host(host) : mcca_net_dns_cache_clear();
		if (!ok) {
			printf("netinfo: dns-cache clear failed\n\r");
			return 1;
		}
		if (host) printf("netinfo: dns-cache cleared host=%s\n\r", host);
		else printf("netinfo: dns-cache cleared\n\r");
		return 0;
	}
	if (argc >= 3 && argc <= 6 && !StrCompare(argv[1], "--dns-server")) {
		if (!StrCompare(argv[2], "none") || !StrCompare(argv[2], "clear")) {
			if (argc != 3) {
				PrintUsage();
				return 1;
			}
			if (!mcca_net_dns_set_server_ipv4(nullptr)) {
				printf("netinfo: dns-server clear failed\n\r");
				return 1;
			}
			printf("netinfo: dns-server cleared\n\r");
			return 0;
		}
		struct in_addr servers[4]{};
		const stduint server_count = stduint(argc - 2);
		for0(i, server_count) {
			uint32 address = 0;
			if (!mcca_net_parse_ipv4_host(argv[i + 2], &address)) {
				PrintUsage();
				return 1;
			}
			servers[i].s_addr = htonl(address);
		}
		if (!mcca_net_dns_set_servers_ipv4(servers, server_count)) {
			printf("netinfo: dns-server failed\n\r");
			return 1;
		}
		printf("netinfo: dns-servers=%u\n\r", (unsigned)server_count);
		for0(i, server_count) {
			printf("netinfo: dns-server%u=", (unsigned)i);
			PrintIPv4(reinterpret_cast<const uint8*>(&servers[i].s_addr));
			printf("\n\r");
		}
		return 0;
	}
	if (argc == 2 && !StrCompare(argv[1], "--dhcp-renew")) {
		if (!mcca_net_dhcp_renew()) {
			printf("netinfo: dhcp renew failed\n\r");
			return 1;
		}
		printf("netinfo: dhcp renew requested\n\r");
		return 0;
	}
	if (argc == 2 && !StrCompare(argv[1], "--dhcp-release")) {
		if (!mcca_net_dhcp_release()) {
			printf("netinfo: dhcp release failed\n\r");
			return 1;
		}
		printf("netinfo: dhcp released\n\r");
		return 0;
	}
	const bool view_all = argc == 1;
	const bool view_if = view_all || (argc == 2 && !StrCompare(argv[1], "--if"));
	const bool view_route = view_all || (argc == 2 && !StrCompare(argv[1], "--route"));
	const bool view_arp = view_all || (argc == 2 && !StrCompare(argv[1], "--arp"));
	const bool view_udp = view_all || (argc == 2 && !StrCompare(argv[1], "--udp"));
	const bool view_tcp = view_all || (argc == 2 && !StrCompare(argv[1], "--tcp"));
	const bool view_dhcp = view_all || (argc == 2 && !StrCompare(argv[1], "--dhcp"));
	const bool view_pending = view_all || (argc == 2 && !StrCompare(argv[1], "--pending"));
	const bool view_stats = view_all || (argc == 2 && !StrCompare(argv[1], "--stats"));
	const bool view_config = argc == 2 && !StrCompare(argv[1], "--config");
	if (!view_if && !view_route && !view_arp && !view_udp && !view_tcp && !view_dhcp &&
		!view_pending && !view_stats && !view_config) {
		PrintUsage();
		return 1;
	}

	if (view_if) {
		stduint count = 0;
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4InterfaceCount),
			_IMM(&count), sizeof(count)) < 0) {
			printf("netinfo: interface query failed\n\r");
			return 1;
		}

		printf("netinfo: interfaces=%u\n\r", (unsigned)count);
		for0(i, count) {
			syscall_net_interface_ipv4_t iface{};
			iface.link_index = uint16(i);
			if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4Interface),
				_IMM(&iface), sizeof(iface)) < 0) {
				printf("netinfo: if%u query failed\n\r", (unsigned)i);
				continue;
			}
			printf("netinfo: if%u dev=%s ip=", (unsigned)i, iface.name[0] ? iface.name : "(none)");
			PrintIPv4(iface.address);
			printf(" mask=");
			PrintIPv4(iface.netmask);
			printf(" mac=");
			PrintMac(iface.hardware);
			printf(" mtu=%u src=%s dhcp=%s", (unsigned)iface.mtu,
				ConfigSourceName(iface.config_source),
				DhcpStateName(iface.dhcp_state));
			if (iface.dhcp_xid) printf(" xid=%[32H]", (stduint)iface.dhcp_xid);
			if (iface.dhcp_retry_count) printf(" retry=%u", (unsigned)iface.dhcp_retry_count);
			if (iface.dhcp_lease_time) printf(" lease=%u", (unsigned)iface.dhcp_lease_time);
			if (iface.dhcp_bound_age) printf(" age=%u", (unsigned)iface.dhcp_bound_age);
			if (iface.dhcp_server[0] || iface.dhcp_server[1] || iface.dhcp_server[2] || iface.dhcp_server[3]) {
				printf(" server=");
				PrintIPv4(iface.dhcp_server);
			}
			if (iface.dns[0] || iface.dns[1] || iface.dns[2] || iface.dns[3]) {
				printf(" dns=");
				PrintIPv4(iface.dns);
			}
			printf(" %s\n\r",
				(iface.flags & syscall_net_route_flag_up) ? "up" : "down");
		}
	}
	if (view_route) PrintDefaultRoute();
	if (view_arp) PrintArpCache();
	if (view_udp) PrintUdpState();
	if (view_tcp) PrintTcpState();
	if (view_dhcp) PrintDhcpState();
	if (view_pending) {
		printf("netinfo: pending arp\n\r");
		PrintArpCache();
		printf("netinfo: pending udp\n\r");
		PrintUdpPendingOnly();
		printf("netinfo: pending tcp\n\r");
		PrintTcpConnectingOnly();
	}
	if (view_stats) PrintNetStats();
	if (view_config) PrintNetworkConfig();
	return 0;
}
