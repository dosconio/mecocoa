#include "aaaaa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void PrintUsage() {
	printf("usage: testtcp [--reuse] [--nonblock] [--poll-accept] [--backlog n] [--accept-delay ms] [--count n] [--sockopt] --listen [port]\n\r");
	printf("       testtcp [--repeat n] [--burst n] [--fill n] [--read-size n] [--write-size n] [--poll-after-recv] [--shutdown-write] [--shutdown-read] [--send-after-shutdown] [--sockopt] [--sockerr-twice] [--http] host port [payload]\n\r");
	printf("  listen: testtcp --listen 80\n\r");
	printf("  reuse:  testtcp --reuse --listen 80\n\r");
	printf("  nbacc:  testtcp --nonblock --listen 80\n\r");
	printf("  lpoll:  testtcp --poll-accept --listen 80\n\r");
	printf("  bklg:   testtcp --backlog 1 --listen 80\n\r");
	printf("  delay:  testtcp --backlog 1 --accept-delay 3000 --listen 80\n\r");
	printf("  count:  testtcp --listen 80 --count 3\n\r");
	printf("  burst:  testtcp --burst 3 10.0.2.1 7777 hello\n\r");
	printf("  fill:   testtcp --fill 1500 10.0.2.1 7777\n\r");
	printf("  read:   testtcp --read-size 2 10.0.2.1 7777 hello\n\r");
	printf("  write:  testtcp --write-size 2 10.0.2.1 7777 hello\n\r");
	printf("  poll:   testtcp --poll-after-recv 10.0.2.1 7777 hello\n\r");
	printf("  sdown:  testtcp --shutdown-write 10.0.2.1 7777 hello\n\r");
	printf("  rdcls:  testtcp --shutdown-read 10.0.2.1 7777 hello\n\r");
	printf("  opt:    testtcp --sockopt 10.0.2.1 7777 hello\n\r");
	printf("  client: testtcp 10.0.2.1 7777 hello\n\r");
	printf("  dns:    testtcp example.com 80 hello\n\r");
	printf("  http:   testtcp --http example.com 80\n\r");
	printf("  path:   testtcp --http --http-path / example.com 80\n\r");
	printf("  repeat: testtcp --repeat 3 10.0.2.1 7777 hello\n\r");
	printf("  host:   nc -vz -w 1 10.0.2.15 80\n\r");
	printf("  data:   printf hello | nc -w 1 10.0.2.15 80\n\r");
}

static void PrintSocketAddress(const char* label, const struct sockaddr_in& address) {
	const uint8* octet = (const uint8*)&address.sin_addr.s_addr;
	printf("testtcp: %s=%u.%u.%u.%u:%u\n\r", label,
		(unsigned)octet[0], (unsigned)octet[1], (unsigned)octet[2], (unsigned)octet[3],
		(unsigned)ntohs(address.sin_port));
}

static bool SetReuseAddress(int fd) {
	int enable = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) return false;
	int value = 0;
	socklen_t length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, &length) < 0) return false;
	printf("testtcp: reuseaddr=%d\n\r", value);
	return value != 0;
}

static void PrintSocketOptions(int fd) {
	int value = 0;
	socklen_t length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &value, &length) == 0) {
		printf("testtcp: so_type=%d\n\r", value);
	}
	length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &value, &length) == 0) {
		printf("testtcp: so_error=%d\n\r", value);
	}
}

static const char* SocketErrorName(int error) {
	switch (error) {
	case 0: return "none";
	case 111: return "refused";
	case 114: return "net-unreach";
	case 116: return "timeout";
	case 118: return "host-unreach";
	case 121: return "dest-required";
	default: return "error";
	}
}

static void PrintSocketError(int fd, const char* label) {
	int value = 0;
	socklen_t length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &value, &length) == 0) {
		printf("testtcp: %s=%d %s\n\r", label, value, SocketErrorName(value));
	}
}

static uint16 ReadNet16(const uint8* data) {
	return uint16((uint16(data[0]) << 8) | data[1]);
}

static void WriteNet16(uint8* data, uint16 value) {
	data[0] = uint8(value >> 8);
	data[1] = uint8(value);
}

