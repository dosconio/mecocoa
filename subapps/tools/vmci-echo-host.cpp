// Build in a Windows Developer Command Prompt: cl /EHsc vmci-echo-host.cpp ws2_32.lib

// cl /nologo /EHsc vmci-echo-host.cpp ws2_32.lib

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
	constexpr wchar_t VmciDevice[] = L"\\\\.\\VMCI";
	constexpr DWORD VmciGetAddressFamily = 0x81032068u;
	constexpr DWORD VmciGetLocalCid = 0x8103206Cu;
	constexpr unsigned int VmciAnyId = 0xFFFFFFFFu;
	constexpr unsigned int DefaultPort = 7777;
	constexpr int BufferCapacity = 4096;
	constexpr int VmciBufferSizeOption = 0;
	constexpr int VmciBufferMinSizeOption = 1;
	constexpr int VmciBufferMaxSizeOption = 2;

	struct SockaddrVm {
		ADDRESS_FAMILY family;
		unsigned short reserved;
		unsigned int port;
		unsigned int cid;
		unsigned char zero[sizeof(sockaddr) - sizeof(ADDRESS_FAMILY) -
			sizeof(unsigned short) - 2 * sizeof(unsigned int)];
	};

	static_assert(sizeof(SockaddrVm) == sizeof(sockaddr),
		"VMCI socket address size mismatch");

	unsigned int ReadVmciValue(DWORD command) {
		unsigned int value = VmciAnyId;
		HANDLE device = CreateFileW(VmciDevice, GENERIC_READ, 0, nullptr,
			OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (device == INVALID_HANDLE_VALUE) return value;
		DWORD returned = 0;
		if (!DeviceIoControl(device, command, &value, sizeof(value),
			&value, sizeof(value), &returned, nullptr)) value = VmciAnyId;
		CloseHandle(device);
		return value;
	}

	bool ParsePort(const char* text, unsigned int& port) {
		char* end = nullptr;
		const unsigned long parsed = std::strtoul(text, &end, 10);
		if (!end || *end || parsed < 1024 || parsed == VmciAnyId) return false;
		port = static_cast<unsigned int>(parsed);
		return true;
	}

	bool SendAll(SOCKET socket_handle, const char* buffer, int length) {
		int offset = 0;
		while (offset < length) {
			const int sent = send(socket_handle, buffer + offset,
				length - offset, 0);
			if (sent == SOCKET_ERROR || sent == 0) return false;
			offset += sent;
		}
		return true;
	}

	bool ConfigureStreamBuffer(SOCKET socket_handle,
		unsigned int address_family) {
		const unsigned long long size = BufferCapacity;
		const int options[] = {
			VmciBufferMinSizeOption,
			VmciBufferMaxSizeOption,
			VmciBufferSizeOption,
		};
		for (int option : options) {
			if (setsockopt(socket_handle, int(address_family), option,
				reinterpret_cast<const char*>(&size), sizeof(size)) ==
				SOCKET_ERROR) {
				std::printf("vmci-echo-host: stream buffer option=%d failed error=%d\n",
					option, WSAGetLastError());
				return false;
			}
		}
		for (int option : options) {
			unsigned long long actual = 0;
			int length = sizeof(actual);
			const int read_result = getsockopt(socket_handle,
				int(address_family), option,
				reinterpret_cast<char*>(&actual), &length);
			if (read_result == SOCKET_ERROR ||
				length != sizeof(actual) || actual != size) {
				std::printf("vmci-echo-host: stream buffer option=%d value=%llu expected=%llu error=%d\n",
					option, actual, size, read_result == SOCKET_ERROR ?
						WSAGetLastError() : 0);
				return false;
			}
		}
		return true;
	}

	int RunDatagramServer(SOCKET socket_handle) {
		char buffer[BufferCapacity];
		for (;;) {
			SockaddrVm peer = {};
			int peer_length = sizeof(peer);
			const int received = recvfrom(socket_handle, buffer, sizeof(buffer), 0,
				reinterpret_cast<sockaddr*>(&peer), &peer_length);
			if (received == SOCKET_ERROR) {
				std::printf("vmci-echo-host: recvfrom failed error=%d\n",
					WSAGetLastError());
				return 1;
			}
			std::printf("vmci-echo-host: recv cid=%08x port=%u bytes=%d\n",
				peer.cid, peer.port, received);
			const int sent = sendto(socket_handle, buffer, received, 0,
				reinterpret_cast<const sockaddr*>(&peer), peer_length);
			if (sent != received) {
				std::printf("vmci-echo-host: sendto failed sent=%d error=%d\n",
					sent, WSAGetLastError());
				return 1;
			}
		}
	}

	int RunStreamServer(SOCKET socket_handle) {
		if (listen(socket_handle, SOMAXCONN) == SOCKET_ERROR) {
			std::printf("vmci-echo-host: listen failed error=%d\n",
				WSAGetLastError());
			return 1;
		}
		for (;;) {
			SockaddrVm peer = {};
			int peer_length = sizeof(peer);
			SOCKET client = accept(socket_handle,
				reinterpret_cast<sockaddr*>(&peer), &peer_length);
			if (client == INVALID_SOCKET) {
				std::printf("vmci-echo-host: accept failed error=%d\n",
					WSAGetLastError());
				return 1;
			}
			std::printf("vmci-echo-host: stream connected cid=%08x port=%u\n",
				peer.cid, peer.port);
			char buffer[BufferCapacity];
			for (;;) {
				const int received = recv(client, buffer, sizeof(buffer), 0);
				if (received == 0) {
					std::printf("vmci-echo-host: stream closed cid=%08x port=%u\n",
						peer.cid, peer.port);
					break;
				}
				if (received == SOCKET_ERROR) {
					std::printf("vmci-echo-host: recv failed error=%d\n",
						WSAGetLastError());
					break;
				}
				std::printf("vmci-echo-host: stream recv cid=%08x port=%u bytes=%d\n",
					peer.cid, peer.port, received);
				if (!SendAll(client, buffer, received)) {
					std::printf("vmci-echo-host: send failed error=%d\n",
						WSAGetLastError());
					break;
				}
			}
			closesocket(client);
		}
	}
}

