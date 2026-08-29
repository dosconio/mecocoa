// ASCII g++ TAB4 LF
// ModuTitle: Intel e1000 Ring1 Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/stdinc.h>
#include "../../include/taskman.com.hpp"
#include "../../include/syscall-pow.hpp"

#if defined(_ACCM) && ((_ACCM & 0xFF00) == 0x8600)

namespace {
	constexpr uint16 E1000VendorId = 0x8086u;
	constexpr uint16 E1000DeviceIds[] = {
		0x100Eu,
		0x100Fu,
		0x1010u,
		0x10D3u,
	};

	enum class E1000Reg : uint32 {
		CTRL = 0x0000u / 4,
		STATUS = 0x0008u / 4,
		EECD = 0x0010u / 4,
		ICR = 0x00C0u / 4,
		IMC = 0x00D8u / 4,
		RCTL = 0x0100u / 4,
		TCTL = 0x0400u / 4,
		TIPG = 0x0410u / 4,
		RDBAL = 0x2800u / 4,
		RDBAH = 0x2804u / 4,
		RDLEN = 0x2808u / 4,
		RDH = 0x2810u / 4,
		RDT = 0x2818u / 4,
		TDBAL = 0x3800u / 4,
		TDBAH = 0x3804u / 4,
		TDLEN = 0x3808u / 4,
		TDH = 0x3810u / 4,
		TDT = 0x3818u / 4,
		MTA = 0x5200u / 4,
		RAL0 = 0x5400u / 4,
		RAH0 = 0x5404u / 4,
	};

	constexpr uint32 E1000_CTRL_RST = 1u << 26;
	constexpr uint32 E1000_CTRL_SLU = 1u << 6;
	constexpr uint32 E1000_STATUS_LU = 1u << 1;
	constexpr uint32 E1000_RCTL_EN = 1u << 1;
	constexpr uint32 E1000_RCTL_BAM = 1u << 15;
	constexpr uint32 E1000_RCTL_SECRC = 1u << 26;
	constexpr uint32 E1000_TCTL_EN = 1u << 1;
	constexpr uint32 E1000_TCTL_PSP = 1u << 3;
	constexpr uint8 E1000_RXD_STAT_DD = 1u << 0;
	constexpr uint8 E1000_TXD_STAT_DD = 1u << 0;
	constexpr uint8 E1000_TXD_CMD_EOP = 1u << 0;
	constexpr uint8 E1000_TXD_CMD_IFCS = 1u << 1;
	constexpr uint8 E1000_TXD_CMD_RS = 1u << 3;

	constexpr stduint E1000_RX_DESC_COUNT = 32;
	constexpr stduint E1000_TX_DESC_COUNT = 16;
	constexpr stduint E1000_FRAME_BUF_SIZE = 2048;
	constexpr stduint E1000_ETHERNET_MIN_FRAME = 60;
	constexpr stduint E1000_ETHERNET_MAX_FRAME = 1518;
	constexpr stduint E1000_TX_WAIT_SPINS = 100000;

	_PACKED(struct) E1000RxDesc {
		uint64 address;
		uint16 length;
		uint16 checksum;
		uint8 status;
		uint8 errors;
		uint16 special;
	};

	_PACKED(struct) E1000TxDesc {
		uint64 address;
		uint16 length;
		uint8 cso;
		uint8 cmd;
		uint8 status;
		uint8 css;
		uint16 special;
	};

	struct DmaRegion {
		stduint handle = 0;
		uint64 physical = 0;
		uint64 length = 0;
		uint8* address = nullptr;
	};

	struct E1000Context {
		stduint dev_handle = 0;
		volatile uint32* regs = nullptr;
		PwcallDeviceResourceInfo mmio = {};
		DmaRegion rx_desc_dma = {};
		DmaRegion tx_desc_dma = {};
		DmaRegion rx_buf_dma = {};
		DmaRegion tx_buf_dma = {};
		E1000RxDesc* rx_desc = nullptr;
		E1000TxDesc* tx_desc = nullptr;
		uint8* rx_buffers = nullptr;
		uint8* tx_buffers = nullptr;
		uint8 mac[6]{};
		uint32 rx_index = 0;
		uint32 tx_index = 0;
		bool rings_ready = false;
		bool link_up = false;

		uint32 read_reg(E1000Reg reg) const {
			return regs ? regs[_IMM(reg)] : 0;
		}

