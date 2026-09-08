#include "aaaaa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>

static bool ParseIPv4(const char* text, in_addr_t* output) {
	if (!text || !output) return false;
	uint8 octets[4] = {};
	const char* cursor = text;
	for0(i, 4) {
		if (*cursor < '0' || *cursor > '9') return false;
		int value = 0;
		while (*cursor >= '0' && *cursor <= '9') {
			value = value * 10 + (*cursor - '0');
			if (value > 255) return false;
			cursor++;
		}
		octets[i] = (uint8)value;
		if (i < 3) {
			if (*cursor != '.') return false;
			cursor++;
		}
	}
	if (*cursor) return false;
	*output = ((in_addr_t)octets[0] << 24) |
		((in_addr_t)octets[1] << 16) |
		((in_addr_t)octets[2] << 8) |
		((in_addr_t)octets[3]);
	return true;
}

static void PrintUsage() {
	printf("usage: testtcp [--reuse] [--count n] --listen [port]\n\r");
	printf("       testtcp [--repeat n] [--burst n] [--fill n] [--read-size n] [--write-size n] ipv4 port [payload]\n\r");
	printf("  listen: testtcp --listen 80\n\r");
	printf("  reuse:  testtcp --reuse --listen 80\n\r");
	printf("  count:  testtcp --listen 80 --count 3\n\r");
	printf("  burst:  testtcp --burst 3 10.0.2.1 7777 hello\n\r");
	printf("  fill:   testtcp --fill 1500 10.0.2.1 7777\n\r");
	printf("  read:   testtcp --read-size 2 10.0.2.1 7777 hello\n\r");
	printf("  write:  testtcp --write-size 2 10.0.2.1 7777 hello\n\r");
	printf("  client: testtcp 10.0.2.1 7777 hello\n\r");
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

int main(int argc, char** argv) {
	if (argc >= 2 && (!StrCompare(argv[1], "-h") || !StrCompare(argv[1], "--help") ||
		!StrCompare(argv[1], "help"))) {
		PrintUsage();
		return 0;
	}

	bool listen_mode = false;
	bool reuse_address = false;
	int accept_limit = 0;
	int repeat_count = 1;
	int burst_count = 1;
	int read_size = 512;
	int write_size = 512;
	int fill_length = 0;
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
		if (positional_count >= 3) {
			PrintUsage();
			return 1;
		}
		positional[positional_count++] = argv[i];
	}

	if (!listen_mode) {
		if (reuse_address || accept_limit ||
			(fill_length ? positional_count != 2 : positional_count != 3)) {
			PrintUsage();
			return 1;
		}
		in_addr_t target_ip = 0;
		const int target_port = atoi(positional[1]);
		char* fill_payload = nullptr;
		const char* payload = fill_length ? nullptr : positional[2];
		if (!ParseIPv4(positional[0], &target_ip) || target_port <= 0 || target_port > 65535) {
			PrintUsage();
			return 1;
		}
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
		for (int repeat = 0; repeat < repeat_count; repeat++) {
			if (repeat_count > 1) printf("testtcp: repeat %d/%d\n\r", repeat + 1, repeat_count);
			int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0) {
				printf("testtcp: socket failed\n\r");
				if (fill_payload) free(fill_payload);
				return 1;
			}
			struct sockaddr_in target{};
			target.sin_family = AF_INET;
			target.sin_port = htons((uint16)target_port);
			target.sin_addr.s_addr = htonl(target_ip);
			if (connect(fd, (const struct sockaddr*)&target, sizeof(target)) < 0) {
				printf("testtcp: connect failed\n\r");
				close(fd);
				if (fill_payload) free(fill_payload);
				return 1;
			}
			struct sockaddr_in local{};
			socklen_t local_length = sizeof(local);
			if (getsockname(fd, (struct sockaddr*)&local, &local_length) == 0) {
				PrintSocketAddress("local", local);
			}
			PrintSocketAddress("peer", target);
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
						return 1;
					}
					printf("testtcp: sent %d bytes\n\r", (int)sent);
					if (!sent) break;
					payload_sent += stduint(sent);
					sent_total += stduint(sent);
				}
			}
			char buffer[513] = {};
			stduint received_total = 0;
			while (received_total < sent_total) {
				const stdsint received = read(fd, buffer, read_size);
				if (received < 0) {
					printf("testtcp: recv failed\n\r");
					close(fd);
					if (fill_payload) free(fill_payload);
					return 1;
				}
				if (!received) break;
				buffer[received] = 0;
				printf("testtcp: recv %d bytes: %s\n\r", (int)received, buffer);
				received_total += stduint(received);
			}
			if (close(fd) < 0) {
				printf("testtcp: close failed\n\r");
				if (fill_payload) free(fill_payload);
				return 1;
			}
			printf("testtcp: closed\n\r");
		}
		if (fill_payload) free(fill_payload);
		return 0;
	}

	if (repeat_count != 1 || burst_count != 1 || fill_length || positional_count > 1) {
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
	if (reuse_address && !SetReuseAddress(fd)) {
		printf("testtcp: reuseaddr failed\n\r");
		close(fd);
		return 1;
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
	if (listen(fd, 4) < 0) {
		printf("testtcp: listen failed\n\r");
		close(fd);
		return 1;
	}

	struct sockaddr_in bound{};
	socklen_t bound_length = sizeof(bound);
	if (getsockname(fd, (struct sockaddr*)&bound, &bound_length) == 0) {
		PrintSocketAddress("local", bound);
	}
	printf("testtcp: listening backlog=4\n\r");
	int accepted_total = 0;
	for (;;) {
		struct sockaddr_in peer{};
		socklen_t peer_length = sizeof(peer);
		const int client = accept(fd, (struct sockaddr*)&peer, &peer_length);
		if (client < 0) {
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
