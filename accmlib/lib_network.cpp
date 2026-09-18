#include "aaaaa.h"
#include "lib_network.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cpp/System/Network/Layer/Application/DNS.hpp>
#include <cpp/System/Network/Layer/Application/HTTP.hpp>

static uint16 MccaReadNet16(const uint8* data) {
	return uint16((uint16(data[0]) << 8) | data[1]);
}

static void MccaWriteNet16(uint8* data, uint16 value) {
	data[0] = uint8(value >> 8);
	data[1] = uint8(value);
}

constexpr stduint MccaDnsCacheCapacity = 4;
constexpr stduint MccaDnsAddressCapacity = 4;
constexpr stduint MccaDnsResolveDepthLimit = 4;

struct MccaDnsCacheEntry {
	char host[64];
	char status[24];
	struct in_addr address;
	struct in_addr addresses[MccaDnsAddressCapacity];
	stduint expire_second;
	stduint answer_count;
	stduint address_count;
	bool negative;
};

static MccaDnsCacheEntry mcca_dns_cache[MccaDnsCacheCapacity]{};
static stduint mcca_dns_cache_next = 0;
static stduint mcca_dns_cache_last = stduint(-1);
static char mcca_dns_kernel_host[64] = {};
static char mcca_dns_kernel_status[24] = {};
static struct in_addr mcca_dns_override_server = {};
static bool mcca_dns_override_server_valid = false;
static const char* mcca_dns_last_status = "none";
static stduint mcca_dns_last_ttl = 0;
static stduint mcca_dns_last_answer_count = 0;
static stduint mcca_dns_last_address_count = 0;
static stduint mcca_dns_last_cname_count = 0;
static stduint mcca_dns_last_non_a_count = 0;
static struct in_addr mcca_dns_last_addresses[MccaDnsAddressCapacity] = {};
static stduint mcca_dns_rotation = 0;

static void MccaClearDnsLastAnswers() {
	mcca_dns_last_address_count = 0;
	for0(i, MccaDnsAddressCapacity) mcca_dns_last_addresses[i] = {};
}

static void MccaRememberDnsAnswer(const struct in_addr& address) {
	if (address.s_addr == 0) return;
	for0(i, mcca_dns_last_address_count) {
		if (mcca_dns_last_addresses[i].s_addr == address.s_addr) return;
	}
	if (mcca_dns_last_address_count < MccaDnsAddressCapacity) {
		mcca_dns_last_addresses[mcca_dns_last_address_count++] = address;
	}
}

static void MccaExpireDnsCache() {
	const stduint now = syssecond();
	for0(i, MccaDnsCacheCapacity) {
		auto& entry = mcca_dns_cache[i];
		if (!entry.host[0]) continue;
		if (!entry.expire_second || now >= entry.expire_second) {
			entry = {};
			if (mcca_dns_cache_last == i) mcca_dns_cache_last = stduint(-1);
		}
	}
}

static MccaDnsCacheEntry* MccaDnsCacheFind(const char* host) {
	if (!host) return nullptr;
	MccaExpireDnsCache();
	for0(i, MccaDnsCacheCapacity) {
		auto& entry = mcca_dns_cache[i];
		if (!entry.host[0]) continue;
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

static void MccaFillDnsCacheAddress(syscall_net_dns_cache_t& entry, const struct in_addr* address) {
	if (!address) return;
	const uint8* source = reinterpret_cast<const uint8*>(&address->s_addr);
	for0(i, 4) entry.address[i] = source[i];
}

static void MccaFillDnsCacheAddresses(syscall_net_dns_cache_t& entry,
	const struct in_addr* addresses, stduint address_count) {
	if (!addresses) return;
	if (address_count > syscall_net_dns_cache_address_capacity) {
		address_count = syscall_net_dns_cache_address_capacity;
	}
	entry.address_count = uint16(address_count);
	for0(a, address_count) {
		const uint8* source = reinterpret_cast<const uint8*>(&addresses[a].s_addr);
		for0(i, 4) entry.addresses[a][i] = source[i];
	}
}

static void MccaCopyText(char* output, stduint capacity, const char* input) {
	if (!output || !capacity) return;
	stduint i = 0;
	if (input) {
		for(; i + 1 < capacity && input[i]; i++) output[i] = input[i];
	}
	output[i] = 0;
}

static int MccaStoreKernelDnsCache(const char* host, const struct in_addr* address,
	const char* status, stduint ttl, stduint answer_count, bool negative) {
	if (!host || !host[0]) return 0;
	syscall_net_dns_cache_t entry{};
	MccaCopyText(entry.host, sizeof(entry.host), host);
	MccaCopyText(entry.status, sizeof(entry.status), status ? status : (negative ? "cached-fail" : "ok"));
	MccaFillDnsCacheAddress(entry, address);
	MccaFillDnsCacheAddresses(entry, mcca_dns_last_addresses, mcca_dns_last_address_count);
	entry.ttl = uint32(ttl);
	entry.answer_count = uint32(answer_count);
	if (negative) entry.flags |= syscall_net_dns_cache_flag_negative;
	return syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::DNSCacheStore),
		_IMM(&entry), sizeof(entry)) >= 0;
}