		void write_reg(E1000Reg reg, uint32 value) const {
			if (regs) regs[_IMM(reg)] = value;
		}

		void write_reg_at(E1000Reg base_reg, stduint index, uint32 value) const {
			if (regs) regs[_IMM(base_reg) + index] = value;
		}

		void read_mac() {
			const uint32 ral = read_reg(E1000Reg::RAL0);
			const uint32 rah = read_reg(E1000Reg::RAH0);
			mac[0] = byte(ral);
			mac[1] = byte(ral >> 8);
			mac[2] = byte(ral >> 16);
			mac[3] = byte(ral >> 24);
			mac[4] = byte(rah);
			mac[5] = byte(rah >> 8);
		}

		void update_link_state() {
			link_up = (read_reg(E1000Reg::STATUS) & E1000_STATUS_LU) != 0;
		}

		void reset() {
			write_reg(E1000Reg::IMC, 0xFFFFFFFFu);
			(void)read_reg(E1000Reg::ICR);
			write_reg(E1000Reg::CTRL, read_reg(E1000Reg::CTRL) | E1000_CTRL_RST);
			for0(i, 100000) (void)read_reg(E1000Reg::STATUS);
			write_reg(E1000Reg::IMC, 0xFFFFFFFFu);
			(void)read_reg(E1000Reg::ICR);
			write_reg(E1000Reg::CTRL, read_reg(E1000Reg::CTRL) | E1000_CTRL_SLU);
		}

		bool configure_rx() {
			if (!rx_desc || !rx_buffers) return false;
			write_reg(E1000Reg::RCTL, 0);
			for0(i, 128) write_reg_at(E1000Reg::MTA, i, 0);
			for0(i, E1000_RX_DESC_COUNT) {
				rx_desc[i].address = rx_buf_dma.physical + uint64(i * E1000_FRAME_BUF_SIZE);
				rx_desc[i].status = 0;
				rx_desc[i].errors = 0;
			}
			write_reg(E1000Reg::RDBAL, uint32(rx_desc_dma.physical));
			write_reg(E1000Reg::RDBAH, uint32(rx_desc_dma.physical >> 32));
			write_reg(E1000Reg::RDLEN, sizeof(E1000RxDesc) * E1000_RX_DESC_COUNT);
			write_reg(E1000Reg::RDH, 0);
			write_reg(E1000Reg::RDT, E1000_RX_DESC_COUNT - 1);
			rx_index = 0;
			write_reg(E1000Reg::RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
			return true;
		}

		bool configure_tx() {
			if (!tx_desc || !tx_buffers) return false;
			write_reg(E1000Reg::TCTL, 0);
			for0(i, E1000_TX_DESC_COUNT) {
				tx_desc[i].address = tx_buf_dma.physical + uint64(i * E1000_FRAME_BUF_SIZE);
				tx_desc[i].status = E1000_TXD_STAT_DD;
			}
			write_reg(E1000Reg::TDBAL, uint32(tx_desc_dma.physical));
			write_reg(E1000Reg::TDBAH, uint32(tx_desc_dma.physical >> 32));
			write_reg(E1000Reg::TDLEN, sizeof(E1000TxDesc) * E1000_TX_DESC_COUNT);
			write_reg(E1000Reg::TDH, 0);
			write_reg(E1000Reg::TDT, 0);
			tx_index = 0;
			write_reg(E1000Reg::TIPG, 0x0060200Au);
			write_reg(E1000Reg::TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10u << 4) | (0x40u << 12));
			return true;
		}

		bool configure_rings() {
			if (!configure_rx()) return false;
			if (!configure_tx()) return false;
			rings_ready = true;
			return true;
		}

		stdsint send_frame(const void* data, stduint count) {
			if (!rings_ready || !data || !count || count > E1000_ETHERNET_MAX_FRAME) return -1;
			auto& desc = tx_desc[tx_index];
			if ((desc.status & E1000_TXD_STAT_DD) == 0) return 0;
			const stduint wire_len = maxof(count, E1000_ETHERNET_MIN_FRAME);
			uint8* buffer = tx_buffers + tx_index * E1000_FRAME_BUF_SIZE;
			MemSet(buffer, 0, wire_len);
			MemCopyN(buffer, data, count);
			desc.length = uint16(wire_len);
			desc.cso = 0;
			desc.cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
			desc.status = 0;
			desc.css = 0;
			desc.special = 0;
			_ASM volatile ("mfence":::"memory");
			const uint32 used_index = tx_index;
			tx_index = (tx_index + 1) % E1000_TX_DESC_COUNT;
			write_reg(E1000Reg::TDT, tx_index);
			for0(i, E1000_TX_WAIT_SPINS) {
				if (tx_desc[used_index].status & E1000_TXD_STAT_DD) {
					update_link_state();
					return stdsint(count);
				}
			}
			return 0;
		}

