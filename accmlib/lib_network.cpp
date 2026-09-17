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

constexpr stduint MccaDnsCacheCapacity = 4;
constexpr stduint MccaDnsResolveDepthLimit = 4;

struct MccaDnsCacheEntry {
	char host[64];
	struct in_addr address;
	stduint expire_second;
	stduint answer_count;
};

static MccaDnsCacheEntry mcca_dns_cache[MccaDnsCacheCapacity]{};
static stduint mcca_dns_cache_next = 0;
static stduint mcca_dns_cache_last = stduint(-1);
static const char* mcca_dns_last_status = "none";
static stduint mcca_dns_last_ttl = 0;
static stduint mcca_dns_last_answer_count = 0;
static stduint mcca_dns_last_cname_count = 0;
static stduint mcca_dns_last_non_a_count = 0;

static MccaDnsCacheEntry* MccaDnsCacheFind(const char* host) {
	if (!host) return nullptr;
	const stduint now = syssecond();
	for0(i, MccaDnsCacheCapacity) {
		auto& entry = mcca_dns_cache[i];
		if (!entry.host[0]) continue;
		if (!entry.expire_second || now >= entry.expire_second) {
			entry.host[0] = 0;
			entry.expire_second = 0;
			continue;
		}
		if (!strcmp(entry.host, host)) {
			mcca_dns_cache_last = i;
			return &entry;
		}
	}
	return nullptr;
}

static stduint MccaDnsCacheEntryTtl(const MccaDnsCacheEntry& entry) {
	if (!entry.host[0] || !entry.expire_second) return 0;
	const stduint now = syssecond();
	return now < entry.expire_second ? (entry.expire_second - now) : 0;
}

static void MccaRememberDnsCache(const char* host, const struct in_addr* address, stduint ttl, stduint answer_count) {
	if (!host || !address || !ttl || strlen(host) >= sizeof(mcca_dns_cache[0].host)) return;
	MccaDnsCacheEntry* entry = MccaDnsCacheFind(host);
	if (!entry) {
		entry = &mcca_dns_cache[mcca_dns_cache_next];
		mcca_dns_cache_last = mcca_dns_cache_next;
		mcca_dns_cache_next = (mcca_dns_cache_next + 1) % MccaDnsCacheCapacity;
	}
	strncpy(entry->host, host, sizeof(entry->host) - 1);
	entry->host[sizeof(entry->host) - 1] = 0;
	entry->address = *address;
	entry->expire_second = syssecond() + ttl;
	entry->answer_count = answer_count;
}