static int MccaGetKernelDnsCacheEntry(stduint index, syscall_net_dns_cache_t& entry) {
	entry = {};
	entry.entry_index = uint16(index);
	return syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::DNSCacheEntry),
		_IMM(&entry), sizeof(entry)) >= 0;
}

static int MccaKernelDnsCacheCount(stduint* output) {
	if (!output) return 0;
	stduint count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::DNSCacheCount),
		_IMM(&count), sizeof(count)) < 0) return 0;
	*output = count;
	return 1;
}

static int MccaFindKernelDnsCache(const char* host, syscall_net_dns_cache_t& entry) {
	if (!host || !host[0]) return 0;
	stduint count = 0;
	if (!MccaKernelDnsCacheCount(&count)) return 0;
	for0(i, count) {
		if (!MccaGetKernelDnsCacheEntry(i, entry)) continue;
		if (!strcmp(entry.host, host)) return 1;
	}
	return 0;
}

static void MccaLoadKernelDnsAnswers(const syscall_net_dns_cache_t& entry) {
	MccaClearDnsLastAnswers();
	if (entry.flags & syscall_net_dns_cache_flag_negative) return;
	stduint address_count = entry.address_count;
	if (address_count > syscall_net_dns_cache_address_capacity) {
		address_count = syscall_net_dns_cache_address_capacity;
	}
	for0(a, address_count) {
		struct in_addr address{};
		uint8* target = reinterpret_cast<uint8*>(&address.s_addr);
		for0(i, 4) target[i] = entry.addresses[a][i];
		MccaRememberDnsAnswer(address);
	}
	if (!mcca_dns_last_address_count) {
		struct in_addr address{};
		uint8* target = reinterpret_cast<uint8*>(&address.s_addr);
		for0(i, 4) target[i] = entry.address[i];
		MccaRememberDnsAnswer(address);
	}
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
	strcpy(entry->status, "ok");
	entry->address = *address;
	entry->address_count = mcca_dns_last_address_count;
	struct in_addr empty_address{};
	for0(i, MccaDnsAddressCapacity) {
		entry->addresses[i] = i < mcca_dns_last_address_count ? mcca_dns_last_addresses[i] : empty_address;
	}
	if (!entry->address_count) {
		entry->addresses[0] = *address;
		entry->address_count = 1;
	}
	entry->expire_second = syssecond() + ttl;
	entry->answer_count = answer_count;
	entry->negative = false;
	MccaStoreKernelDnsCache(host, address, "ok", ttl, answer_count, false);
}

static void MccaRememberDnsFailure(const char* host, const char* status) {
	if (!host || !status || strlen(host) >= sizeof(mcca_dns_cache[0].host)) return;
	MccaDnsCacheEntry* entry = MccaDnsCacheFind(host);
	if (!entry) {
		entry = &mcca_dns_cache[mcca_dns_cache_next];
		mcca_dns_cache_last = mcca_dns_cache_next;
		mcca_dns_cache_next = (mcca_dns_cache_next + 1) % MccaDnsCacheCapacity;
	}
	strncpy(entry->host, host, sizeof(entry->host) - 1);
	entry->host[sizeof(entry->host) - 1] = 0;
	strncpy(entry->status, status, sizeof(entry->status) - 1);
	entry->status[sizeof(entry->status) - 1] = 0;
	struct in_addr empty{};
	entry->address = empty;
	entry->address_count = 0;
	for0(i, MccaDnsAddressCapacity) entry->addresses[i] = {};
	entry->expire_second = syssecond() + 3;
	entry->answer_count = 0;
	entry->negative = true;
	MccaStoreKernelDnsCache(host, nullptr, status, 3, 0, true);
}

