#include "aaaaa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void PrintUsage() {
	printf("usage: testtcp [--reuse] [--nonblock] [--poll-accept] [--backlog n] [--accept-delay ms] [--count n] [--sockopt] --listen [port]\n\r");
	printf("       testtcp [--repeat n] [--burst n] [--fill n] [--read-size n] [--write-size n] [--hold ms] [--poll-after-recv] [--select-after-recv] [--shutdown-write] [--shutdown-read] [--send-after-shutdown] [--sockopt] [--sockerr-twice] [--http] host port [payload]\n\r");
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
	printf("  select: testtcp --select-after-recv 10.0.2.1 7777 hello\n\r");
	printf("  sdown:  testtcp --shutdown-write 10.0.2.1 7777 hello\n\r");
	printf("  rdcls:  testtcp --shutdown-read 10.0.2.1 7777 hello\n\r");
	printf("  opt:    testtcp --sockopt 10.0.2.1 7777 hello\n\r");
	printf("  nbconn: testtcp --nonblock-connect --poll-connect 10.0.2.1 7777 hello\n\r");
	printf("  timeo:  testtcp --rcvtimeo 1000 --sndtimeo 1000 10.0.2.1 7777 hello\n\r");
	printf("  client: testtcp 10.0.2.1 7777 hello\n\r");
	printf("  resolve:testtcp --resolve example.com\n\r");
	printf("  rcached:testtcp --resolve-twice example.com\n\r");
	printf("  dns:    testtcp example.com 80 hello\n\r");
	printf("  http:   testtcp --http example.com 80\n\r");
	printf("  path:   testtcp --http --http-path / example.com 80\n\r");
	printf("  repeat: testtcp --repeat 3 10.0.2.1 7777 hello\n\r");
	printf("  rounds: testtcp --listen 80 --rounds 3\n\r");
	printf("  hold:   testtcp --hold 5000 10.0.2.1 7777 hello\n\r");
	printf("  host:   nc -vz -w 1 10.0.2.15 80\n\r");
	printf("  data:   printf hello | nc -w 1 10.0.2.15 80\n\r");
}

static void PrintSocketAddress(const char* label, const struct sockaddr_in& address) {
	char text[32] = {};
	if (mcca_net_format_sockaddr_ipv4(text, sizeof(text), &address)) {
		printf("testtcp: %s=%s\n\r", label, text);
	}
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
	if (getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, &length) == 0) {
		printf("testtcp: so_reuseaddr=%d\n\r", value);
	}
	length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &value, &length) == 0) {
		printf("testtcp: so_type=%d\n\r", value);
	}
	length = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &value, &length) == 0) {
		printf("testtcp: so_error=%d %s\n\r", value, mcca_net_socket_error_name(value));
	}
	struct timeval timeout{};
	length = sizeof(timeout);
	if (getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, &length) == 0) {
		printf("testtcp: so_rcvtimeo=%ums\n\r",
			(unsigned)(timeout.tv_sec * 1000 + (timeout.tv_usec + 999) / 1000));
	}
	length = sizeof(timeout);
	if (getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, &length) == 0) {
		printf("testtcp: so_sndtimeo=%ums\n\r",
			(unsigned)(timeout.tv_sec * 1000 + (timeout.tv_usec + 999) / 1000));
	}
}

static void PrintSocketError(int fd, const char* label) {
	const int value = mcca_net_get_socket_error(fd);
	if (value >= 0) {
		printf("testtcp: %s=%d %s\n\r", label, value, mcca_net_socket_error_name(value));
	}
}