		void release_rx_descriptor(uint32 index) {
			auto& desc = rx_desc[index];
			desc.status = 0;
			desc.errors = 0;
			_ASM volatile ("mfence":::"memory");
			write_reg(E1000Reg::RDT, index);
		}

		stdsint read_frame(void* data, stduint count) {
			if (!rings_ready || !data || !count) return -1;
			for0(i, E1000_RX_DESC_COUNT) {
				auto& desc = rx_desc[rx_index];
				if ((desc.status & E1000_RXD_STAT_DD) == 0) return 0;
				const uint32 released = rx_index;
				rx_index = (rx_index + 1) % E1000_RX_DESC_COUNT;
				if (desc.errors) {
					release_rx_descriptor(released);
					continue;
				}
				const stduint frame_len = desc.length;
				const stduint copy_len = minof(count, frame_len);
				MemCopyN(data, rx_buffers + released * E1000_FRAME_BUF_SIZE, copy_len);
				release_rx_descriptor(released);
				update_link_state();
				return stdsint(copy_len);
			}
			return 0;
		}
	};

	E1000Context g_e1000{};

	bool MatchDeviceId(uint16 device_id) {
		for0(i, numsof(E1000DeviceIds)) {
			if (E1000DeviceIds[i] == device_id) return true;
		}
		return false;
	}

	bool FindResource(stduint dev_handle, PwcallDeviceResourceType type, uint32 index, PwcallDeviceResourceInfo* out) {
		if (!out) return false;
		const stdsint count = Powercall::DevGetResourceCount(dev_handle);
		if (count <= 0) return false;
		for (stdsint i = 0; i < count; ++i) {
			PwcallDeviceResourceQuery query = {};
			query.index = uint32(i);
			if (Powercall::DevGetResource(dev_handle, &query) != 0) continue;
			if (query.resource.type == _IMM(type) && query.resource.index == index) {
				*out = query.resource;
				return true;
			}
		}
		return false;
	}

	bool AllocDmaRegion(stduint dev_handle, stduint size, DmaRegion& region) {
		const stdsint dma_handle = Powercall::DevDmaAlloc(dev_handle, size, 0);
		if (dma_handle <= 0) return false;
		region.handle = stduint(dma_handle);
		PwcallDeviceDmaMapRequest req = {};
		req.map_flags = _IMM(PwcallDeviceDmaMapFlag::Writable);
		if (Powercall::DevDmaMap(region.handle, &req) != 0 || !req.address || !req.physical || !req.length) {
			Powercall::DevDmaFree(region.handle);
			region = {};
			return false;
		}
		region.physical = req.physical;
		region.length = req.length;
		region.address = reinterpret_cast<uint8*>(stduint(req.address));
		MemSet(region.address, 0, stduint(region.length));
		return true;
	}

	stduint OpenE1000() {
		for0(i, numsof(E1000DeviceIds)) {
			const stduint cls = (stduint(E1000VendorId) << 16) | E1000DeviceIds[i];
			const stdsint opened = Powercall::DevOpen(0, cls, 0);
			if (opened > 0) return stduint(opened);
		}
		return 0;
	}

	bool PublishAttach() {
		FMT_NetworkMsg_DRV_ATTACH attach = {};
		attach.version = NetworkDriverProtocolVersion;
		attach.caps = NetworkDriverCap_Poll;
		attach.dev_handle = uint32(g_e1000.dev_handle);
		attach.mtu = 1500;
		for0(i, numsof(g_e1000.mac)) attach.mac[i] = g_e1000.mac[i];
		attach.link_state = g_e1000.link_up ? 1 : 0;
		const char* name = "e1000";
		for0(i, NetworkDriverNameCapacity - 1) {
			attach.name[i] = name[i];
			if (!name[i]) break;
		}
		CommMsg send_msg = {};
		send_msg.data.address = _IMM(&attach);
		send_msg.data.length = sizeof(attach);
		send_msg.type = _IMM(NetworkMsg::DRV_ATTACH);
		if (Powercall::SysComm(COMM_SEND, Task_Net_Serv, &send_msg)) return false;

		stdsint result = -1;
		CommMsg recv_msg = {};
		recv_msg.data.address = _IMM(&result);
		recv_msg.data.length = sizeof(result);
		if (Powercall::SysComm(COMM_RECV, Task_Net_Serv, &recv_msg)) return false;
		return result == 0;
	}