static int MccaResolveDnsFail(const char* host, const char* status) {
	(void)host;
	mcca_dns_last_status = status ? status : "unknown";
	mcca_dns_last_ttl = 0;
	mcca_dns_last_answer_count = 0;
	MccaClearDnsLastAnswers();
	mcca_dns_last_cname_count = 0;
	mcca_dns_last_non_a_count = 0;
	if (host && status && strcmp(status, "bad-argument") && strcmp(status, "numeric")) {
		MccaRememberDnsFailure(host, status);
	}
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

static uni::Network::DNSResolveStatus MccaDnsStatusFromText(const char* status) {
	if (!status) return uni::Network::DNSResolveStatus::None;
	if (!strcmp(status, "ok")) return uni::Network::DNSResolveStatus::OK;
	if (!strcmp(status, "cached")) return uni::Network::DNSResolveStatus::Cached;
	if (!strcmp(status, "numeric")) return uni::Network::DNSResolveStatus::Numeric;
	if (!strcmp(status, "no-dns-server")) return uni::Network::DNSResolveStatus::NoDNSServer;
	if (!strcmp(status, "timeout")) return uni::Network::DNSResolveStatus::Timeout;
	if (!strcmp(status, "nxdomain")) return uni::Network::DNSResolveStatus::NXDomain;
	if (!strcmp(status, "servfail")) return uni::Network::DNSResolveStatus::ServFail;
	if (!strcmp(status, "no-a-record") || !strcmp(status, "no-aaaa") ||
		!strcmp(status, "cached-no-a")) return uni::Network::DNSResolveStatus::NoAddress;
	if (!strcmp(status, "bad-name")) return uni::Network::DNSResolveStatus::BadName;
	if (!strcmp(status, "not-implemented")) return uni::Network::DNSResolveStatus::Unsupported;
	if (!strcmp(status, "bad-argument")) return uni::Network::DNSResolveStatus::Unsupported;
	if (!strcmp(status, "bad-xid") || !strcmp(status, "bad-answer") ||
		!strcmp(status, "bad-question") || !strcmp(status, "bad-rdata") ||
		!strcmp(status, "short-reply") || !strcmp(status, "not-response") ||
		!strcmp(status, "format-error")) return uni::Network::DNSResolveStatus::BadReply;
	return uni::Network::DNSResolveStatus::None;
}

static uint32 MccaReadNet32(const uint8* data) {
	return (uint32(data[0]) << 24) | (uint32(data[1]) << 16) |
		(uint32(data[2]) << 8) | uint32(data[3]);
}

static bool MccaSameIPv4(const struct in_addr& lhs, const struct in_addr& rhs) {
	return lhs.s_addr == rhs.s_addr;
}

static bool MccaIPv4IsZero(const struct in_addr& address) {
	return address.s_addr == 0;
}

static bool MccaRememberDnsCandidate(struct in_addr previous[], stduint capacity,
	stduint& count, const struct in_addr& candidate) {
	if (MccaIPv4IsZero(candidate)) return false;
	for0(i, count) if (MccaSameIPv4(previous[i], candidate)) return false;
	if (count < capacity) previous[count++] = candidate;
	return true;
}

static bool MccaSelectDnsCandidate(stduint index, stduint& ordinal,
	struct in_addr previous[], stduint capacity, stduint& previous_count,
	const struct in_addr& candidate, struct in_addr* output) {
	if (!MccaRememberDnsCandidate(previous, capacity, previous_count, candidate)) return false;
	if (ordinal++ != index) return false;
	*output = candidate;
	return true;
}

static bool MccaGetConfiguredDnsServerAt(stduint index, struct in_addr* output) {
	if (!output) return false;
	stduint ordinal = 0;
	struct in_addr previous[4]{};
	stduint previous_count = 0;
	if (mcca_dns_override_server_valid &&
		MccaSelectDnsCandidate(index, ordinal, previous, numsof(previous),
			previous_count, mcca_dns_override_server, output)) return true;
	stduint count = 0;
	if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4InterfaceCount),
		_IMM(&count), sizeof(count)) < 0) return false;
	for0(i, count) {
		syscall_net_interface_ipv4_t iface{};
		iface.link_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4Interface),
			_IMM(&iface), sizeof(iface)) < 0) continue;
		struct in_addr candidate{};
		uint8* target = reinterpret_cast<uint8*>(&candidate.s_addr);
		for0(j, 4) target[j] = iface.dns[j];
		if (MccaSelectDnsCandidate(index, ordinal, previous, numsof(previous),
			previous_count, candidate, output)) return true;
	}
	for0(i, count) {
		syscall_net_interface_ipv4_t iface{};
		iface.link_index = uint16(i);
		if (syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::IPv4Interface),
			_IMM(&iface), sizeof(iface)) < 0) continue;
		struct in_addr candidate{};
		uint8* target = reinterpret_cast<uint8*>(&candidate.s_addr);
		for0(j, 4) target[j] = iface.gateway[j];
		if (MccaSelectDnsCandidate(index, ordinal, previous, numsof(previous),
			previous_count, candidate, output)) return true;
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

extern "C" int mcca_net_dns_status_code() {
	return int(MccaDnsStatusFromText(mcca_dns_last_status));
}

extern "C" int mcca_net_dns_status_text_code(const char* status) {
	return int(MccaDnsStatusFromText(status));
}

extern "C" stduint mcca_net_dns_ttl() {
	return mcca_dns_last_ttl;
}

extern "C" stduint mcca_net_dns_answer_count() {
	return mcca_dns_last_answer_count;
}

extern "C" stduint mcca_net_dns_address_count() {
	return mcca_dns_last_address_count;
}

extern "C" int mcca_net_dns_address(stduint index, struct in_addr* output) {
	if (!output || index >= mcca_dns_last_address_count) return 0;
	*output = mcca_dns_last_addresses[index];
	return 1;
}

extern "C" stduint mcca_net_dns_cname_count() {
	return mcca_dns_last_cname_count;
}

extern "C" stduint mcca_net_dns_non_a_count() {
	return mcca_dns_last_non_a_count;
}