static bool GetConfiguredDnsServer(struct in_addr* output) {
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

static bool EncodeDnsName(uint8* output, stduint capacity, const char* host, stduint* length) {
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

static bool SkipDnsName(const uint8* packet, stduint packet_length, stduint* offset) {
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

static bool ResolveDnsA(const char* host, struct in_addr* output) {
	if (!host || !output) return false;
	if (inet_aton(host, output)) return true;
	struct in_addr dns{};
	if (!GetConfiguredDnsServer(&dns)) {
		printf("testtcp: no dns server\n\r");
		return false;
	}

	uint8 query[256] = {};
	static uint16 next_id = 0x4D43;
	const uint16 query_id = next_id++;
	WriteNet16(query + 0, query_id);
	WriteNet16(query + 2, 0x0100u);
	WriteNet16(query + 4, 1);
	stduint name_length = 0;
	if (!EncodeDnsName(query + 12, sizeof(query) - 16, host, &name_length)) return false;
	stduint query_length = 12 + name_length;
	WriteNet16(query + query_length, 1);
	WriteNet16(query + query_length + 2, 1);
	query_length += 4;

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) return false;
	struct sockaddr_in server{};
	server.sin_family = AF_INET;
	server.sin_port = htons(53);
	server.sin_addr = dns;
	const stdsint sent = sendto(fd, query, query_length, 0,
		(const struct sockaddr*)&server, sizeof(server));
	if (sent != stdsint(query_length)) {
		close(fd);
		return false;
	}
	struct pollfd pfd{};
	pfd.fd = fd;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 2500) <= 0 || !(pfd.revents & POLLIN)) {
		close(fd);
		return false;
	}
	uint8 response[512] = {};
	const stdsint received = recvfrom(fd, response, sizeof(response), 0, nullptr, nullptr);
	close(fd);
	if (received < 12) return false;
	const stduint response_length = stduint(received);
	if (ReadNet16(response) != query_id) return false;
	if ((ReadNet16(response + 2) & 0x000Fu) != 0) return false;
	const uint16 question_count = ReadNet16(response + 4);
	const uint16 answer_count = ReadNet16(response + 6);
	stduint offset = 12;
	for0(i, question_count) {
		if (!SkipDnsName(response, response_length, &offset)) return false;
		if (offset + 4 > response_length) return false;
		offset += 4;
	}
	for0(i, answer_count) {
		if (!SkipDnsName(response, response_length, &offset)) return false;
		if (offset + 10 > response_length) return false;
		const uint16 type = ReadNet16(response + offset);
		const uint16 dns_class = ReadNet16(response + offset + 2);
		const uint16 rdlength = ReadNet16(response + offset + 8);
		offset += 10;
		if (offset + rdlength > response_length) return false;
		if (type == 1 && dns_class == 1 && rdlength == 4) {
			uint8* target = (uint8*)&output->s_addr;
			for0(j, 4) target[j] = response[offset + j];
			return true;
		}
		offset += rdlength;
	}
	return false;
}

static char* BuildHttpRequest(const char* host, const char* path) {
	if (!host) return nullptr;
	if (!path || !path[0]) path = "/";
	const char* prefix = "GET ";
	const char* middle = " HTTP/1.0\r\nHost: ";
	const char* suffix = "\r\nConnection: close\r\n\r\n";
	const stduint length = strlen(prefix) + strlen(path) + strlen(middle) + strlen(host) + strlen(suffix);
	char* request = (char*)malloc(length + 1);
	if (!request) return nullptr;
	char* cursor = request;
	const char* parts[] = { prefix, path, middle, host, suffix };
	for0(i, numsof(parts)) {
		const char* part = parts[i];
		while (*part) *cursor++ = *part++;
	}
	*cursor = 0;
	return request;
}

