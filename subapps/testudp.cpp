#include "aaaaa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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
	printf("usage: testudp [--dontwait] [--nonblock] [--poll] [--reuse] [--listen] [ipv4] [port] [payload] [conn]\n\r");
	printf("  default: testudp 10.0.2.1 7 mecocoa\n\r");
	printf("  sendto:  testudp 10.0.2.1 7777 mecocoa\n\r");
	printf("  conn:    testudp 10.0.2.1 7777 mecocoa conn\n\r");
	printf("  nowait:  testudp --dontwait 10.0.2.1 7777 mecocoa\n\r");
	printf("  nbread:  testudp --nonblock 10.0.2.1 7777 mecocoa conn\n\r");
	printf("  poll:    testudp --poll 10.0.2.1 7777 mecocoa\n\r");
	printf("  listen:  testudp --reuse --listen 7777\n\r");
}

static void PrintSocketAddress(const char* label, const struct sockaddr_in& address) {
	const uint8* octet = (const uint8*)&address.sin_addr.s_addr;
	printf("testudp: %s=%u.%u.%u.%u:%u\n\r", label,
		(unsigned)octet[0], (unsigned)octet[1], (unsigned)octet[2], (unsigned)octet[3],
		(unsigned)ntohs(address.sin_port));
}

static void PrintSocketNames(int fd, bool peer) {
	struct sockaddr_in local{};
	socklen_t local_length = sizeof(local);
	if (getsockname(fd, (struct sockaddr*)&local, &local_length) == 0) {
		PrintSocketAddress("local", local);
	}
	if (peer) {
		struct sockaddr_in remote{};
		socklen_t remote_length = sizeof(remote);
		if (getpeername(fd, (struct sockaddr*)&remote, &remote_length) == 0) {
			PrintSocketAddress("peer", remote);
		}
	}
}

static bool SetReuseAddress(int fd) {
	int enable = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) return false;
	int value = 0;
	socklen_t length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, &length) < 0) return false;
	printf("testudp: reuseaddr=%d\n\r", value);
	return value != 0;
}

