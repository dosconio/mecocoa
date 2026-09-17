#include "aaaaa.h"
#include "lib_network.hpp"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static uint16 MccaReadNet16(const uint8* data) {
	return uint16((uint16(data[0]) << 8) | data[1]);
}

static void MccaWriteNet16(uint8* data, uint16 value) {
	data[0] = uint8(value >> 8);
	data[1] = uint8(value);
}

static char mcca_dns_cache_host[64] = {};
static struct in_addr mcca_dns_cache_address = {};
static stduint mcca_dns_cache_expire_second = 0;
static stduint mcca_dns_cache_answer_count = 0;
static const char* mcca_dns_last_status = "none";
static stduint mcca_dns_last_ttl = 0;
static stduint mcca_dns_last_answer_count = 0;

static bool MccaDnsCacheMatch(const char* host) {
	return host && mcca_dns_cache_host[0] && !strcmp(mcca_dns_cache_host, host);
}

static void MccaForgetDnsCache() {
	mcca_dns_cache_host[0] = 0;
	mcca_dns_cache_expire_second = 0;
	mcca_dns_cache_answer_count = 0;
}

static void MccaRememberDnsCache(const char* host, const struct in_addr* address, stduint ttl, stduint answer_count) {
	if (!host || !address || !ttl || strlen(host) >= sizeof(mcca_dns_cache_host)) return;
	strncpy(mcca_dns_cache_host, host, sizeof(mcca_dns_cache_host) - 1);
	mcca_dns_cache_host[sizeof(mcca_dns_cache_host) - 1] = 0;
	mcca_dns_cache_address = *address;
	mcca_dns_cache_expire_second = syssecond() + ttl;
	mcca_dns_cache_answer_count = answer_count;
}

static int MccaResolveDnsFail(const char* host, const char* status) {
	(void)host;
	mcca_dns_last_status = status ? status : "unknown";
	mcca_dns_last_ttl = 0;
	mcca_dns_last_answer_count = 0;
	return 0;
}

static const char* MccaDnsRcodeStatus(uint16 rcode) {
	switch (rcode) {
	case 0: return "ok";
	case 1: return "format-error";
	case 2: return "servfail";
	case 3: return "nxdomain";
	case 4: return "not-implemented";
	case 5: return "refused";
	default: return "dns-error";
	}
}

static uint32 MccaReadNet32(const uint8* data) {
	return (uint32(data[0]) << 24) | (uint32(data[1]) << 16) |
		(uint32(data[2]) << 8) | uint32(data[3]);
}

static bool MccaGetConfiguredDnsServer(struct in_addr* output) {
	if (!output) return false;
	stduint count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4InterfaceCount),
		_IMM(&count), sizeof(count)) < 0) return false;
	for0(i, count) {
		syscall_net_interface_ipv4_t iface{};
		iface.link_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4Interface),
			_IMM(&iface), sizeof(iface)) < 0) continue;
		if (!iface.dns[0] && !iface.dns[1] && !iface.dns[2] && !iface.dns[3]) continue;
		uint8* target = (uint8*)&output->s_addr;
		for0(j, 4) target[j] = iface.dns[j];
		return true;
	}
	return false;
}

static bool MccaEncodeDnsName(uint8* output, stduint capacity, const char* host, stduint* length) {
	if (!output || !host || !length) return false;
	stduint out = 0;
	const char* label = host;
	while (*label) {
		const char* cursor = label;
		stduint label_length = 0;
		while (*cursor && *cursor != '.') {
			label_length++;
			cursor++;
		}
		if (!label_length || label_length > 63 || out + 1 + label_length >= capacity) return false;
		output[out++] = uint8(label_length);
		for0(i, label_length) output[out++] = uint8(label[i]);
		label = *cursor == '.' ? cursor + 1 : cursor;
	}
	if (out >= capacity) return false;
	output[out++] = 0;
	*length = out;
	return true;
}

static bool MccaSkipDnsName(const uint8* packet, stduint packet_length, stduint* offset) {
	if (!packet || !offset) return false;
	stduint cursor = *offset;
	for0(depth, 32) {
		if (cursor >= packet_length) return false;
		const uint8 label = packet[cursor++];
		if (!label) {
			*offset = cursor;
			return true;
		}
		if ((label & 0xC0u) == 0xC0u) {
			if (cursor >= packet_length) return false;
			*offset = cursor + 1;
			return true;
		}
		if (label & 0xC0u) return false;
		cursor += label;
		if (cursor > packet_length) return false;
	}
	return false;
}

extern "C" const char* mcca_net_dns_status() {
	return mcca_dns_last_status;
}

extern "C" stduint mcca_net_dns_ttl() {
	return mcca_dns_last_ttl;
}

extern "C" stduint mcca_net_dns_answer_count() {
	return mcca_dns_last_answer_count;
}

extern "C" const char* mcca_net_socket_error_name(int error) {
	switch (error) {
	case 0: return "none";
	case 32: return "broken-pipe";
	case 104: return "reset";
	case 111: return "refused";
	case 114: return "net-unreach";
	case 116: return "timeout";
	case 118: return "host-unreach";
	case 121: return "dest-required";
	default: return "error";
	}
}

extern "C" int mcca_net_get_socket_error(int fd) {
	int value = 0;
	socklen_t length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &value, &length) < 0) return -1;
	return value;
}

extern "C" int mcca_net_format_ipv4(char* output, stduint capacity, const uint8 address[4]) {
	if (!output || !capacity || !address) return 0;
	const int written = snprintf(output, capacity, "%u.%u.%u.%u",
		(unsigned)address[0], (unsigned)address[1], (unsigned)address[2], (unsigned)address[3]);
	if (written < 0 || stduint(written) >= capacity) {
		output[0] = 0;
		return 0;
	}
	return 1;
}

