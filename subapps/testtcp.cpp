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
	printf("       testtcp ipv4 port payload\n\r");
	printf("  listen: testtcp --listen 80\n\r");
	printf("  reuse:  testtcp --reuse --listen 80\n\r");
	printf("  count:  testtcp --listen 80 --count 3\n\r");
	printf("  client: testtcp 10.0.2.1 7777 hello\n\r");
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
		if (positional_count >= 3) {
			PrintUsage();
			return 1;
		}
		positional[positional_count++] = argv[i];
	}

	if (!listen_mode) {
		if (reuse_address || accept_limit || positional_count != 3) {
			PrintUsage();
			return 1;
		}
		in_addr_t target_ip = 0;
		const int target_port = atoi(positional[1]);
		const char* payload = positional[2];
		if (!ParseIPv4(positional[0], &target_ip) || target_port <= 0 || target_port > 65535) {
			PrintUsage();
			return 1;
		}
		int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (fd < 0) {
			printf("testtcp: socket failed\n\r");
			return 1;
		}
		struct sockaddr_in target{};
		target.sin_family = AF_INET;
		target.sin_port = htons((uint16)target_port);
		target.sin_addr.s_addr = htonl(target_ip);
		if (connect(fd, (const struct sockaddr*)&target, sizeof(target)) < 0) {
			printf("testtcp: connect failed\n\r");
			close(fd);
			return 1;
		}
		struct sockaddr_in local{};
		socklen_t local_length = sizeof(local);
		if (getsockname(fd, (struct sockaddr*)&local, &local_length) == 0) {
			PrintSocketAddress("local", local);
		}
		PrintSocketAddress("peer", target);
		const size_t payload_length = strlen(payload);
		const stdsint sent = write(fd, payload, payload_length);
		if (sent < 0) {
			printf("testtcp: send failed\n\r");
			close(fd);
			return 1;
		}
		printf("testtcp: sent %d bytes\n\r", (int)sent);
		char buffer[513] = {};
		const stdsint received = read(fd, buffer, sizeof(buffer) - 1);
		if (received < 0) {
			printf("testtcp: recv failed\n\r");
			close(fd);
			return 1;
		}
		buffer[received] = 0;
		printf("testtcp: recv %d bytes: %s\n\r", (int)received, buffer);
		close(fd);
		return 0;
	}

	if (positional_count > 1) {
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
			const stdsint received = recv(client, buffer, sizeof(buffer) - 1, 0);
			if (received < 0) {
				printf("testtcp: recv failed\n\r");
				close(client);
				return 1;
			}
			buffer[received] = 0;
			printf("testtcp: recv %d bytes: %s\n\r", (int)received, buffer);
			const stdsint sent = write(client, buffer, received);
			if (sent < 0) {
				printf("testtcp: send failed\n\r");
				close(client);
				return 1;
			}
			printf("testtcp: sent %d bytes\n\r", (int)sent);
			pfd.revents = 0;
			const int close_ready = poll(&pfd, 1, 3000);
			if (close_ready < 0) {
				printf("testtcp: eof poll failed\n\r");
				close(client);
				return 1;
			}
			if (close_ready > 0 && (pfd.revents & POLLIN)) {
				const stdsint closed = recv(client, buffer, sizeof(buffer) - 1, 0);
				if (closed < 0) {
					printf("testtcp: eof recv failed\n\r");
					close(client);
					return 1;
				}
				if (closed == 0) {
					printf("testtcp: eof\n\r");
				}
				else {
					buffer[closed] = 0;
					printf("testtcp: recv %d bytes after echo: %s\n\r", (int)closed, buffer);
				}
			}
			else {
				printf("testtcp: no eof\n\r");
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
