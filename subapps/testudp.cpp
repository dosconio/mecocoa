#include "aaaaa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
	printf("usage: testudp [ipv4] [port] [payload] [conn]\n\r");
	printf("  default: testudp 10.0.2.1 7 mecocoa\n\r");
	printf("  sendto:  testudp 10.0.2.1 7777 mecocoa\n\r");
	printf("  conn:    testudp 10.0.2.1 7777 mecocoa conn\n\r");
}

int main(int argc, char** argv) {
	if (argc >= 2 && (!StrCompare(argv[1], "-h") || !StrCompare(argv[1], "--help") ||
		!StrCompare(argv[1], "help"))) {
		PrintUsage();
		return 0;
	}
	const char* target_text = argc >= 2 ? argv[1] : "10.0.2.1";
	const int target_port = argc >= 3 ? atoi(argv[2]) : 7;
	const char* payload = argc >= 4 ? argv[3] : "mecocoa";
	const bool use_connect = argc >= 5 && StrCompare(argv[4], "conn") == 0;

	in_addr_t target_ip = 0;
	if (!ParseIPv4(target_text, &target_ip) || target_port <= 0 || target_port > 65535) {
		PrintUsage();
		return 1;
	}

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		printf("testudp: socket failed\n\r");
		return 1;
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

	char buffer[128] = {};
	struct sockaddr_in source{};
	socklen_t source_length = sizeof(source);
	stdsint received = 0;
	for0(i, 50) {
		if (use_connect) {
			received = read(fd, buffer, sizeof(buffer) - 1);
		}
		else {
			source_length = sizeof(source);
			received = recvfrom(fd, buffer, sizeof(buffer) - 1, 0,
				(struct sockaddr*)&source, &source_length);
		}
		if (received != 0) break;
		sysrest(1, 50);
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
		(int)received, use_connect ? "read" : "recvfrom", buffer);
	close(fd);
	return 0;
}