int main(int argc, char** argv) {
	unsigned int port = DefaultPort;
	bool stream = false;
	int argument = 1;
	if (argument < argc && std::strcmp(argv[argument], "--stream") == 0) {
		stream = true;
		argument++;
	}
	if (argc - argument > 1) {
		std::printf("usage: vmci-echo-host [--stream] [port]\n");
		return 1;
	}
	if (argument < argc) {
		if (!ParsePort(argv[argument], port)) {
			std::printf("vmci-echo-host: invalid port=%s\n", argv[argument]);
			return 1;
		}
	}

	WSADATA winsock = {};
	if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
		std::printf("vmci-echo-host: WSAStartup failed\n");
		return 1;
	}

	const unsigned int address_family = ReadVmciValue(VmciGetAddressFamily);
	const unsigned int local_cid = ReadVmciValue(VmciGetLocalCid);
	if (address_family == VmciAnyId || local_cid == VmciAnyId) {
		std::printf("vmci-echo-host: VMware VMCI socket provider unavailable\n");
		WSACleanup();
		return 1;
	}

	SOCKET socket_handle = socket(int(address_family),
		stream ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (socket_handle == INVALID_SOCKET) {
		std::printf("vmci-echo-host: socket failed error=%d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}
	if (stream && !ConfigureStreamBuffer(socket_handle, address_family)) {
		closesocket(socket_handle);
		WSACleanup();
		return 1;
	}

	SockaddrVm local = {};
	local.family = ADDRESS_FAMILY(address_family);
	local.cid = VmciAnyId;
	local.port = port;
	if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&local),
		sizeof(local)) == SOCKET_ERROR) {
		std::printf("vmci-echo-host: bind port=%u failed error=%d\n",
			port, WSAGetLastError());
		closesocket(socket_handle);
		WSACleanup();
		return 1;
	}

	std::printf("vmci-echo-host: listening %s cid=%u port=%u af=%u\n",
		stream ? "stream" : "datagram", local_cid, port, address_family);
	const int result = stream ? RunStreamServer(socket_handle) :
		RunDatagramServer(socket_handle);

	closesocket(socket_handle);
	WSACleanup();
	return result;
}