int main(int argc, char** argv) {
	if (argc >= 2 && (!StrCompare(argv[1], "-h") || !StrCompare(argv[1], "--help") ||
		!StrCompare(argv[1], "help"))) {
		PrintUsage();
		return 0;
	}
	const char* positional[3] = {};
	stduint positional_count = 0;
	bool use_connect = false;
	bool dont_wait = false;
	bool nonblock = false;
	bool listen_mode = false;
	bool reuse_address = false;
	bool use_poll = false;
	for (int i = 1; i < argc; i++) {
		if (StrCompare(argv[i], "conn") == 0) {
			use_connect = true;
			continue;
		}
		if (StrCompare(argv[i], "--dontwait") == 0) {
			dont_wait = true;
			continue;
		}
		if (StrCompare(argv[i], "--nonblock") == 0) {
			nonblock = true;
			continue;
		}
		if (StrCompare(argv[i], "--listen") == 0) {
			listen_mode = true;
			continue;
		}
		if (StrCompare(argv[i], "--reuse") == 0) {
			reuse_address = true;
			continue;
		}
		if (StrCompare(argv[i], "--poll") == 0) {
			use_poll = true;
			continue;
		}
		if (positional_count >= 3) {
			PrintUsage();
			return 1;
		}
		positional[positional_count++] = argv[i];
	}
	const char* target_text = positional_count >= 1 ? positional[0] : "10.0.2.1";
	const int target_port = positional_count >= 2 ? atoi(positional[1]) : 7;
	const char* payload = positional_count >= 3 ? positional[2] : "mecocoa";
	const int listen_port = positional_count >= 1 ? atoi(positional[0]) : 7;

	in_addr_t target_ip = 0;
	if (!listen_mode && (!ParseIPv4(target_text, &target_ip) || target_port <= 0 || target_port > 65535)) {
		PrintUsage();
		return 1;
	}
	if (listen_mode && (listen_port <= 0 || listen_port > 65535 || positional_count > 1 || use_connect)) {
		PrintUsage();
		return 1;
	}

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		printf("testudp: socket failed\n\r");
		return 1;
	}
	if (nonblock) {
		const int flags = fcntl(fd, F_GETFL);
		if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
			printf("testudp: fcntl failed\n\r");
			close(fd);
			return 1;
		}
	}
	if (reuse_address && !SetReuseAddress(fd)) {
		printf("testudp: reuseaddr failed\n\r");
		close(fd);
		return 1;
	}

	if (listen_mode) {
		struct sockaddr_in local{};
		local.sin_family = AF_INET;
		local.sin_port = htons((uint16)listen_port);
		local.sin_addr.s_addr = 0;
		if (bind(fd, (const struct sockaddr*)&local, sizeof(local)) < 0) {
			printf("testudp: bind failed\n\r");
			close(fd);
			return 1;
		}
		PrintSocketNames(fd, false);

		char buffer[1500] = {};
		struct sockaddr_in source{};
		socklen_t source_length = sizeof(source);
		const int receive_flags = dont_wait ? MSG_DONTWAIT : 0;
		const stdsint received = recvfrom(fd, buffer, sizeof(buffer), receive_flags,
			(struct sockaddr*)&source, &source_length);
		if (received < 0) {
			printf("testudp: recv failed\n\r");
			close(fd);
			return 1;
		}
		if (received == 0) {
			printf("testudp: no packet received\n\r");
			close(fd);
			return 2;
		}
		PrintSocketAddress("source", source);
		const stdsint sent = sendto(fd, buffer, (size_t)received, 0,
			(const struct sockaddr*)&source, source_length);
		if (sent < 0) {
			printf("testudp: send failed\n\r");
			close(fd);
			return 1;
		}
		printf("testudp: echoed %d bytes\n\r", (int)sent);
		close(fd);
		return 0;
	}

	struct sockaddr_in target{};
	target.sin_family = AF_INET;
	target.sin_port = htons((uint16)target_port);
	target.sin_addr.s_addr = htonl(target_ip);

	const size_t payload_length = strlen(payload);
	stdsint sent = 0;
	if (use_connect) {
		if (connect(fd, (const struct sockaddr*)&target, sizeof(target)) < 0) {
			printf("testudp: connect failed\n\r");
			close(fd);
			return 1;
		}
		sent = write(fd, payload, payload_length);
	}
	else {
		sent = sendto(fd, payload, payload_length, 0,
			(const struct sockaddr*)&target, sizeof(target));
	}
	if (sent < 0) {
		printf("testudp: send failed\n\r");
		close(fd);
		return 1;
	}
	printf("testudp: sent %d bytes to %s:%d via %s\n\r",
		(int)sent, target_text, target_port, use_connect ? "write" : "sendto");
	PrintSocketNames(fd, use_connect);

	char buffer[128] = {};
	struct sockaddr_in source{};
	socklen_t source_length = sizeof(source);
	stdsint received = 0;
	const bool no_wait_receive = dont_wait || nonblock;
	const stduint receive_attempts = no_wait_receive ? 1 : 50;
	const int receive_flags = dont_wait ? MSG_DONTWAIT : 0;
	for0(i, receive_attempts) {
		if (use_poll) {
			struct pollfd pfd{};
			pfd.fd = fd;
			pfd.events = POLLIN;
			const int poll_ready = poll(&pfd, 1, 0);
			if (poll_ready < 0) {
				printf("testudp: poll failed\n\r");
				close(fd);
				return 1;
			}
			if (poll_ready == 0 || !(pfd.revents & POLLIN)) {
				if (!no_wait_receive) {
					sysrest(1, 50);
					continue;
				}
			}
			printf("testudp: poll=%d revents=%04x\n\r", poll_ready, (unsigned)pfd.revents);
		}
		if (use_connect) {
			received = dont_wait ? recv(fd, buffer, sizeof(buffer) - 1, receive_flags) :
				read(fd, buffer, sizeof(buffer) - 1);
		}
		else {
			source_length = sizeof(source);
			received = recvfrom(fd, buffer, sizeof(buffer) - 1, receive_flags,
				(struct sockaddr*)&source, &source_length);
		}
		if (received != 0) break;
		if (!no_wait_receive) sysrest(1, 50);
	}
	if (received < 0) {
		printf("testudp: recv failed\n\r");
		close(fd);
		return 1;
	}
	if (received == 0) {
		printf("testudp: no packet received\n\r");
		close(fd);
		return 2;
	}

	buffer[received] = 0;
	printf("testudp: recv %d bytes via %s: %s\n\r",
		(int)received, use_connect ? (dont_wait ? "recv" : "read") : "recvfrom", buffer);
	close(fd);
	return 0;
}