extern "C" int mcca_net_format_sockaddr_ipv4(char* output, stduint capacity, const struct sockaddr_in* address) {
	if (!output || !capacity || !address) return 0;
	const uint8* octet = (const uint8*)&address->sin_addr.s_addr;
	const int written = snprintf(output, capacity, "%u.%u.%u.%u:%u",
		(unsigned)octet[0], (unsigned)octet[1], (unsigned)octet[2], (unsigned)octet[3],
		(unsigned)ntohs(address->sin_port));
	if (written < 0 || stduint(written) >= capacity) {
		output[0] = 0;
		return 0;
	}
	return 1;
}

extern "C" int mcca_net_resolve_ipv4(const char* host, struct in_addr* output) {
	if (!host || !output) return MccaResolveDnsFail(host, "bad-argument");
	if (MccaDnsCacheMatch(host)) {
		const stduint now = syssecond();
		if (mcca_dns_cache_expire_second && now < mcca_dns_cache_expire_second) {
			mcca_dns_last_status = "cached";
			mcca_dns_last_ttl = mcca_dns_cache_expire_second - now;
			mcca_dns_last_answer_count = mcca_dns_cache_answer_count;
			*output = mcca_dns_cache_address;
			return 1;
		}
		MccaForgetDnsCache();
		mcca_dns_last_status = "expired";
		mcca_dns_last_ttl = 0;
		mcca_dns_last_answer_count = 0;
	}
	if (inet_aton(host, output)) {
		mcca_dns_last_status = "numeric";
		mcca_dns_last_ttl = 0;
		mcca_dns_last_answer_count = 1;
		return 1;
	}
	struct in_addr dns{};
	if (!MccaGetConfiguredDnsServer(&dns)) {
		return MccaResolveDnsFail(host, "no-dns-server");
	}

	uint8 query[256] = {};
	static uint16 next_id = 0x4D4Eu;
	const uint16 query_id = next_id++;
	MccaWriteNet16(query + 0, query_id);
	MccaWriteNet16(query + 2, 0x0100u);
	MccaWriteNet16(query + 4, 1);
	stduint name_length = 0;
	if (!MccaEncodeDnsName(query + 12, sizeof(query) - 16, host, &name_length)) {
		return MccaResolveDnsFail(host, "bad-name");
	}
	stduint query_length = 12 + name_length;
	MccaWriteNet16(query + query_length, 1);
	MccaWriteNet16(query + query_length + 2, 1);
	query_length += 4;

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) return MccaResolveDnsFail(host, "socket");
	struct sockaddr_in server{};
	server.sin_family = AF_INET;
	server.sin_port = htons(53);
	server.sin_addr = dns;
	const stdsint sent = sendto(fd, query, query_length, 0,
		(const struct sockaddr*)&server, sizeof(server));
	if (sent != stdsint(query_length)) {
		close(fd);
		return MccaResolveDnsFail(host, "send");
	}
	struct pollfd pfd{};
	pfd.fd = fd;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 2500) <= 0 || !(pfd.revents & POLLIN)) {
		close(fd);
		return MccaResolveDnsFail(host, "timeout");
	}
	uint8 response[512] = {};
	const stdsint received = recvfrom(fd, response, sizeof(response), 0, nullptr, nullptr);
	close(fd);
	if (received < 12) return MccaResolveDnsFail(host, "short-reply");
	const stduint response_length = stduint(received);
	if (MccaReadNet16(response) != query_id) return MccaResolveDnsFail(host, "bad-xid");
	const uint16 dns_flags = MccaReadNet16(response + 2);
	const uint16 dns_rcode = dns_flags & 0x000Fu;
	if (!(dns_flags & 0x8000u)) return MccaResolveDnsFail(host, "not-response");
	if (dns_rcode != 0) return MccaResolveDnsFail(host, MccaDnsRcodeStatus(dns_rcode));
	const uint16 question_count = MccaReadNet16(response + 4);
	const uint16 answer_count = MccaReadNet16(response + 6);
	mcca_dns_last_answer_count = answer_count;
	stduint offset = 12;
	for0(i, question_count) {
		if (!MccaSkipDnsName(response, response_length, &offset)) return MccaResolveDnsFail(host, "bad-question");
		if (offset + 4 > response_length) return MccaResolveDnsFail(host, "bad-question");
		offset += 4;
	}
	for0(i, answer_count) {
		if (!MccaSkipDnsName(response, response_length, &offset)) return MccaResolveDnsFail(host, "bad-answer");
		if (offset + 10 > response_length) return MccaResolveDnsFail(host, "bad-answer");
		const uint16 type = MccaReadNet16(response + offset);
		const uint16 dns_class = MccaReadNet16(response + offset + 2);
		uint32 ttl = MccaReadNet32(response + offset + 4);
		const uint16 rdlength = MccaReadNet16(response + offset + 8);
		offset += 10;
		if (offset + rdlength > response_length) return MccaResolveDnsFail(host, "bad-rdata");
		if (type == 1 && dns_class == 1 && rdlength == 4) {
			uint8* target = (uint8*)&output->s_addr;
			for0(j, 4) target[j] = response[offset + j];
			mcca_dns_last_status = "ok";
			if (ttl > 86400) ttl = 86400;
			mcca_dns_last_ttl = stduint(ttl);
			MccaRememberDnsCache(host, output, mcca_dns_last_ttl, mcca_dns_last_answer_count);
			return 1;
		}
		offset += rdlength;
	}
	return MccaResolveDnsFail(host, "no-a-record");
}
