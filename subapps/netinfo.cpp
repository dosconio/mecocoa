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
	printf("  dns clear: netinfo --dns-cache-clear\n\r");
	printf("  dhcp: netinfo --dhcp-renew | --dhcp-release\n\r");
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
		printf("netinfo: udp%u port=%u queued=%u drops=%u waiters=%u inbox=%u %s%s\n\r",
			(unsigned)i, (unsigned)inbox.port, (unsigned)inbox.queued,
			(unsigned)inbox.drops, (unsigned)inbox.waiters, (unsigned)inbox.inbox_id,
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
		printf(" src=%u dst=%u len=%u arp=%u age=%u\n\r",
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
		printf(" src=%u dst=%u len=%u arp=%u age=%u\n\r",
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
	stduint count_connecting = 0;
	stduint count_established = 0;
	stduint count_close_wait = 0;
	stduint count_closing = 0;
	stduint count_time_wait = 0;
	stduint count_reset = 0;
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
		else if (connection.state == 4 || connection.state == 5 || connection.state == 6) count_closing++;
		else if (connection.state == 7) count_time_wait++;
		else if (connection.state == 8) count_reset++;
		printf("netinfo: tcp%u local=", (unsigned)i);
		PrintIPv4(connection.local_address);
		printf(":%u peer=", (unsigned)connection.local_port);
		PrintIPv4(connection.remote_address);
		printf(":%u state=%s phase=%s dir=%s err=%u/%s rx=%u win=%u peerwin=%u tx=%u retry=%u rexmit=%u mss=%u/%u send=%u dup=%u ooo=%u full=%u idle=%u/%u",
			(unsigned)connection.remote_port, TcpStateDisplayName(connection),
			TcpClosePhaseName(connection.close_phase), TcpDirectionName(connection.flags),
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
		if (connection.flags & 0x0200u) printf(" fin-ack");
		if (connection.flags & 0x0400u) printf(" tx-exhausted");
		if (connection.flags & 0x0800u) printf(" reset");
		printf("\n\r");
	}
	if (connection_count) {
		printf("netinfo: tcp summary connecting=%u established=%u close-wait=%u closing=%u time-wait=%u reset=%u\n\r",
			(unsigned)count_connecting, (unsigned)count_established,
			(unsigned)count_close_wait, (unsigned)count_closing,
			(unsigned)count_time_wait, (unsigned)count_reset);
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
		if (iface.dhcp_server[0] || iface.dhcp_server[1] || iface.dhcp_server[2] || iface.dhcp_server[3]) {
			printf(" server=");
			PrintIPv4(iface.dhcp_server);
		}
		printf(" %s\n\r", (iface.flags & syscall_net_route_flag_up) ? "up" : "down");
	}
	PrintDefaultRoute();
}

static int PrintDnsLookup(const char* host) {
	struct in_addr address{};
	if (!mcca_net_resolve_ipv4(host, &address)) {
		printf("netinfo: dns failed: %s\n\r", mcca_net_dns_status());
		printf("netinfo: dns status-code=%d\n\r", mcca_net_dns_status_code());
		return 1;
	}
	const uint8* octet = (const uint8*)&address.s_addr;
	printf("netinfo: dns %s A=%u.%u.%u.%u status=%s\n\r", host,
		(unsigned)octet[0], (unsigned)octet[1], (unsigned)octet[2], (unsigned)octet[3],
		mcca_net_dns_status());
	printf("netinfo: dns status-code=%d\n\r", mcca_net_dns_status_code());
	if (mcca_net_dns_ttl()) printf("netinfo: dns ttl=%u\n\r", (unsigned)mcca_net_dns_ttl());
	if (mcca_net_dns_answer_count()) {
		printf("netinfo: dns answers=%u\n\r", (unsigned)mcca_net_dns_answer_count());
	}
	if (mcca_net_dns_address_count() > 1) {
		printf("netinfo: dns addresses=%u\n\r", (unsigned)mcca_net_dns_address_count());
		for0(i, mcca_net_dns_address_count()) {
			struct in_addr item{};
			if (!mcca_net_dns_address(i, &item)) continue;
			const uint8* item_octet = (const uint8*)&item.s_addr;
			printf("netinfo: dns A%u=%u.%u.%u.%u\n\r", (unsigned)i,
				(unsigned)item_octet[0], (unsigned)item_octet[1],
				(unsigned)item_octet[2], (unsigned)item_octet[3]);
		}
	}
	if (mcca_net_dns_cname_count() || mcca_net_dns_non_a_count()) {
		printf("netinfo: dns cname=%u non-a=%u\n\r",
			(unsigned)mcca_net_dns_cname_count(), (unsigned)mcca_net_dns_non_a_count());
	}
	return 0;
}

static int PrintDns6Probe(const char* host) {
	stduint ttl = 0;
	const int ok = mcca_net_dns_probe_aaaa(host, &ttl);
	printf("netinfo: dns6 %s status=%s\n\r", host, mcca_net_dns_status());
	if (ok && ttl) printf("netinfo: dns6 ttl=%u\n\r", (unsigned)ttl);
	if (mcca_net_dns_answer_count()) {
		printf("netinfo: dns6 answers=%u\n\r", (unsigned)mcca_net_dns_answer_count());
	}
	if (mcca_net_dns_cname_count() || mcca_net_dns_non_a_count()) {
		printf("netinfo: dns6 cname=%u non-a=%u\n\r",
			(unsigned)mcca_net_dns_cname_count(), (unsigned)mcca_net_dns_non_a_count());
	}
	return ok ? 0 : 1;
}

static int PrintDnsCache(const char* host) {
	if (host) {
		struct in_addr resolved{};
		if (!mcca_net_resolve_ipv4(host, &resolved)) {
			printf("netinfo: dns-cache resolve failed: %s\n\r", mcca_net_dns_status());
		}
	}
	const stduint count = mcca_net_dns_cache_count();
	if (!count) {
		printf("netinfo: dns-cache empty\n\r");
		return 0;
	}
	printf("netinfo: dns-cache entries=%u\n\r", (unsigned)count);
	for0(i, count) {
		const char* cached_host = nullptr;
		struct in_addr address{};
		stduint ttl = 0;
		if (!mcca_net_dns_cache_entry(i, &cached_host, &address, &ttl)) continue;
		const char* status = mcca_net_dns_cache_entry_status(i);
		const uint8* octet = (const uint8*)&address.s_addr;
		printf("netinfo: dns-cache%u host=%s status=%s A=%u.%u.%u.%u ttl=%u\n\r",
			(unsigned)i, cached_host ? cached_host : "(none)",
			status ? status : "unknown",
			(unsigned)octet[0], (unsigned)octet[1], (unsigned)octet[2],
			(unsigned)octet[3], (unsigned)ttl);
		printf("netinfo: dns-cache%u status-code=%d\n\r",
			(unsigned)i, mcca_net_dns_status_text_code(status));
		const stduint address_count = mcca_net_dns_cache_entry_address_count(i);
		if (address_count > 1) {
			printf("netinfo: dns-cache%u addresses=%u\n\r", (unsigned)i, (unsigned)address_count);
			for0(j, address_count) {
				struct in_addr item{};
				if (!mcca_net_dns_cache_entry_address(i, j, &item)) continue;
				const uint8* item_octet = (const uint8*)&item.s_addr;
				printf("netinfo: dns-cache%u A%u=%u.%u.%u.%u\n\r",
					(unsigned)i, (unsigned)j,
					(unsigned)item_octet[0], (unsigned)item_octet[1],
					(unsigned)item_octet[2], (unsigned)item_octet[3]);
			}
		}
	}
	return 0;
}

int main(int argc, char** argv) {
	if (argc >= 2 && (!StrCompare(argv[1], "-h") || !StrCompare(argv[1], "--help") ||
		!StrCompare(argv[1], "help"))) {
		PrintUsage();
		return 0;
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
	if (argc == 2 && !StrCompare(argv[1], "--dns-cache-clear")) {
		if (!mcca_net_dns_cache_clear()) {
			printf("netinfo: dns-cache clear failed\n\r");
			return 1;
		}
		printf("netinfo: dns-cache cleared\n\r");
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
		PrintArpCache();
		PrintUdpPendingOnly();
		PrintTcpConnectingOnly();
	}
	if (view_stats) PrintNetStats();
	if (view_config) PrintNetworkConfig();
	return 0;
}