int main(int argc, char** argv) {
	if (argc >= 2 && (!StrCompare(argv[1], "-h") || !StrCompare(argv[1], "--help") ||
		!StrCompare(argv[1], "help"))) {
		PrintUsage();
		return 0;
	}

	bool listen_mode = false;
	bool reuse_address = false;
	bool nonblock = false;
	bool poll_accept = false;
	int accept_limit = 0;
	int listen_backlog = 4;
	int accept_delay = 0;
	int repeat_count = 1;
	int burst_count = 1;
	int read_size = 512;
	int write_size = 512;
	int fill_length = 0;
	bool poll_after_recv = false;
	bool show_sockopt = false;
	bool sockerr_twice = false;
	bool shutdown_write = false;
	bool shutdown_read = false;
	bool send_after_shutdown = false;
	bool http_mode = false;
	const char* http_path = "/";
	const char* positional[3] = {};
	stduint positional_count = 0;
	for (int i = 1; i < argc; i++) {
		if (StrCompare(argv[i], "--listen") == 0) {
			listen_mode = true;
			continue;
		}
		if (StrCompare(argv[i], "--reuse") == 0) {
			reuse_address = true;
			continue;
		}
		if (StrCompare(argv[i], "--nonblock") == 0) {
			nonblock = true;
			continue;
		}
		if (StrCompare(argv[i], "--poll-accept") == 0) {
			poll_accept = true;
			continue;
		}
		if (StrCompare(argv[i], "--backlog") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			listen_backlog = atoi(argv[i]);
			if (listen_backlog <= 0) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--accept-delay") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			accept_delay = atoi(argv[i]);
			if (accept_delay < 0) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--count") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			accept_limit = atoi(argv[i]);
			if (accept_limit <= 0) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--repeat") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			repeat_count = atoi(argv[i]);
			if (repeat_count <= 0) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--burst") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			burst_count = atoi(argv[i]);
			if (burst_count <= 0) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--fill") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			fill_length = atoi(argv[i]);
			if (fill_length <= 0 || fill_length > 4096) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--read-size") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			read_size = atoi(argv[i]);
			if (read_size <= 0 || read_size > 512) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--write-size") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			write_size = atoi(argv[i]);
			if (write_size <= 0 || write_size > 4096) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--poll-after-recv") == 0) {
			poll_after_recv = true;
			continue;
		}
		if (StrCompare(argv[i], "--sockopt") == 0) {
			show_sockopt = true;
			continue;
		}
		if (StrCompare(argv[i], "--sockerr-twice") == 0) {
			sockerr_twice = true;
			show_sockopt = true;
			continue;
		}
		if (StrCompare(argv[i], "--shutdown-write") == 0) {
			shutdown_write = true;
			continue;
		}
		if (StrCompare(argv[i], "--shutdown-read") == 0) {
			shutdown_read = true;
			continue;
		}
		if (StrCompare(argv[i], "--send-after-shutdown") == 0) {
			send_after_shutdown = true;
			shutdown_write = true;
			continue;
		}
		if (StrCompare(argv[i], "--http") == 0) {
			http_mode = true;
			continue;
		}
		if (StrCompare(argv[i], "--http-path") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			http_mode = true;
			http_path = argv[i];
			continue;
		}
		if (positional_count >= 3) {
			PrintUsage();
			return 1;
		}
		positional[positional_count++] = argv[i];
	}

	if (!listen_mode) {
		if (reuse_address || nonblock || poll_accept || accept_limit || listen_backlog != 4 || accept_delay ||
			(fill_length ? positional_count != 2 : (http_mode ? positional_count != 2 : positional_count != 3))) {
			PrintUsage();
			return 1;
		}
		struct in_addr target_address{};
		const int target_port = atoi(positional[1]);
		char* fill_payload = nullptr;
		char* http_payload = nullptr;
		const char* payload = fill_length || http_mode ? nullptr : positional[2];
		if (target_port <= 0 || target_port > 65535) {
			PrintUsage();
			return 1;
		}
		if (!ResolveDnsA(positional[0], &target_address)) {
			printf("testtcp: resolve failed\n\r");
			return 1;
		}
		printf("testtcp: target=%s ip=%s\n\r", positional[0], inet_ntoa(target_address));
		if (fill_length) {
			fill_payload = (char*)malloc(stduint(fill_length) + 1);
			if (!fill_payload) {
				printf("testtcp: fill alloc failed\n\r");
				return 1;
			}
			for (int i = 0; i < fill_length; i++) fill_payload[i] = char('A' + (i % 26));
			fill_payload[fill_length] = 0;
			payload = fill_payload;
		}
		if (http_mode) {
			http_payload = BuildHttpRequest(positional[0], http_path);
			if (!http_payload) {
				printf("testtcp: http request alloc failed\n\r");
				if (fill_payload) free(fill_payload);
				return 1;
			}
			payload = http_payload;
		}
		for (int repeat = 0; repeat < repeat_count; repeat++) {
			if (repeat_count > 1) printf("testtcp: repeat %d/%d\n\r", repeat + 1, repeat_count);
			int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0) {
				printf("testtcp: socket failed\n\r");
				if (fill_payload) free(fill_payload);
				if (http_payload) free(http_payload);
				return 1;
			}
			if (show_sockopt) PrintSocketOptions(fd);
			struct sockaddr_in target{};
			target.sin_family = AF_INET;
			target.sin_port = htons((uint16)target_port);
			target.sin_addr = target_address;
			if (connect(fd, (const struct sockaddr*)&target, sizeof(target)) < 0) {
				printf("testtcp: connect failed\n\r");
				PrintSocketError(fd, "connect-error");
				close(fd);
				if (fill_payload) free(fill_payload);
				if (http_payload) free(http_payload);
				return 1;
			}
			struct sockaddr_in local{};
			socklen_t local_length = sizeof(local);
			if (getsockname(fd, (struct sockaddr*)&local, &local_length) == 0) {
				PrintSocketAddress("local", local);
			}
			PrintSocketAddress("peer", target);
			if (show_sockopt) PrintSocketOptions(fd);
			if (sockerr_twice) PrintSocketOptions(fd);
			const size_t payload_length = strlen(payload);
			stduint sent_total = 0;
			for (int burst = 0; burst < burst_count; burst++) {
				if (burst_count > 1) printf("testtcp: burst %d/%d\n\r", burst + 1, burst_count);
				stduint payload_sent = 0;
				while (payload_sent < payload_length) {
					const stduint remaining = payload_length - payload_sent;
					const stduint chunk = remaining < stduint(write_size) ? remaining : stduint(write_size);
					const stdsint sent = write(fd, payload + payload_sent, chunk);
					if (sent < 0) {
						printf("testtcp: send failed\n\r");
						close(fd);
						if (fill_payload) free(fill_payload);
						if (http_payload) free(http_payload);
						return 1;
					}
					printf("testtcp: sent %d bytes\n\r", (int)sent);
					if (!sent) break;
					payload_sent += stduint(sent);
					sent_total += stduint(sent);
				}
			}
			if (shutdown_write) {
				if (shutdown(fd, SHUT_WR) < 0) {
					printf("testtcp: shutdown write failed\n\r");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				printf("testtcp: shutdown write\n\r");
			}
			if (send_after_shutdown) {
				const stdsint sent = write(fd, "X", 1);
				printf("testtcp: send-after-shutdown=%d\n\r", (int)sent);
				if (show_sockopt) PrintSocketOptions(fd);
				if (sockerr_twice) PrintSocketOptions(fd);
			}
			if (shutdown_read) {
				if (shutdown(fd, SHUT_RD) < 0) {
					printf("testtcp: shutdown read failed\n\r");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				printf("testtcp: shutdown read\n\r");
			}
			char buffer[513] = {};
			stduint received_total = 0;
			while (http_mode || received_total < sent_total) {
				struct pollfd read_pfd{};
				read_pfd.fd = fd;
				read_pfd.events = POLLIN;
				const int read_ready = poll(&read_pfd, 1, 2500);
				if (read_ready < 0) {
					printf("testtcp: poll failed\n\r");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				if (read_ready == 0 || !(read_pfd.revents & POLLIN)) {
					if (!http_mode) printf("testtcp: no data received\n\r");
					break;
				}
				const stdsint received = read(fd, buffer, read_size);
				if (received < 0) {
					printf("testtcp: recv failed\n\r");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				if (!received) break;
				buffer[received] = 0;
				printf("testtcp: recv %d bytes: %s\n\r", (int)received, buffer);
				received_total += stduint(received);
			}
			if (poll_after_recv) {
				struct pollfd pfd{};
				pfd.fd = fd;
				pfd.events = POLLIN | POLLOUT;
				const int ready = poll(&pfd, 1, 1000);
				if (ready < 0) {
					printf("testtcp: poll-after-recv failed\n\r");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				printf("testtcp: poll-after-recv=%d revents=%[16H]\n\r",
					ready, (stduint)pfd.revents);
			}
			if (close(fd) < 0) {
				printf("testtcp: close failed\n\r");
				if (fill_payload) free(fill_payload);
				if (http_payload) free(http_payload);
				return 1;
			}
			printf("testtcp: closed\n\r");
		}
		if (fill_payload) free(fill_payload);
		if (http_payload) free(http_payload);
		return 0;
	}

	if (repeat_count != 1 || burst_count != 1 || fill_length || poll_after_recv || http_mode || positional_count > 1) {
		PrintUsage();
		return 1;
	}
	const int port = positional_count ? atoi(positional[0]) : 80;
	if (port <= 0 || port > 65535) {
		PrintUsage();
		return 1;
	}

	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		printf("testtcp: socket failed\n\r");
		return 1;
	}
	if (show_sockopt) PrintSocketOptions(fd);
	if (reuse_address && !SetReuseAddress(fd)) {
		printf("testtcp: reuseaddr failed\n\r");
		close(fd);
		return 1;
	}
	if (nonblock) {
		const int flags = fcntl(fd, F_GETFL);
		if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
			printf("testtcp: nonblock failed\n\r");
			close(fd);
			return 1;
		}
		printf("testtcp: nonblock=1\n\r");
	}

	struct sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_port = htons((uint16)port);
	local.sin_addr.s_addr = 0;
	if (bind(fd, (const struct sockaddr*)&local, sizeof(local)) < 0) {
		printf("testtcp: bind failed\n\r");
		close(fd);
		return 1;
	}
	if (listen(fd, listen_backlog) < 0) {
		printf("testtcp: listen failed\n\r");
		close(fd);
		return 1;
	}

	struct sockaddr_in bound{};
	socklen_t bound_length = sizeof(bound);
	if (getsockname(fd, (struct sockaddr*)&bound, &bound_length) == 0) {
		PrintSocketAddress("local", bound);
	}
	printf("testtcp: listening backlog=%d\n\r", listen_backlog);
	if (accept_delay > 0) {
		printf("testtcp: accept delay=%dms\n\r", accept_delay);
		poll(nullptr, 0, accept_delay);
	}
	int accepted_total = 0;
	for (;;) {
		if (poll_accept) {
			struct pollfd listen_pfd{};
			listen_pfd.fd = fd;
			listen_pfd.events = POLLIN;
			const int listen_ready = poll(&listen_pfd, 1, 3000);
			if (listen_ready < 0) {
				printf("testtcp: listen poll failed\n\r");
				close(fd);
				return 1;
			}
			printf("testtcp: listen poll=%d revents=%[16H]\n\r",
				listen_ready, (stduint)listen_pfd.revents);
			if (listen_ready == 0) {
				if (nonblock) {
					printf("testtcp: accept would block\n\r");
					break;
				}
				continue;
			}
		}
		struct sockaddr_in peer{};
		socklen_t peer_length = sizeof(peer);
		const int client = accept(fd, (struct sockaddr*)&peer, &peer_length);
		if (client < 0) {
			if (nonblock) {
				printf("testtcp: accept would block\n\r");
				break;
			}
			printf("testtcp: accept failed\n\r");
			return 1;
		}
		accepted_total++;
		PrintSocketAddress("accepted", peer);
		struct pollfd pfd{};
		pfd.fd = client;
		pfd.events = POLLIN;
		const int ready = poll(&pfd, 1, 3000);
		if (ready < 0) {
			printf("testtcp: poll failed\n\r");
			close(client);
			return 1;
		}
		if (ready > 0 && (pfd.revents & POLLIN)) {
			char buffer[513] = {};
			for (;;) {
				const stdsint received = recv(client, buffer, read_size, 0);
				if (received < 0) {
					printf("testtcp: recv failed\n\r");
					close(client);
					return 1;
				}
				if (received == 0) {
					printf("testtcp: eof\n\r");
					break;
				}
				buffer[received] = 0;
				printf("testtcp: recv %d bytes: %s\n\r", (int)received, buffer);
				stduint sent_total = 0;
				while (sent_total < stduint(received)) {
					const stduint remaining = stduint(received) - sent_total;
					const stduint chunk = remaining < stduint(write_size) ? remaining : stduint(write_size);
					const stdsint sent = write(client, buffer + sent_total, chunk);
					if (sent < 0) {
						printf("testtcp: send failed\n\r");
						close(client);
						return 1;
					}
					printf("testtcp: sent %d bytes\n\r", (int)sent);
					if (!sent) break;
					sent_total += stduint(sent);
				}
				pfd.revents = 0;
				const int close_ready = poll(&pfd, 1, 3000);
				if (close_ready < 0) {
					printf("testtcp: eof poll failed\n\r");
					close(client);
					return 1;
				}
				printf("testtcp: eof poll=%d revents=%[16H]\n\r",
					close_ready, (stduint)pfd.revents);
				if (!close_ready || !(pfd.revents & POLLIN)) {
					printf("testtcp: no eof\n\r");
					break;
				}
			}
		}
		else {
			printf("testtcp: no data\n\r");
		}
		close(client);
		printf("testtcp: closed\n\r");
		if (accept_limit > 0 && accepted_total >= accept_limit) break;
	}
	close(fd);
	return 0;
}