extern "C" stduint mcca_net_dns_cache_count() {
	MccaExpireDnsCache();
	stduint kernel_count = 0;
	if (MccaKernelDnsCacheCount(&kernel_count)) return kernel_count;
	stduint count = 0;
	for0(i, MccaDnsCacheCapacity) if (MccaDnsCacheEntryTtl(mcca_dns_cache[i])) count++;
	return count;
}

extern "C" int mcca_net_dns_cache_entry(stduint index, const char** host, struct in_addr* output, stduint* ttl) {
	MccaExpireDnsCache();
	syscall_net_dns_cache_t kernel_entry{};
	if (MccaGetKernelDnsCacheEntry(index, kernel_entry)) {
		MccaCopyText(mcca_dns_kernel_host, sizeof(mcca_dns_kernel_host), kernel_entry.host);
		if (host) *host = mcca_dns_kernel_host;
		if (output) {
			output->s_addr = 0;
			if (!(kernel_entry.flags & syscall_net_dns_cache_flag_negative)) {
				uint8* target = reinterpret_cast<uint8*>(&output->s_addr);
				for0(i, 4) target[i] = kernel_entry.address[i];
			}
		}
		if (ttl) *ttl = kernel_entry.ttl;
		return 1;
	}
	stduint ordinal = 0;
	for0(i, MccaDnsCacheCapacity) {
		auto& entry = mcca_dns_cache[i];
		const stduint entry_ttl = MccaDnsCacheEntryTtl(entry);
		if (!entry_ttl) continue;
		if (ordinal++ != index) continue;
		if (host) *host = entry.host;
		if (output) {
			if (entry.negative) {
				struct in_addr empty{};
				*output = empty;
			}
			else *output = entry.address;
		}
		if (ttl) *ttl = entry_ttl;
		return 1;
	}
	return 0;
}

extern "C" const char* mcca_net_dns_cache_entry_status(stduint index) {
	MccaExpireDnsCache();
	syscall_net_dns_cache_t kernel_entry{};
	if (MccaGetKernelDnsCacheEntry(index, kernel_entry)) {
		MccaCopyText(mcca_dns_kernel_status, sizeof(mcca_dns_kernel_status),
			kernel_entry.status[0] ? kernel_entry.status : "unknown");
		return mcca_dns_kernel_status;
	}
	stduint ordinal = 0;
	for0(i, MccaDnsCacheCapacity) {
		auto& entry = mcca_dns_cache[i];
		if (!MccaDnsCacheEntryTtl(entry)) continue;
		if (ordinal++ != index) continue;
		return entry.negative ? entry.status : "ok";
	}
	return nullptr;
}

extern "C" stduint mcca_net_dns_cache_entry_address_count(stduint index) {
	MccaExpireDnsCache();
	syscall_net_dns_cache_t kernel_entry{};
	if (MccaGetKernelDnsCacheEntry(index, kernel_entry)) {
		if (kernel_entry.flags & syscall_net_dns_cache_flag_negative) return 0;
		stduint address_count = kernel_entry.address_count;
		if (address_count > syscall_net_dns_cache_address_capacity) {
			address_count = syscall_net_dns_cache_address_capacity;
		}
		if (!address_count) {
			struct in_addr address{};
			uint8* target = reinterpret_cast<uint8*>(&address.s_addr);
			for0(i, 4) target[i] = kernel_entry.address[i];
			if (address.s_addr) address_count = 1;
		}
		return address_count;
	}
	stduint ordinal = 0;
	for0(i, MccaDnsCacheCapacity) {
		auto& entry = mcca_dns_cache[i];
		if (!MccaDnsCacheEntryTtl(entry)) continue;
		if (ordinal++ != index) continue;
		if (entry.negative) return 0;
		return entry.address_count ? entry.address_count : (entry.address.s_addr ? 1 : 0);
	}
	return 0;
}

extern "C" int mcca_net_dns_cache_entry_address(stduint index, stduint address_index, struct in_addr* output) {
	if (!output) return 0;
	MccaExpireDnsCache();
	syscall_net_dns_cache_t kernel_entry{};
	if (MccaGetKernelDnsCacheEntry(index, kernel_entry)) {
		if (kernel_entry.flags & syscall_net_dns_cache_flag_negative) return 0;
		stduint address_count = kernel_entry.address_count;
		if (address_count > syscall_net_dns_cache_address_capacity) {
			address_count = syscall_net_dns_cache_address_capacity;
		}
		if (address_count && address_index < address_count) {
			uint8* target = reinterpret_cast<uint8*>(&output->s_addr);
			for0(i, 4) target[i] = kernel_entry.addresses[address_index][i];
			return output->s_addr != 0;
		}
		if (!address_count && address_index == 0) {
			uint8* target = reinterpret_cast<uint8*>(&output->s_addr);
			for0(i, 4) target[i] = kernel_entry.address[i];
			return output->s_addr != 0;
		}
		return 0;
	}
	stduint ordinal = 0;
	for0(i, MccaDnsCacheCapacity) {
		auto& entry = mcca_dns_cache[i];
		if (!MccaDnsCacheEntryTtl(entry)) continue;
		if (ordinal++ != index) continue;
		if (entry.negative) return 0;
		if (!entry.address_count && address_index == 0) {
			*output = entry.address;
			return output->s_addr != 0;
		}
		if (address_index >= entry.address_count) return 0;
		*output = entry.addresses[address_index];
		return output->s_addr != 0;
	}
	return 0;
}

