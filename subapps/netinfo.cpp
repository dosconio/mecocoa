#include "aaaaa.h"
#include <stdio.h>

static void PrintUsage() {
	printf("usage: netinfo [help]\n\r");
	printf("  list IPv4 network interfaces\n\r");
	printf("  fields: index device ip mask mac mtu state dhcp dns lease\n\r");
	printf("  also prints the default IPv4 route\n\r");
	printf("  also prints IPv4 ARP cache entries\n\r");
	printf("  also prints TCP listener and connection state\n\r");
	printf("  also prints network packet counters\n\r");
}

static void PrintIPv4(const uint8 address[4]) {
	printf("%u.%u.%u.%u", (unsigned)address[0], (unsigned)address[1],
		(unsigned)address[2], (unsigned)address[3]);
}

static void PrintMac(const uint8 address[6]) {
	printf("%[8H]:%[8H]:%[8H]:%[8H]:%[8H]:%[8H]",
		(stduint)address[0], (stduint)address[1], (stduint)address[2],
		(stduint)address[3], (stduint)address[4], (stduint)address[5]);
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
	default: return "unknown";
	}
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
		printf("netinfo: tcp-listen%u port=%u backlog=%u pending=%u %s\n\r",
			(unsigned)i, (unsigned)listener.port, (unsigned)listener.backlog,
			(unsigned)listener.pending,
			(listener.flags & syscall_net_route_flag_up) ? "up" : "down");
	}

	stduint connection_count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::TCPConnectionCount),
		_IMM(&connection_count), sizeof(connection_count)) < 0) {
		printf("netinfo: tcp connection query failed\n\r");
		return;
	}
	printf("netinfo: tcp connections=%u\n\r", (unsigned)connection_count);
	for0(i, connection_count) {
		syscall_net_tcp_connection_t connection{};
		connection.entry_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::TCPConnectionEntry),
			_IMM(&connection), sizeof(connection)) < 0) {
			printf("netinfo: tcp connection%u query failed\n\r", (unsigned)i);
			continue;
		}
		printf("netinfo: tcp%u local=", (unsigned)i);
		PrintIPv4(connection.local_address);
		printf(":%u peer=", (unsigned)connection.local_port);
		PrintIPv4(connection.remote_address);
		printf(":%u state=%s rx=%u win=%u tx=%u retry=%u rexmit=%u mss=%u/%u send=%u dup=%u ooo=%u full=%u",
			(unsigned)connection.remote_port, TcpStateName(connection.state),
			(unsigned)connection.rx_bytes, (unsigned)connection.rx_window,
			(unsigned)connection.tx_pending, (unsigned)connection.tx_retry_count,
			(unsigned)connection.tx_retransmit,
			(unsigned)connection.local_mss, (unsigned)connection.peer_mss,
			(unsigned)connection.send_mss,
			(unsigned)connection.rx_duplicate, (unsigned)connection.rx_out_of_order,
			(unsigned)connection.rx_window_full);
		if (connection.time_wait_age || connection.time_wait_remaining) {
			printf(" tw-age=%u tw-left=%u",
				(unsigned)connection.time_wait_age, (unsigned)connection.time_wait_remaining);
		}
		printf("%s%s\n\r",
			(connection.flags & 0x0100u) ? " active" : " passive",
			(connection.flags & 0x0800u) ? " reset" :
				((connection.flags & 0x0400u) ? " tx-exhausted" : ""));
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
}

int main(int argc, char** argv) {
	if (argc >= 2 && (!StrCompare(argv[1], "-h") || !StrCompare(argv[1], "--help") ||
		!StrCompare(argv[1], "help"))) {
		PrintUsage();
		return 0;
	}

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
	PrintDefaultRoute();
	PrintArpCache();
	PrintTcpState();
	PrintNetStats();
	return 0;
}