	bool InitializeE1000() {
		g_e1000.dev_handle = OpenE1000();
		if (!g_e1000.dev_handle) return false;
		PwcallDeviceIdentity identity = {};
		if (Powercall::DevGetIdentity(g_e1000.dev_handle, &identity) != 0) return false;
		if (identity.vendor_id != E1000VendorId || !MatchDeviceId(identity.device_id)) return false;
		if (!FindResource(g_e1000.dev_handle, PwcallDeviceResourceType::PciBarMmio, 0, &g_e1000.mmio)) return false;

		PwcallDeviceMapRequest mmio_req = {};
		mmio_req.resource_type = _IMM(PwcallDeviceResourceType::PciBarMmio);
		mmio_req.resource_index = g_e1000.mmio.index;
		mmio_req.map_flags = _IMM(PwcallDeviceMapFlag::Writable);
		const stdsint mmio_addr = Powercall::DevMmap(g_e1000.dev_handle, &mmio_req);
		if (mmio_addr <= 0) return false;
		g_e1000.regs = reinterpret_cast<volatile uint32*>(stduint(mmio_addr));

		if (!AllocDmaRegion(g_e1000.dev_handle, sizeof(E1000RxDesc) * E1000_RX_DESC_COUNT, g_e1000.rx_desc_dma)) return false;
		if (!AllocDmaRegion(g_e1000.dev_handle, sizeof(E1000TxDesc) * E1000_TX_DESC_COUNT, g_e1000.tx_desc_dma)) return false;
		if (!AllocDmaRegion(g_e1000.dev_handle, E1000_FRAME_BUF_SIZE * E1000_RX_DESC_COUNT, g_e1000.rx_buf_dma)) return false;
		if (!AllocDmaRegion(g_e1000.dev_handle, E1000_FRAME_BUF_SIZE * E1000_TX_DESC_COUNT, g_e1000.tx_buf_dma)) return false;

		g_e1000.rx_desc = reinterpret_cast<E1000RxDesc*>(g_e1000.rx_desc_dma.address);
		g_e1000.tx_desc = reinterpret_cast<E1000TxDesc*>(g_e1000.tx_desc_dma.address);
		g_e1000.rx_buffers = g_e1000.rx_buf_dma.address;
		g_e1000.tx_buffers = g_e1000.tx_buf_dma.address;

		g_e1000.reset();
		g_e1000.read_mac();
		g_e1000.update_link_state();
		if (!g_e1000.configure_rings()) return false;
		return true;
	}
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	if (Powercall::Hello() != 0) return -1;
	if (!InitializeE1000()) return -1;
	if (!PublishAttach()) return -1;
	if (Powercall::DevPublish(g_e1000.dev_handle, PwcallDevicePublishCommand::Started) != 0) return -1;

	for (;;) {
		FMT_NetworkMsg_DRV_FRAME frame = {};
		CommMsg recv_msg = {};
		recv_msg.data.address = _IMM(&frame);
		recv_msg.data.length = sizeof(frame);
		if (Powercall::SysComm(COMM_RECV, ANYPROC, &recv_msg)) {
			syscall(syscall_t::REST, 1, 10);
			continue;
		}

		switch (NetworkMsg(recv_msg.type)) {
		case NetworkMsg::DRV_SEND:
			frame.status = g_e1000.send_frame(frame.data, frame.length);
			break;
		case NetworkMsg::DRV_RECV:
			frame.status = g_e1000.read_frame(frame.data, minof(stduint(frame.capacity), stduint(NetworkDriverFrameCapacity)));
			frame.length = frame.status > 0 ? uint32(frame.status) : 0;
			break;
		default:
			frame.status = -1;
			break;
		}

		CommMsg reply_msg = {};
		reply_msg.data.address = _IMM(&frame);
		reply_msg.data.length = sizeof(frame);
		reply_msg.type = recv_msg.type;
		Powercall::SysComm(COMM_SEND_ASYNC, recv_msg.src, &reply_msg);
	}
}

#else

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return -1;
}

#endif
