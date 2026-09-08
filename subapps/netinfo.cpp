#include "aaaaa.h"
#include <stdio.h>

static void PrintUsage() {
	printf("usage: netinfo [help]\n\r");
	printf("  list IPv4 network interfaces\n\r");
	printf("  fields: index device ip mask mac mtu state\n\r");
	printf("  also prints the default IPv4 route\n\r");
	printf("  also prints IPv4 ARP cache entries\n\r");
	printf("  also prints TCP listener and connection state\n\r");
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
		printf(":%u state=%s rx=%u tx=%u retry=%u%s%s\n\r",
			(unsigned)connection.remote_port, TcpStateName(connection.state),
			(unsigned)connection.rx_bytes, (unsigned)connection.tx_pending,
			(unsigned)connection.tx_retry_count,
			(connection.flags & 0x0100u) ? " active" : " passive",
			(connection.flags & 0x0400u) ? " tx-exhausted" : "");
	}
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
		printf(" mtu=%u %s\n\r", (unsigned)iface.mtu,
			(iface.flags & syscall_net_route_flag_up) ? "up" : "down");
	}
	PrintDefaultRoute();
	PrintArpCache();
	PrintTcpState();
	return 0;
}