static int MccaResolveDnsFail(const char* host, const char* status) {
	(void)host;
	mcca_dns_last_status = status ? status : "unknown";
	mcca_dns_last_ttl = 0;
	mcca_dns_last_answer_count = 0;
	mcca_dns_last_cname_count = 0;
	mcca_dns_last_non_a_count = 0;
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

static bool MccaReadDnsName(const uint8* packet, stduint packet_length, stduint* offset,
	char* output, stduint capacity) {
	if (!packet || !offset || !output || !capacity) return false;
	stduint cursor = *offset;
	stduint out = 0;
	bool jumped = false;
	stduint next_offset = cursor;
	for0(depth, 32) {
		if (cursor >= packet_length) return false;
		const uint8 label = packet[cursor++];
		if (!label) {
			if (!jumped) next_offset = cursor;
			if (!out) {
				if (capacity < 2) return false;
				output[out++] = '.';
			}
			output[out] = 0;
			*offset = next_offset;
			return true;
		}
		if ((label & 0xC0u) == 0xC0u) {
			if (cursor >= packet_length) return false;
			const stduint pointer = (stduint(label & 0x3Fu) << 8) | packet[cursor++];
			if (!jumped) next_offset = cursor;
			cursor = pointer;
			jumped = true;
			continue;
		}
		if (label & 0xC0u) return false;
		if (cursor + label > packet_length) return false;
		if (out) {
			if (out + 1 >= capacity) return false;
			output[out++] = '.';
		}
		if (out + label >= capacity) return false;
		for0(i, label) output[out++] = char(packet[cursor++]);
		if (!jumped) next_offset = cursor;
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

extern "C" stduint mcca_net_dns_cname_count() {
	return mcca_dns_last_cname_count;
}

extern "C" stduint mcca_net_dns_non_a_count() {
	return mcca_dns_last_non_a_count;
}

extern "C" stduint mcca_net_dns_cache_count() {
	stduint count = 0;
	for0(i, MccaDnsCacheCapacity) if (MccaDnsCacheEntryTtl(mcca_dns_cache[i])) count++;
	return count;
}

extern "C" int mcca_net_dns_cache_entry(stduint index, const char** host, struct in_addr* output, stduint* ttl) {
	stduint ordinal = 0;
	for0(i, MccaDnsCacheCapacity) {
		auto& entry = mcca_dns_cache[i];
		const stduint entry_ttl = MccaDnsCacheEntryTtl(entry);
		if (!entry_ttl) continue;
		if (ordinal++ != index) continue;
		if (host) *host = entry.host;
		if (output) *output = entry.address;
		if (ttl) *ttl = entry_ttl;
		return 1;
	}
	return 0;
}

extern "C" const char* mcca_net_dns_cache_host() {
	if (mcca_dns_cache_last >= MccaDnsCacheCapacity) return nullptr;
	const auto& entry = mcca_dns_cache[mcca_dns_cache_last];
	return MccaDnsCacheEntryTtl(entry) ? entry.host : nullptr;
}

extern "C" stduint mcca_net_dns_cache_ttl() {
	if (mcca_dns_cache_last >= MccaDnsCacheCapacity) return 0;
	return MccaDnsCacheEntryTtl(mcca_dns_cache[mcca_dns_cache_last]);
}

extern "C" int mcca_net_dns_cache_address(struct in_addr* output) {
	if (!output || mcca_dns_cache_last >= MccaDnsCacheCapacity ||
		!MccaDnsCacheEntryTtl(mcca_dns_cache[mcca_dns_cache_last])) return 0;
	*output = mcca_dns_cache[mcca_dns_cache_last].address;
	return 1;
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

extern "C" int mcca_net_parse_ipv4(const char* text, uint8 output[4]) {
	if (!text || !output) return 0;
	const char* cursor = text;
	for0(i, 4) {
		if (*cursor < '0' || *cursor > '9') return 0;
		int value = 0;
		while (*cursor >= '0' && *cursor <= '9') {
			value = value * 10 + (*cursor - '0');
			if (value > 255) return 0;
			cursor++;
		}
		output[i] = uint8(value);
		if (i < 3) {
			if (*cursor != '.') return 0;
			cursor++;
		}
	}
	return *cursor ? 0 : 1;
}

extern "C" int mcca_net_parse_ipv4_host(const char* text, uint32* output) {
	uint8 octet[4] = {};
	if (!output || !mcca_net_parse_ipv4(text, octet)) return 0;
	*output = (uint32(octet[0]) << 24) | (uint32(octet[1]) << 16) |
		(uint32(octet[2]) << 8) | uint32(octet[3]);
	return 1;
}

extern "C" int mcca_net_parse_port(const char* text, uint16* output) {
	if (!text || !output || !*text) return 0;
	uint32 value = 0;
	for (const char* cursor = text; *cursor; cursor++) {
		if (*cursor < '0' || *cursor > '9') return 0;
		value = value * 10 + uint32(*cursor - '0');
		if (value > 65535u) return 0;
	}
	if (!value) return 0;
	*output = uint16(value);
	return 1;
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

static int MccaResolveIPv4Query(const char* host, struct in_addr* output, stduint depth) {
	if (!host || !output) return MccaResolveDnsFail(host, "bad-argument");
	if (depth > MccaDnsResolveDepthLimit) return MccaResolveDnsFail(host, "cname-loop");
	if (auto* entry = MccaDnsCacheFind(host)) {
		mcca_dns_last_status = "cached";
		mcca_dns_last_ttl = MccaDnsCacheEntryTtl(*entry);
		mcca_dns_last_answer_count = entry->answer_count;
		mcca_dns_last_cname_count = 0;
		mcca_dns_last_non_a_count = 0;
		*output = entry->address;
		return 1;
	}
	if (inet_aton(host, output)) {
		mcca_dns_last_status = "numeric";
		mcca_dns_last_ttl = 0;
		mcca_dns_last_answer_count = 1;
		mcca_dns_last_cname_count = 0;
		mcca_dns_last_non_a_count = 0;
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
	mcca_dns_last_cname_count = 0;
	mcca_dns_last_non_a_count = 0;
	stduint offset = 12;
	for0(i, question_count) {
		if (!MccaSkipDnsName(response, response_length, &offset)) return MccaResolveDnsFail(host, "bad-question");
		if (offset + 4 > response_length) return MccaResolveDnsFail(host, "bad-question");
		offset += 4;
	}
	bool has_a_record = false;
	struct in_addr first_address{};
	stduint first_ttl = 0;
	char first_cname[64] = {};
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
			if (!has_a_record) {
				uint8* target = (uint8*)&first_address.s_addr;
				for0(j, 4) target[j] = response[offset + j];
				if (ttl > 86400) ttl = 86400;
				first_ttl = stduint(ttl);
				has_a_record = true;
			}
		}
		else if (type == 5 && dns_class == 1) {
			mcca_dns_last_cname_count++;
			if (!first_cname[0]) {
				stduint cname_offset = offset;
				(void)MccaReadDnsName(response, response_length, &cname_offset,
					first_cname, sizeof(first_cname));
			}
		}
		else mcca_dns_last_non_a_count++;
		offset += rdlength;
	}
	if (has_a_record) {
		*output = first_address;
		mcca_dns_last_status = "ok";
		mcca_dns_last_ttl = first_ttl;
		MccaRememberDnsCache(host, output, mcca_dns_last_ttl, mcca_dns_last_answer_count);
		return 1;
	}
	if (first_cname[0]) {
		const stduint cname_count = mcca_dns_last_cname_count;
		const stduint non_a_count = mcca_dns_last_non_a_count;
		const uint16 original_answer_count = answer_count;
		const int resolved = MccaResolveIPv4Query(first_cname, output, depth + 1);
		mcca_dns_last_cname_count += cname_count;
		mcca_dns_last_non_a_count += non_a_count;
		if (resolved && mcca_dns_last_ttl) {
			mcca_dns_last_answer_count += original_answer_count;
			MccaRememberDnsCache(host, output, mcca_dns_last_ttl, mcca_dns_last_answer_count);
		}
		return resolved;
	}
	return MccaResolveDnsFail(host, "no-a-record");
}

extern "C" int mcca_net_resolve_ipv4(const char* host, struct in_addr* output) {
	return MccaResolveIPv4Query(host, output, 0);
}