extern "C" int mcca_net_dns_cache_clear() {
	for0(i, MccaDnsCacheCapacity) mcca_dns_cache[i] = {};
	mcca_dns_cache_next = 0;
	mcca_dns_cache_last = stduint(-1);
	return syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::DNSCacheClear), 1, 0) >= 0;
}

extern "C" const char* mcca_net_dns_cache_host() {
	MccaExpireDnsCache();
	if (mcca_dns_cache_last >= MccaDnsCacheCapacity) return nullptr;
	const auto& entry = mcca_dns_cache[mcca_dns_cache_last];
	return MccaDnsCacheEntryTtl(entry) ? entry.host : nullptr;
}

extern "C" stduint mcca_net_dns_cache_ttl() {
	MccaExpireDnsCache();
	if (mcca_dns_cache_last >= MccaDnsCacheCapacity) return 0;
	return MccaDnsCacheEntryTtl(mcca_dns_cache[mcca_dns_cache_last]);
}

extern "C" int mcca_net_dns_cache_address(struct in_addr* output) {
	MccaExpireDnsCache();
	if (!output || mcca_dns_cache_last >= MccaDnsCacheCapacity ||
		!MccaDnsCacheEntryTtl(mcca_dns_cache[mcca_dns_cache_last]) ||
		mcca_dns_cache[mcca_dns_cache_last].negative) return 0;
	*output = mcca_dns_cache[mcca_dns_cache_last].address;
	return 1;
}

extern "C" int mcca_net_dns_set_server_ipv4(const struct in_addr* address) {
	if (!address) {
		struct in_addr empty{};
		mcca_dns_override_server = empty;
		mcca_dns_override_server_valid = false;
		return 1;
	}
	mcca_dns_override_server = *address;
	mcca_dns_override_server_valid = true;
	return 1;
}

extern "C" int mcca_net_dhcp_renew() {
	return syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::DHCPRenew), 1, 0) >= 0;
}

extern "C" int mcca_net_dhcp_release() {
	return syscall(syscall_t::ROUT, stduint(syscall_net_route_func_t::DHCPRelease), 1, 0) >= 0;
}

extern "C" int mcca_net_dns_probe_aaaa(const char* host, stduint* ttl) {
	if (ttl) *ttl = 0;
	mcca_dns_last_ttl = 0;
	mcca_dns_last_answer_count = 0;
	MccaClearDnsLastAnswers();
	mcca_dns_last_cname_count = 0;
	mcca_dns_last_non_a_count = 0;
	if (!host) {
		mcca_dns_last_status = "bad-argument";
		return 0;
	}
	uint8 query[256] = {};
	static uint16 next_id = 0x4141u;
	const uint16 query_id = next_id++;
	MccaWriteNet16(query + 0, query_id);
	MccaWriteNet16(query + 2, 0x0100u);
	MccaWriteNet16(query + 4, 1);
	stduint name_length = 0;
	if (!MccaEncodeDnsName(query + 12, sizeof(query) - 16, host, &name_length)) {
		mcca_dns_last_status = "bad-name";
		return 0;
	}
	stduint query_length = 12 + name_length;
	MccaWriteNet16(query + query_length, 28);
	MccaWriteNet16(query + query_length + 2, 1);
	query_length += 4;

	uint8 response[512] = {};
	stdsint received = -1;
	const char* io_status = "no-dns-server";
	for0(candidate_index, 4) {
		struct in_addr dns{};
		if (!MccaGetConfiguredDnsServerAt(candidate_index, &dns)) break;
		int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (fd < 0) {
			io_status = "socket";
			continue;
		}
		struct sockaddr_in server{};
		server.sin_family = AF_INET;
		server.sin_port = htons(53);
		server.sin_addr = dns;
		const stdsint sent = sendto(fd, query, query_length, 0,
			(const struct sockaddr*)&server, sizeof(server));
		if (sent != stdsint(query_length)) {
			close(fd);
			io_status = "send";
			continue;
		}
		struct pollfd pfd{};
		pfd.fd = fd;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 2500) <= 0 || !(pfd.revents & POLLIN)) {
			close(fd);
			io_status = "timeout";
			continue;
		}
		received = recvfrom(fd, response, sizeof(response), 0, nullptr, nullptr);
		close(fd);
		break;
	}
	if (received < 0) {
		mcca_dns_last_status = io_status;
		return 0;
	}
	if (received < 12) {
		mcca_dns_last_status = "short-reply";
		return 0;
	}
	const stduint response_length = stduint(received);
	if (MccaReadNet16(response) != query_id) {
		mcca_dns_last_status = "bad-xid";
		return 0;
	}
	const uint16 dns_flags = MccaReadNet16(response + 2);
	const uint16 dns_rcode = dns_flags & 0x000Fu;
	if (!(dns_flags & 0x8000u)) {
		mcca_dns_last_status = "not-response";
		return 0;
	}
	if (dns_rcode != 0) {
		mcca_dns_last_status = MccaDnsRcodeStatus(dns_rcode);
		return 0;
	}
	const uint16 question_count = MccaReadNet16(response + 4);
	const uint16 answer_count = MccaReadNet16(response + 6);
	mcca_dns_last_answer_count = answer_count;
	mcca_dns_last_cname_count = 0;
	mcca_dns_last_non_a_count = 0;
	stduint offset = 12;
	for0(i, question_count) {
		if (!MccaSkipDnsName(response, response_length, &offset) || offset + 4 > response_length) {
			mcca_dns_last_status = "bad-question";
			return 0;
		}
		offset += 4;
	}
	for0(i, answer_count) {
		if (!MccaSkipDnsName(response, response_length, &offset) || offset + 10 > response_length) {
			mcca_dns_last_status = "bad-answer";
			return 0;
		}
		const uint16 type = MccaReadNet16(response + offset);
		const uint16 dns_class = MccaReadNet16(response + offset + 2);
		uint32 answer_ttl = MccaReadNet32(response + offset + 4);
		const uint16 rdlength = MccaReadNet16(response + offset + 8);
		offset += 10;
		if (offset + rdlength > response_length) {
			mcca_dns_last_status = "bad-rdata";
			return 0;
		}
		if (type == 28 && dns_class == 1 && rdlength == 16) {
			if (answer_ttl > 86400) answer_ttl = 86400;
			if (ttl) *ttl = stduint(answer_ttl);
			mcca_dns_last_status = "ok";
			mcca_dns_last_ttl = stduint(answer_ttl);
			return 1;
		}
		if (type == 5 && dns_class == 1) mcca_dns_last_cname_count++;
		else mcca_dns_last_non_a_count++;
		offset += rdlength;
	}
	mcca_dns_last_status = "no-aaaa";
	mcca_dns_last_ttl = 0;
	return 0;
}

