#include "aaaaa.h"
#include <sys/socket.h>
#include <netinet/in.h>

struct MccaSocketAddress {
	uint16 domain;
	uint16 length;
};

struct MccaSocketAddressIPv4 {
	MccaSocketAddress head;
	uint8 address[4];
	uint16 port;
};

static bool SocketAddressFromPosix(MccaSocketAddressIPv4* output,
	const struct sockaddr* address, socklen_t address_length) {
	if (!output || !address || address_length < sizeof(struct sockaddr_in)) return false;
	if (address->sa_family != AF_INET) return false;
	const struct sockaddr_in* ipv4 = (const struct sockaddr_in*)address;
	output->head.domain = AF_INET;
	output->head.length = sizeof(MccaSocketAddressIPv4);
	const uint8* source = (const uint8*)&ipv4->sin_addr.s_addr;
	output->address[0] = source[0];
	output->address[1] = source[1];
	output->address[2] = source[2];
	output->address[3] = source[3];
	output->port = ntohs(ipv4->sin_port);
	return true;
}

static bool SocketAddressToPosix(struct sockaddr* address, socklen_t* address_length,
	const MccaSocketAddressIPv4& input) {
	if (!address || !address_length) return false;
	if (*address_length < sizeof(struct sockaddr_in)) return false;
	struct sockaddr_in* ipv4 = (struct sockaddr_in*)address;
	ipv4->sin_family = AF_INET;
	ipv4->sin_port = htons(input.port);
	uint8* target = (uint8*)&ipv4->sin_addr.s_addr;
	target[0] = input.address[0];
	target[1] = input.address[1];
	target[2] = input.address[2];
	target[3] = input.address[3];
	for0(i, sizeof(ipv4->sin_zero)) ipv4->sin_zero[i] = 0;
	*address_length = sizeof(struct sockaddr_in);
	return true;
}

extern "C" int socket(int domain, int type, int protocol) {
	return (int)syscall(syscall_t::SOCK, (stduint)domain, (stduint)type, (stduint)protocol);
}

extern "C" int bind(int sockfd, const struct sockaddr* address, socklen_t address_length) {
	MccaSocketAddressIPv4 kernel_address{};
	if (!SocketAddressFromPosix(&kernel_address, address, address_length)) return -1;
	return (int)syscall(syscall_t::BIND, (stduint)sockfd, _IMM(&kernel_address), sizeof(kernel_address));
}

extern "C" int connect(int sockfd, const struct sockaddr* address, socklen_t address_length) {
	MccaSocketAddressIPv4 kernel_address{};
	if (!SocketAddressFromPosix(&kernel_address, address, address_length)) return -1;
	return (int)syscall(syscall_t::CONN, (stduint)sockfd, _IMM(&kernel_address), sizeof(kernel_address));
}

extern "C" stdsint send(int sockfd, const void* buffer, size_t length, int flags) {
	syscall_net_send_t request{};
	request.payload = buffer;
	request.length = length;
	return (stdsint)syscall(syscall_t::SEND, (stduint)sockfd, _IMM(&request), (stduint)flags);
}

extern "C" stdsint recv(int sockfd, void* buffer, size_t length, int flags) {
	syscall_net_recv_t request{};
	request.payload = buffer;
	request.capacity = length;
	return (stdsint)syscall(syscall_t::RECV, (stduint)sockfd, _IMM(&request), (stduint)flags);
}

extern "C" stdsint sendto(int sockfd, const void* buffer, size_t length, int flags,
	const struct sockaddr* address, socklen_t address_length) {
	if (!address) return send(sockfd, buffer, length, flags);
	MccaSocketAddressIPv4 kernel_address{};
	if (!SocketAddressFromPosix(&kernel_address, address, address_length)) return -1;
	syscall_net_send_t request{};
	request.payload = buffer;
	request.length = length;
	request.address = &kernel_address;
	request.address_length = sizeof(kernel_address);
	return (stdsint)syscall(syscall_t::SEND, (stduint)sockfd, _IMM(&request), (stduint)flags);
}

extern "C" stdsint recvfrom(int sockfd, void* buffer, size_t length, int flags,
	struct sockaddr* address, socklen_t* address_length) {
	MccaSocketAddressIPv4 kernel_address{};
	stduint kernel_address_length = sizeof(kernel_address);
	syscall_net_recv_t request{};
	request.payload = buffer;
	request.capacity = length;
	if (address && address_length) {
		request.address = &kernel_address;
		request.address_length = &kernel_address_length;
	}
	const stdsint ret = (stdsint)syscall(syscall_t::RECV, (stduint)sockfd, _IMM(&request), (stduint)flags);
	if (ret > 0 && address && address_length) {
		if (!SocketAddressToPosix(address, address_length, kernel_address)) return -1;
	}
	return ret;
}

extern "C" int listen(int sockfd, int backlog) {
	return (int)syscall(syscall_t::LIST, (stduint)sockfd, (stduint)backlog, 0);
}

extern "C" int accept(int sockfd, struct sockaddr* address, socklen_t* address_length) {
	(void)sockfd;
	(void)address;
	(void)address_length;
	return -1;
}

extern "C" int shutdown(int sockfd, int how) {
	(void)sockfd;
	(void)how;
	return -1;
}

extern "C" int getsockname(int sockfd, struct sockaddr* address, socklen_t* address_length) {
	MccaSocketAddressIPv4 kernel_address{};
	stduint kernel_address_length = sizeof(kernel_address);
	syscall_net_socket_address_t request{};
	request.address = &kernel_address;
	request.address_length = &kernel_address_length;
	const int ret = (int)syscall(syscall_t::SADR, (stduint)sockfd,
		_IMM(&request), stduint(syscall_net_socket_address_func_t::Local));
	if (ret < 0) return ret;
	return SocketAddressToPosix(address, address_length, kernel_address) ? 0 : -1;
}

extern "C" int getpeername(int sockfd, struct sockaddr* address, socklen_t* address_length) {
	MccaSocketAddressIPv4 kernel_address{};
	stduint kernel_address_length = sizeof(kernel_address);
	syscall_net_socket_address_t request{};
	request.address = &kernel_address;
	request.address_length = &kernel_address_length;
	const int ret = (int)syscall(syscall_t::SADR, (stduint)sockfd,
		_IMM(&request), stduint(syscall_net_socket_address_func_t::Peer));
	if (ret < 0) return ret;
	return SocketAddressToPosix(address, address_length, kernel_address) ? 0 : -1;
}

extern "C" int setsockopt(int sockfd, int level, int option_name, const void* option_value, socklen_t option_length) {
	syscall_net_socket_option_t request{};
	request.level = (stduint)level;
	request.option_name = (stduint)option_name;
	request.option_value = (void*)option_value;
	request.option_length = option_length;
	return (int)syscall(syscall_t::SOPT, (stduint)sockfd,
		_IMM(&request), stduint(syscall_net_socket_option_func_t::Set));
}

extern "C" int getsockopt(int sockfd, int level, int option_name, void* option_value, socklen_t* option_length) {
	if (!option_length) return -1;
	stduint kernel_option_length = *option_length;
	syscall_net_socket_option_t request{};
	request.level = (stduint)level;
	request.option_name = (stduint)option_name;
	request.option_value = option_value;
	request.option_length = kernel_option_length;
	request.result_length = &kernel_option_length;
	const int ret = (int)syscall(syscall_t::SOPT, (stduint)sockfd,
		_IMM(&request), stduint(syscall_net_socket_option_func_t::Get));
	if (ret < 0) return ret;
	*option_length = (socklen_t)kernel_option_length;
	return 0;
}