static bool SetSocketTimeoutOption(int fd, int option_name, int timeout_ms, const char* label) {
	struct timeval timeout{};
	timeout.tv_sec = stduint(timeout_ms / 1000);
	timeout.tv_usec = stduint(timeout_ms % 1000) * 1000;
	if (setsockopt(fd, SOL_SOCKET, option_name, &timeout, sizeof(timeout)) < 0) return false;
	struct timeval check{};
	socklen_t length = sizeof(check);
	if (getsockopt(fd, SOL_SOCKET, option_name, &check, &length) < 0) return false;
	printf("testtcp: %s=%ums\n\r", label,
		(unsigned)(check.tv_sec * 1000 + (check.tv_usec + 999) / 1000));
	return true;
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
	bool select_after_recv = false;
	bool poll_connect = false;
	bool nonblock_connect = false;
	bool show_sockopt = false;
	bool sockerr_twice = false;
	bool shutdown_write = false;
	bool shutdown_read = false;
	bool send_after_shutdown = false;
	bool http_mode = false;
	bool resolve_only = false;
	bool resolve_twice = false;
	int serve_rounds = 0;
	int receive_timeout = -1;
	int send_timeout = -1;
	int hold_time = 0;
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
		if (StrCompare(argv[i], "--poll-connect") == 0) {
			poll_connect = true;
			continue;
		}
		if (StrCompare(argv[i], "--nonblock-connect") == 0) {
			nonblock_connect = true;
			nonblock = true;
			continue;
		}
		if (StrCompare(argv[i], "--select-after-recv") == 0) {
			select_after_recv = true;
			continue;
		}
		if (StrCompare(argv[i], "--rcvtimeo") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			receive_timeout = atoi(argv[i]);
			if (receive_timeout < 0) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--sndtimeo") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			send_timeout = atoi(argv[i]);
			if (send_timeout < 0) {
				PrintUsage();
				return 1;
			}
			continue;
		}
		if (StrCompare(argv[i], "--sockopt") == 0) {
			show_sockopt = true;
			continue;
		}
		if (StrCompare(argv[i], "--hold") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			hold_time = atoi(argv[i]);
			if (hold_time < 0) {
				PrintUsage();
				return 1;
			}
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
		if (StrCompare(argv[i], "--resolve") == 0) {
			resolve_only = true;
			continue;
		}
		if (StrCompare(argv[i], "--resolve-twice") == 0) {
			resolve_only = true;
			resolve_twice = true;
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
		if (StrCompare(argv[i], "--rounds") == 0) {
			if (++i >= argc) {
				PrintUsage();
				return 1;
			}
			serve_rounds = atoi(argv[i]);
			if (serve_rounds <= 0) {
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
		if (resolve_only) {
			if (positional_count != 1 || reuse_address || nonblock || poll_accept || accept_limit ||
				listen_backlog != 4 || accept_delay || repeat_count != 1 || burst_count != 1 ||
				fill_length || poll_after_recv || select_after_recv || poll_connect || nonblock_connect ||
				show_sockopt || sockerr_twice || shutdown_write || shutdown_read || send_after_shutdown ||
				http_mode || serve_rounds || receive_timeout >= 0 || send_timeout >= 0 || hold_time) {
				PrintUsage();
				return 1;
			}
			struct in_addr resolved{};
			const int resolve_count = resolve_twice ? 2 : 1;
			for (int i = 0; i < resolve_count; i++) {
				if (!mcca_net_resolve_ipv4(positional[0], &resolved)) {
					printf("testtcp: resolve failed: %s\n\r", mcca_net_dns_status());
					return 1;
				}
				printf("testtcp: resolve %d/%d target=%s ip=%s status=%s\n\r",
					i + 1, resolve_count, positional[0], inet_ntoa(resolved), mcca_net_dns_status());
				if (mcca_net_dns_ttl()) printf("testtcp: dns ttl=%u\n\r", (unsigned)mcca_net_dns_ttl());
				if (mcca_net_dns_answer_count()) {
					printf("testtcp: dns answers=%u\n\r", (unsigned)mcca_net_dns_answer_count());
				}
				if (mcca_net_dns_cname_count() || mcca_net_dns_non_a_count()) {
					printf("testtcp: dns cname=%u non-a=%u\n\r",
						(unsigned)mcca_net_dns_cname_count(), (unsigned)mcca_net_dns_non_a_count());
				}
			}
			return 0;
		}
		if (reuse_address || poll_accept || accept_limit || listen_backlog != 4 || accept_delay ||
			serve_rounds ||
			(fill_length ? positional_count != 2 : (http_mode ? positional_count != 2 : positional_count != 3))) {
			PrintUsage();
			return 1;
		}
		struct in_addr target_address{};
		uint16 target_port = 0;
		char* fill_payload = nullptr;
		char* http_payload = nullptr;
		const char* payload = fill_length || http_mode ? nullptr : positional[2];
		if (!mcca_net_parse_port(positional[1], &target_port)) {
			PrintUsage();
			return 1;
		}
		if (!mcca_net_resolve_ipv4(positional[0], &target_address)) {
			printf("testtcp: resolve failed: %s\n\r", mcca_net_dns_status());
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
			if (receive_timeout >= 0 && !SetSocketTimeoutOption(fd, SO_RCVTIMEO, receive_timeout, "rcvtimeo")) {
				printf("testtcp: rcvtimeo failed\n\r");
				close(fd);
				if (fill_payload) free(fill_payload);
				if (http_payload) free(http_payload);
				return 1;
			}
			if (send_timeout >= 0 && !SetSocketTimeoutOption(fd, SO_SNDTIMEO, send_timeout, "sndtimeo")) {
				printf("testtcp: sndtimeo failed\n\r");
				close(fd);
				if (fill_payload) free(fill_payload);
				if (http_payload) free(http_payload);
				return 1;
			}
			if (nonblock) {
				const int flags = fcntl(fd, F_GETFL);
				if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
					printf("testtcp: nonblock failed\n\r");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				printf("testtcp: nonblock=1\n\r");
			}
			struct sockaddr_in target{};
			target.sin_family = AF_INET;
			target.sin_port = htons(target_port);
			target.sin_addr = target_address;
			if (connect(fd, (const struct sockaddr*)&target, sizeof(target)) < 0) {
				if (!nonblock_connect) {
					printf("testtcp: connect failed\n\r");
					PrintSocketError(fd, "connect-error");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				printf("testtcp: connect pending\n\r");
			}
			if (nonblock_connect || poll_connect) {
				struct pollfd connect_pfd{};
				connect_pfd.fd = fd;
				connect_pfd.events = POLLOUT;
				const int ready = poll(&connect_pfd, 1, 3500);
				printf("testtcp: connect poll=%d revents=%[16H]\n\r",
					ready, (stduint)connect_pfd.revents);
				PrintSocketError(fd, "connect-error");
				if (ready <= 0 || !(connect_pfd.revents & POLLOUT)) {
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
			}
			struct sockaddr_in local{};
			socklen_t local_length = sizeof(local);
			if (getsockname(fd, (struct sockaddr*)&local, &local_length) == 0) {
				PrintSocketAddress("local", local);
			}
			struct sockaddr_in peer{};
			socklen_t peer_length = sizeof(peer);
			if (getpeername(fd, (struct sockaddr*)&peer, &peer_length) == 0) {
				PrintSocketAddress("peer", peer);
			}
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
			char http_status[80] = {};
			bool http_status_ready = false;
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
					if (read_ready > 0) {
						printf("testtcp: recv poll revents=%[16H]\n\r", (stduint)read_pfd.revents);
						PrintSocketError(fd, "recv-error");
					}
					if (!http_mode) printf("testtcp: no data received\n\r");
					break;
				}
				const stdsint received = read(fd, buffer, read_size);
				if (received < 0) {
					printf("testtcp: recv failed\n\r");
					PrintSocketError(fd, "recv-error");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				if (!received) {
					printf("testtcp: eof\n\r");
					break;
				}
				buffer[received] = 0;
				if (http_mode && !http_status_ready) {
					stduint line_length = 0;
					while (line_length < stduint(received) && line_length + 1 < sizeof(http_status) &&
						buffer[line_length] != '\r' && buffer[line_length] != '\n') {
						http_status[line_length] = buffer[line_length];
						line_length++;
					}
					http_status[line_length] = 0;
					http_status_ready = line_length != 0;
				}
				printf("testtcp: recv %d bytes: %s\n\r", (int)received, buffer);
				received_total += stduint(received);
			}
			if (http_mode) {
				if (http_status_ready) printf("testtcp: http status: %s\n\r", http_status);
				printf("testtcp: http bytes=%u\n\r", (unsigned)received_total);
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
			if (select_after_recv) {
				fd_set readfds;
				fd_set writefds;
				fd_set exceptfds;
				FD_ZERO(&readfds);
				FD_ZERO(&writefds);
				FD_ZERO(&exceptfds);
				FD_SET(fd, &readfds);
				FD_SET(fd, &writefds);
				FD_SET(fd, &exceptfds);
				struct timeval timeout{};
				timeout.tv_sec = 1;
				timeout.tv_usec = 0;
				const int ready = select(fd + 1, &readfds, &writefds, &exceptfds, &timeout);
				if (ready < 0) {
					printf("testtcp: select-after-recv failed\n\r");
					close(fd);
					if (fill_payload) free(fill_payload);
					if (http_payload) free(http_payload);
					return 1;
				}
				printf("testtcp: select-after-recv=%d read=%d write=%d except=%d\n\r",
					ready, FD_ISSET(fd, &readfds) ? 1 : 0,
					FD_ISSET(fd, &writefds) ? 1 : 0,
					FD_ISSET(fd, &exceptfds) ? 1 : 0);
			}
			if (hold_time > 0) {
				printf("testtcp: hold=%dms\n\r", hold_time);
				poll(nullptr, 0, hold_time);
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

	if (repeat_count != 1 || burst_count != 1 || fill_length || poll_after_recv || select_after_recv ||
		poll_connect || nonblock_connect || http_mode || resolve_only || positional_count > 1) {
		PrintUsage();
		return 1;
	}
	uint16 port = 80;
	if (positional_count && !mcca_net_parse_port(positional[0], &port)) {
		PrintUsage();
		return 1;
	}

	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		printf("testtcp: socket failed\n\r");
		return 1;
	}
	if (show_sockopt) PrintSocketOptions(fd);
	if (receive_timeout >= 0 && !SetSocketTimeoutOption(fd, SO_RCVTIMEO, receive_timeout, "rcvtimeo")) {
		printf("testtcp: rcvtimeo failed\n\r");
		close(fd);
		return 1;
	}
	if (send_timeout >= 0 && !SetSocketTimeoutOption(fd, SO_SNDTIMEO, send_timeout, "sndtimeo")) {
		printf("testtcp: sndtimeo failed\n\r");
		close(fd);
		return 1;
	}
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
	if (show_sockopt) {
		struct sockaddr_in listen_peer{};
		socklen_t listen_peer_length = sizeof(listen_peer);
		if (getpeername(fd, (struct sockaddr*)&listen_peer, &listen_peer_length) < 0) {
			printf("testtcp: listen peer=none\n\r");
		}
	}
	printf("testtcp: listening backlog=%d\n\r", listen_backlog);
	if (accept_delay > 0) {
		printf("testtcp: accept delay=%dms\n\r", accept_delay);
		poll(nullptr, 0, accept_delay);
	}
	if (accept_limit == 0) accept_limit = 1;
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
			int served_rounds = 0;
			for (;;) {
				const stdsint received = recv(client, buffer, read_size, 0);
				if (received < 0) {
					printf("testtcp: recv failed\n\r");
					PrintSocketError(client, "recv-error");
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
				served_rounds++;
				if (serve_rounds > 0 && served_rounds >= serve_rounds) {
					printf("testtcp: rounds=%d\n\r", served_rounds);
					break;
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
					if (serve_rounds > 0) {
						printf("testtcp: wait next round %d/%d\n\r", served_rounds, serve_rounds);
						continue;
					}
					printf("testtcp: eof wait timeout\n\r");
					break;
				}
			}
		}
		else {
			printf("testtcp: no data revents=%[16H]\n\r", (stduint)pfd.revents);
			PrintSocketError(client, "recv-error");
		}
		if (hold_time > 0) {
			printf("testtcp: hold=%dms\n\r", hold_time);
			poll(nullptr, 0, hold_time);
		}
		close(client);
		printf("testtcp: closed\n\r");
		if (accept_limit > 0 && accepted_total >= accept_limit) break;
	}
	close(fd);
	return 0;
}