int mcca_net_resolve_ipv4_result(const char* host, uni::Network::DNSIPv4Result& result) {
	uni::Network::DNSClearIPv4Result(result);
	struct in_addr address{};
	if (!mcca_net_resolve_ipv4(host, &address)) {
		result.status = MccaDnsStatusFromText(mcca_net_dns_status());
		result.ttl = uint32(mcca_dns_last_ttl);
		result.answer_count = mcca_dns_last_answer_count;
		result.cname_count = mcca_dns_last_cname_count;
		result.non_address_count = mcca_dns_last_non_a_count;
		return 0;
	}
	result.status = MccaDnsStatusFromText(mcca_net_dns_status());
	result.ttl = uint32(mcca_dns_last_ttl);
	result.answer_count = mcca_dns_last_answer_count;
	result.cname_count = mcca_dns_last_cname_count;
	result.non_address_count = mcca_dns_last_non_a_count;
	for0(i, mcca_dns_last_address_count) {
		uni::Network::IPv4Address item{};
		const uint8* source = reinterpret_cast<const uint8*>(&mcca_dns_last_addresses[i].s_addr);
		uni::Network::IPv4CopyAddress(item, source);
		(void)uni::Network::DNSAppendIPv4Address(result, item);
	}
	if (!result.address_count) {
		uni::Network::IPv4Address item{};
		const uint8* source = reinterpret_cast<const uint8*>(&address.s_addr);
		uni::Network::IPv4CopyAddress(item, source);
		(void)uni::Network::DNSAppendIPv4Address(result, item);
	}
	return 1;
}

extern "C" const char* mcca_net_socket_error_name(int error) {
	switch (error) {
	case 0: return "none";
	case 32: return "broken-pipe";
	case 104: return "reset";
	case 105: return "no-buffer";
	case 111: return "refused";
	case 112: return "addr-in-use";
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

extern "C" int mcca_net_get_socket_option_int(int fd, int option_name, int* output) {
	if (!output) return 0;
	int value = 0;
	socklen_t length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, option_name, &value, &length) < 0) return 0;
	*output = value;
	return 1;
}

extern "C" stduint mcca_net_timeval_milliseconds(const struct timeval* timeout) {
	if (!timeout) return 0;
	return stduint(timeout->tv_sec) * 1000u + stduint((timeout->tv_usec + 999) / 1000);
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

extern "C" int mcca_net_http_build_get_request(char* output, stduint capacity, const char* host, const char* path) {
	const stdsint length = uni::Network::HTTPBuildGET(output, capacity, host, path);
	return length < 0 ? 0 : int(length);
}

extern "C" int mcca_net_http_build_get(char* output, stduint capacity, const char* host, const char* path) {
	return mcca_net_http_build_get_request(output, capacity, host, path);
}

extern "C" int mcca_net_http_parse_status_code(const char* response, stduint length) {
	return uni::Network::HTTPParseStatusCode(response, length);
}

extern "C" int mcca_net_http_status_code(const char* response, stduint length) {
	return mcca_net_http_parse_status_code(response, length);
}

extern "C" stduint mcca_net_http_header_length(const char* response, stduint length) {
	return uni::Network::HTTPHeaderLength(response, length);
}

extern "C" int mcca_net_http_content_length(const char* response, stduint length, stduint* output) {
	if (!output) return 0;
	return uni::Network::HTTPContentLength(response, length, *output) ? 1 : 0;
}

static int MccaResolveIPv4Query(const char* host, struct in_addr* output, stduint depth) {
	if (!host || !output) return MccaResolveDnsFail(host, "bad-argument");
	if (depth > MccaDnsResolveDepthLimit) return MccaResolveDnsFail(host, "cname-loop");
	MccaClearDnsLastAnswers();
	syscall_net_dns_cache_t kernel_entry{};
	if (MccaFindKernelDnsCache(host, kernel_entry)) {
		mcca_dns_last_ttl = kernel_entry.ttl;
		mcca_dns_last_answer_count = kernel_entry.answer_count;
		mcca_dns_last_cname_count = 0;
		mcca_dns_last_non_a_count = 0;
		if (kernel_entry.flags & syscall_net_dns_cache_flag_negative) {
			mcca_dns_last_status = kernel_entry.status[0] ? kernel_entry.status : "cached-fail";
			return 0;
		}
		MccaLoadKernelDnsAnswers(kernel_entry);
		if (!mcca_dns_last_address_count) return MccaResolveDnsFail(host, "cached-no-a");
		*output = mcca_dns_last_addresses[mcca_dns_rotation++ % mcca_dns_last_address_count];
		mcca_dns_last_status = "cached";
		return 1;
	}
	if (auto* entry = MccaDnsCacheFind(host)) {
		if (entry->negative) {
			mcca_dns_last_status = entry->status[0] ? entry->status : "cached-fail";
			mcca_dns_last_ttl = MccaDnsCacheEntryTtl(*entry);
			mcca_dns_last_answer_count = 0;
			mcca_dns_last_cname_count = 0;
			mcca_dns_last_non_a_count = 0;
			return 0;
		}
		mcca_dns_last_status = "cached";
		mcca_dns_last_ttl = MccaDnsCacheEntryTtl(*entry);
		mcca_dns_last_answer_count = entry->answer_count;
		mcca_dns_last_cname_count = 0;
		mcca_dns_last_non_a_count = 0;
		MccaClearDnsLastAnswers();
		for0(i, entry->address_count) MccaRememberDnsAnswer(entry->addresses[i]);
		if (!mcca_dns_last_address_count) MccaRememberDnsAnswer(entry->address);
		if (!mcca_dns_last_address_count) return MccaResolveDnsFail(host, "cached-no-a");
		*output = mcca_dns_last_addresses[mcca_dns_rotation++ % mcca_dns_last_address_count];
		return 1;
	}
	if (inet_aton(host, output)) {
		mcca_dns_last_status = "numeric";
		mcca_dns_last_ttl = 0;
		mcca_dns_last_answer_count = 1;
		mcca_dns_last_cname_count = 0;
		mcca_dns_last_non_a_count = 0;
		MccaRememberDnsAnswer(*output);
		return 1;
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

	uint8 response[512] = {};
	stdsint received = -1;
	const char* io_status = "no-dns-server";
	for0(candidate_index, 4) {
		struct in_addr dns{};
		if (!MccaGetConfiguredDnsServerAt(candidate_index, &dns)) break;
		int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (fd < 0) {
			io_status = "socket";
			continue;
		}
		struct sockaddr_in server{};
		server.sin_family = AF_INET;
		server.sin_port = htons(53);
		server.sin_addr = dns;
		const stdsint sent = sendto(fd, query, query_length, 0,
			(const struct sockaddr*)&server, sizeof(server));
		if (sent != stdsint(query_length)) {
			close(fd);
			io_status = "send";
			continue;
		}
		struct pollfd pfd{};
		pfd.fd = fd;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 2500) <= 0 || !(pfd.revents & POLLIN)) {
			close(fd);
			io_status = "timeout";
			continue;
		}
		received = recvfrom(fd, response, sizeof(response), 0, nullptr, nullptr);
		close(fd);
		break;
	}
	if (received < 0) return MccaResolveDnsFail(host, io_status);
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
			struct in_addr address{};
			uint8* target = (uint8*)&address.s_addr;
			for0(j, 4) target[j] = response[offset + j];
			if (ttl > 86400) ttl = 86400;
			if (!first_ttl || ttl < first_ttl) first_ttl = stduint(ttl);
			MccaRememberDnsAnswer(address);
			has_a_record = true;
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
		if (!mcca_dns_last_address_count) return MccaResolveDnsFail(host, "no-a-record");
		*output = mcca_dns_last_addresses[mcca_dns_rotation++ % mcca_dns_last_address_count];
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

static char* MccaDuplicateText(const char* text) {
	if (!text) return nullptr;
	const stduint length = strlen(text);
	char* output = (char*)malloc(length + 1);
	if (!output) return nullptr;
	for0(i, length + 1) output[i] = text[i];
	return output;
}

extern "C" const char* gai_strerror(int error_code) {
	switch (error_code) {
	case 0: return "ok";
	case EAI_BADFLAGS: return "bad flags";
	case EAI_NONAME: return "name or service not known";
	case EAI_AGAIN: return "temporary failure";
	case EAI_FAIL: return "non-recoverable failure";
	case EAI_FAMILY: return "address family not supported";
	case EAI_MEMORY: return "memory allocation failure";
	case EAI_SERVICE: return "service not supported";
	case EAI_SOCKTYPE: return "socket type not supported";
	case EAI_SYSTEM: return "system error";
	default: return "address info error";
	}
}

extern "C" void freeaddrinfo(struct addrinfo* res) {
	while (res) {
		struct addrinfo* next = res->ai_next;
		if (res->ai_addr) free(res->ai_addr);
		if (res->ai_canonname) free(res->ai_canonname);
		free(res);
		res = next;
	}
}

extern "C" int getaddrinfo(const char* node, const char* service,
	const struct addrinfo* hints, struct addrinfo** res) {
	if (!res) return EAI_FAIL;
	*res = nullptr;
	const int flags = hints ? hints->ai_flags : 0;
	const int supported_flags = AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV;
	if (flags & ~supported_flags) return EAI_BADFLAGS;
	const int family = hints ? hints->ai_family : AF_UNSPEC;
	if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;
	const int socktype = hints ? hints->ai_socktype : 0;
	if (socktype && socktype != SOCK_STREAM && socktype != SOCK_DGRAM) return EAI_SOCKTYPE;
	const int protocol = hints ? hints->ai_protocol : 0;
	if (protocol && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) return EAI_SERVICE;
	uint16 port = 0;
	if (service && service[0]) {
		if (!mcca_net_parse_port(service, &port)) return EAI_SERVICE;
	}
	struct in_addr addresses[MccaDnsAddressCapacity] = {};
	stduint address_count = 0;
	if (!node || !node[0]) {
		address_count = 1;
		addresses[0].s_addr = (flags & AI_PASSIVE) ? INADDR_ANY : htonl(INADDR_LOOPBACK);
	}
	else {
		struct in_addr first{};
		if (flags & AI_NUMERICHOST) {
			if (!inet_aton(node, &first)) return EAI_NONAME;
			addresses[address_count++] = first;
		}
		else {
			if (!mcca_net_resolve_ipv4(node, &first)) {
				const auto status = MccaDnsStatusFromText(mcca_net_dns_status());
				if (status == uni::Network::DNSResolveStatus::Timeout ||
					status == uni::Network::DNSResolveStatus::NoDNSServer) return EAI_AGAIN;
				if (status == uni::Network::DNSResolveStatus::ServFail ||
					status == uni::Network::DNSResolveStatus::BadReply) return EAI_FAIL;
				return EAI_NONAME;
			}
			addresses[address_count++] = first;
			const stduint dns_count = mcca_net_dns_address_count();
			for0(i, dns_count) {
				if (address_count >= numsof(addresses)) break;
				struct in_addr item{};
				if (!mcca_net_dns_address(i, &item)) continue;
				bool duplicate = false;
				for0(j, address_count) {
					if (addresses[j].s_addr == item.s_addr) {
						duplicate = true;
						break;
					}
				}
				if (!duplicate) addresses[address_count++] = item;
			}
			if (!address_count) addresses[address_count++] = first;
		}
	}
	struct addrinfo* head = nullptr;
	struct addrinfo* tail = nullptr;
	const int output_socktype = socktype ? socktype : SOCK_STREAM;
	const int output_protocol = protocol ? protocol :
		(output_socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP);
	for0(i, address_count) {
		auto* info = (struct addrinfo*)malloc(sizeof(struct addrinfo));
		auto* addr = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
		if (!info || !addr) {
			if (info) free(info);
			if (addr) free(addr);
			freeaddrinfo(head);
			return EAI_MEMORY;
		}
		*info = {};
		*addr = {};
		addr->sin_family = AF_INET;
		addr->sin_port = htons(port);
		addr->sin_addr = addresses[i];
		info->ai_family = AF_INET;
		info->ai_socktype = output_socktype;
		info->ai_protocol = output_protocol;
		info->ai_addrlen = sizeof(struct sockaddr_in);
		info->ai_addr = (struct sockaddr*)addr;
		if ((flags & AI_CANONNAME) && !head && node && node[0]) {
			info->ai_canonname = MccaDuplicateText(node);
			if (!info->ai_canonname) {
				freeaddrinfo(info);
				freeaddrinfo(head);
				return EAI_MEMORY;
			}
		}
		if (!head) head = info;
		else tail->ai_next = info;
		tail = info;
	}
	*res = head;
	return 0;
}
