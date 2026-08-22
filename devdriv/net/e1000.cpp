// ASCII g++ TAB4 LF
// ModuTitle: Intel e1000 bring-up
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/System/Network/Layer/Link.hpp>

namespace {
	uni::PCI pci;

	constexpr uint16 e1000_pci_device_ids[] = {
		0x100Eu,
		0x100Fu,
		0x1010u,
		0x10D3u,
	};

	constexpr uint16 PCI_CMD_IO_SPACE = 0x0001u;
	constexpr uint16 PCI_CMD_MEM_SPACE = 0x0002u;
	constexpr uint16 PCI_CMD_BUS_MASTER = 0x0004u;

	enum class E1000Reg {
		CTRL = 0x0000u / 4,
		STATUS = 0x0008u / 4,
		EECD = 0x0010u / 4,
		ICR = 0x00C0u / 4,
		IMS = 0x00D0u / 4,
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
	constexpr uint32 E1000_RCTL_EN = 1u << 1;
	constexpr uint32 E1000_RCTL_BAM = 1u << 15;
	constexpr uint32 E1000_RCTL_SECRC = 1u << 26;
	constexpr uint32 E1000_TCTL_EN = 1u << 1;
	constexpr uint32 E1000_TCTL_PSP = 1u << 3;
	constexpr uint32 E1000_STATUS_LU = 1u << 1;
	constexpr uint32 E1000_ICR_TXDW = 1u << 0;
	constexpr uint32 E1000_ICR_TXQE = 1u << 1;
	constexpr uint32 E1000_ICR_LSC = 1u << 2;
	constexpr uint32 E1000_ICR_RXSEQ = 1u << 3;
	constexpr uint32 E1000_ICR_RXDMT0 = 1u << 4;
	constexpr uint32 E1000_ICR_RXO = 1u << 6;
	constexpr uint32 E1000_ICR_RXT0 = 1u << 7;
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
	constexpr uint8 E1000_IRQ_VECTOR = 0x78u;
	constexpr uint32 E1000_IRQ_MASK =
		E1000_ICR_TXDW | E1000_ICR_TXQE | E1000_ICR_LSC |
		E1000_ICR_RXSEQ | E1000_ICR_RXDMT0 | E1000_ICR_RXO | E1000_ICR_RXT0;
	constexpr stduint E1000_CTRL_GET_STATS = 0xE1000001u;
	constexpr stduint E1000_CTRL_SERVICE_EVENTS = 0xE1000002u;
	constexpr uint32 E1000_EVENT_RX = 1u << 0;
	constexpr uint32 E1000_EVENT_TX = 1u << 1;
	constexpr uint32 E1000_EVENT_LINK = 1u << 2;
	constexpr uint32 E1000_EVENT_ERROR = 1u << 3;

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

	struct E1000Stats {
		uint32 rx_packets;
		uint32 tx_packets;
		uint32 rx_drops;
		uint32 rx_errors;
		uint32 tx_busy;
		uint32 irq_count;
		uint32 rx_interrupts;
		uint32 tx_interrupts;
		uint32 link_changes;
		uint32 rx_overruns;
		uint32 rx_seq_errors;
		uint32 tx_queue_empty;
		uint32 last_icr;
		uint32 pending_events;
		uint8 irq_vector;
		uint8 msi_enabled;
		uint8 intx_enabled;
		uint8 interrupts_enabled;
		uint8 link_up;
		uint8 reserved[3];
	};

	bool matches_device_id(uint16 device_id) {
		for0(i, numsof(e1000_pci_device_ids)) {
			if (e1000_pci_device_ids[i] == device_id) return true;
		}
		return false;
	}

	const DeviceResource* find_any_io_bar(const DeviceNode* node) {
		if (!node) return nullptr;
		for (uint32 bar_index = 0; bar_index < 6; ++bar_index) {
			if (const auto* io = Devsman::FindResource(node, DeviceResourceType::PciBarIo, bar_index)) {
				return io;
			}
		}
		return nullptr;
	}

	bool is_e1000_device(const DeviceNode* node) {
		if (!node) return false;
		if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return false;
		return node->fields.vendor_id == 0x8086u && matches_device_id(node->fields.device_id);
	}

	struct E1000Context {
		DeviceNode* node = nullptr;
		const DeviceResource* mmio = nullptr;
		const DeviceResource* io = nullptr;
		const DeviceResource* irq = nullptr;
		bool use_mmio = false;
		bool rings_ready = false;
		uint32 ctrl = 0;
		uint32 status = 0;
		uint32 eecd = 0;
		uint8 mac[6]{};
		E1000RxDesc* rx_desc = nullptr;
		E1000TxDesc* tx_desc = nullptr;
		uint8* rx_buffers = nullptr;
		uint8* tx_buffers = nullptr;
		uint32 rx_index = 0;
		uint32 tx_index = 0;
		uint32 rx_packets = 0;
		uint32 tx_packets = 0;
		uint64 rx_bytes = 0;
		uint64 tx_bytes = 0;
		uint32 rx_drops = 0;
		uint32 rx_errors = 0;
		uint32 tx_drops = 0;
		uint32 tx_errors = 0;
		uint32 tx_busy = 0;
		uint32 irq_count = 0;
		uint32 rx_interrupts = 0;
		uint32 tx_interrupts = 0;
		uint32 link_changes = 0;
		uint32 rx_overruns = 0;
		uint32 rx_seq_errors = 0;
		uint32 tx_queue_empty = 0;
		uint32 last_icr = 0;
		uint8 irq_vector = 0xFFu;
		bool msi_enabled = false;
		bool intx_enabled = false;
		bool interrupts_enabled = false;
		bool link_up = false;
		uint32 pending_events = 0;

		uint32 read_reg_at(stduint reg_index) const {
			if (use_mmio && mmio) {
				auto* base = reinterpret_cast<volatile uint32*>(stduint(mmio->start));
				return base[reg_index];
			}
			if (io) {
				outpd(uint16(io->start), uint32(reg_index << 2));
				return innpd(uint16(io->start + 4));
			}
			return 0;
		}

		void write_reg_at(stduint reg_index, uint32 value) const {
			if (use_mmio && mmio) {
				auto* base = reinterpret_cast<volatile uint32*>(stduint(mmio->start));
				base[reg_index] = value;
				return;
			}
			if (io) {
				outpd(uint16(io->start), uint32(reg_index << 2));
				outpd(uint16(io->start + 4), value);
			}
		}

		uint32 read_reg(E1000Reg reg) const {
			return read_reg_at(_IMM(reg));
		}

		void write_reg(E1000Reg reg, uint32 value) const {
			write_reg_at(_IMM(reg), value);
		}

		void write_reg_at(E1000Reg base_reg, stduint index, uint32 value) const {
			write_reg_at(_IMM(base_reg) + index, value);
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

		bool update_link_state() {
			const bool current = (read_reg(E1000Reg::STATUS) & E1000_STATUS_LU) != 0;
			const bool changed = current != link_up;
			link_up = current;
			return changed;
		}

		bool bind(DeviceNode* dev_node) {
			if (!is_e1000_device(dev_node)) return false;
			node = dev_node;
			mmio = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 0);
			io = find_any_io_bar(node);
			irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
			use_mmio = mmio != nullptr;
			return use_mmio || io != nullptr;
		}

		void reset() {
			write_reg(E1000Reg::IMC, 0xFFFFFFFFu);
			(void)read_reg(E1000Reg::ICR);
			write_reg(E1000Reg::CTRL, read_reg(E1000Reg::CTRL) | E1000_CTRL_RST);
			for0(i, 100000) (void)read_reg(E1000Reg::STATUS);
			write_reg(E1000Reg::IMC, 0xFFFFFFFFu);
			(void)read_reg(E1000Reg::ICR);
			write_reg(E1000Reg::CTRL, read_reg(E1000Reg::CTRL) | E1000_CTRL_SLU);
			interrupts_enabled = false;
		}

		bool allocate_rings() {
			if (!rx_desc) rx_desc = (E1000RxDesc*)mempool.allocate(sizeof(E1000RxDesc) * E1000_RX_DESC_COUNT, 12);
			if (!tx_desc) tx_desc = (E1000TxDesc*)mempool.allocate(sizeof(E1000TxDesc) * E1000_TX_DESC_COUNT, 12);
			if (!rx_buffers) rx_buffers = (uint8*)mempool.allocate(E1000_FRAME_BUF_SIZE * E1000_RX_DESC_COUNT, 12);
			if (!tx_buffers) tx_buffers = (uint8*)mempool.allocate(E1000_FRAME_BUF_SIZE * E1000_TX_DESC_COUNT, 12);
			if (!rx_desc || !tx_desc || !rx_buffers || !tx_buffers) return false;
			MemSet(rx_desc, 0, sizeof(E1000RxDesc) * E1000_RX_DESC_COUNT);
			MemSet(tx_desc, 0, sizeof(E1000TxDesc) * E1000_TX_DESC_COUNT);
			MemSet(rx_buffers, 0, E1000_FRAME_BUF_SIZE * E1000_RX_DESC_COUNT);
			MemSet(tx_buffers, 0, E1000_FRAME_BUF_SIZE * E1000_TX_DESC_COUNT);
			return true;
		}

		bool configure_rx() {
			write_reg(E1000Reg::RCTL, 0);
			for0(i, 128) write_reg_at(E1000Reg::MTA, i, 0);
			for0(i, E1000_RX_DESC_COUNT) {
				rx_desc[i].address = uint64(stduint(rx_buffers + i * E1000_FRAME_BUF_SIZE));
				rx_desc[i].status = 0;
			}
			const uint64 base = uint64(stduint(rx_desc));
			write_reg(E1000Reg::RDBAL, uint32(base));
			write_reg(E1000Reg::RDBAH, uint32(base >> 32));
			write_reg(E1000Reg::RDLEN, sizeof(E1000RxDesc) * E1000_RX_DESC_COUNT);
			write_reg(E1000Reg::RDH, 0);
			write_reg(E1000Reg::RDT, E1000_RX_DESC_COUNT - 1);
			rx_index = 0;
			write_reg(E1000Reg::RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
			return true;
		}

		bool configure_tx() {
			write_reg(E1000Reg::TCTL, 0);
			for0(i, E1000_TX_DESC_COUNT) {
				tx_desc[i].address = uint64(stduint(tx_buffers + i * E1000_FRAME_BUF_SIZE));
				tx_desc[i].status = E1000_TXD_STAT_DD;
			}
			const uint64 base = uint64(stduint(tx_desc));
			write_reg(E1000Reg::TDBAL, uint32(base));
			write_reg(E1000Reg::TDBAH, uint32(base >> 32));
			write_reg(E1000Reg::TDLEN, sizeof(E1000TxDesc) * E1000_TX_DESC_COUNT);
			write_reg(E1000Reg::TDH, 0);
			write_reg(E1000Reg::TDT, 0);
			tx_index = 0;
			write_reg(E1000Reg::TIPG, 0x0060200Au);
			write_reg(E1000Reg::TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10u << 4) | (0x40u << 12));
			return true;
		}

		bool configure_rings() {
			if (!allocate_rings()) return false;
			if (!configure_rx()) return false;
			if (!configure_tx()) return false;
			rings_ready = true;
			return true;
		}

		stdsint send_frame(const void* data, stduint count) {
			if (!rings_ready || !data || count == 0 || count > E1000_ETHERNET_MAX_FRAME) {
				++tx_errors;
				return -1;
			}
			auto& desc = tx_desc[tx_index];
			if ((desc.status & E1000_TXD_STAT_DD) == 0) {
				++tx_busy;
				++tx_drops;
				return 0;
			}
			const stduint wire_len = maxof(count, E1000_ETHERNET_MIN_FRAME);
			uint8* buffer = tx_buffers + tx_index * E1000_FRAME_BUF_SIZE;
			MemSet(buffer, 0, wire_len);
			MemCopyN(buffer, data, count);
			desc.address = uint64(stduint(buffer));
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
					++tx_packets;
					tx_bytes += count;
					return stdsint(count);
				}
			}
			++tx_busy;
			++tx_drops;
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
			if (!rings_ready || !data || count == 0) return -1;
			for0(i, E1000_RX_DESC_COUNT) {
				auto& desc = rx_desc[rx_index];
				if ((desc.status & E1000_RXD_STAT_DD) == 0) return 0;
				const uint32 released = rx_index;
				rx_index = (rx_index + 1) % E1000_RX_DESC_COUNT;
				if (desc.errors) {
					++rx_errors;
					++rx_drops;
					release_rx_descriptor(released);
					continue;
				}
				const stduint frame_len = desc.length;
				const stduint copy_len = minof(count, frame_len);
				if (frame_len > count) ++rx_drops;
				MemCopyN(data, rx_buffers + released * E1000_FRAME_BUF_SIZE, copy_len);
				release_rx_descriptor(released);
				++rx_packets;
				rx_bytes += frame_len;
				return stdsint(copy_len);
			}
			return 0;
		}

		void enable_interrupts() {
			(void)read_reg(E1000Reg::ICR);
			write_reg(E1000Reg::IMS, E1000_IRQ_MASK);
			interrupts_enabled = true;
		}

		void disable_interrupts() {
			write_reg(E1000Reg::IMC, 0xFFFFFFFFu);
			(void)read_reg(E1000Reg::ICR);
			interrupts_enabled = false;
		}

		uint32 handle_interrupt() {
			const uint32 icr = read_reg(E1000Reg::ICR);
			if (!icr) return 0;
			last_icr = icr;
			++irq_count;
			if (icr & (E1000_ICR_RXDMT0 | E1000_ICR_RXT0)) {
				++rx_interrupts;
				pending_events |= E1000_EVENT_RX;
			}
			if (icr & E1000_ICR_RXO) {
				++rx_overruns;
				pending_events |= E1000_EVENT_ERROR;
			}
			if (icr & E1000_ICR_RXSEQ) {
				++rx_seq_errors;
				pending_events |= E1000_EVENT_ERROR;
			}
			if (icr & E1000_ICR_TXDW) {
				++tx_interrupts;
				pending_events |= E1000_EVENT_TX;
			}
			if (icr & E1000_ICR_TXQE) {
				++tx_queue_empty;
				pending_events |= E1000_EVENT_TX;
			}
			if (icr & E1000_ICR_LSC) {
				++link_changes;
				pending_events |= E1000_EVENT_LINK;
				update_link_state();
			}
			return icr;
		}

		void fill_stats(E1000Stats& stats) const {
			stats.rx_packets = rx_packets;
			stats.tx_packets = tx_packets;
			stats.rx_drops = rx_drops;
			stats.rx_errors = rx_errors;
			stats.tx_busy = tx_busy;
			stats.irq_count = irq_count;
			stats.rx_interrupts = rx_interrupts;
			stats.tx_interrupts = tx_interrupts;
			stats.link_changes = link_changes;
			stats.rx_overruns = rx_overruns;
			stats.rx_seq_errors = rx_seq_errors;
			stats.tx_queue_empty = tx_queue_empty;
			stats.last_icr = last_icr;
			stats.pending_events = pending_events;
			stats.irq_vector = irq_vector;
			stats.msi_enabled = msi_enabled;
			stats.intx_enabled = intx_enabled;
			stats.interrupts_enabled = interrupts_enabled;
			stats.link_up = link_up;
			stats.reserved[0] = stats.reserved[1] = stats.reserved[2] = 0;
		}

		uint32 service_events() {
			const uint32 events = pending_events;
			pending_events = 0;
			return events;
		}
	};

	E1000Context g_e1000;

	class E1000LinkDevice : public uni::Network::LinkDevice {
	public:
		virtual const char* GetName() const override {
			return g_e1000.node && g_e1000.node->link.addr ? g_e1000.node->link.addr : "e1000";
		}

		virtual uni::Network::LinkMedium GetMedium() const override {
			return uni::Network::LinkMedium::Ethernet;
		}

		virtual uni::Network::LinkState GetState() const override {
			return g_e1000.link_up ? uni::Network::LinkState::Up : uni::Network::LinkState::Down;
		}

		virtual uni::Network::MacAddress GetAddress() const override {
			uni::Network::MacAddress address{};
			for0(i, numsof(address.octet)) address.octet[i] = g_e1000.mac[i];
			return address;
		}

		virtual stduint GetMtu() const override {
			return 1500;
		}

		virtual stdsint Send(const uni::Network::LinkFrameView& frame) override {
			return g_e1000.send_frame(frame.data, frame.length);
		}

		virtual stdsint Receive(uni::Network::LinkMutableFrameView& frame) override {
			const stdsint len = g_e1000.read_frame(frame.data, frame.capacity);
			if (len > 0) frame.length = stduint(len);
			return len;
		}

		virtual stdsint Control(stduint command, void* args) override {
			switch (command) {
			case E1000_CTRL_GET_STATS:
				if (!args) return -1;
				g_e1000.fill_stats(*reinterpret_cast<E1000Stats*>(args));
				return 0;
			case E1000_CTRL_SERVICE_EVENTS:
				if (!args) return -1;
				*reinterpret_cast<uint32*>(args) = g_e1000.service_events();
				return 0;
			default:
				return -1;
			}
		}

		virtual void GetStatistics(uni::Network::LinkStatistics& statistics) const override {
			MemSet(&statistics, 0, sizeof(statistics));
			statistics.rx_packets = g_e1000.rx_packets;
			statistics.tx_packets = g_e1000.tx_packets;
			statistics.rx_bytes = g_e1000.rx_bytes;
			statistics.tx_bytes = g_e1000.tx_bytes;
			statistics.rx_drops = g_e1000.rx_drops;
			statistics.tx_drops = g_e1000.tx_drops;
			statistics.rx_errors = g_e1000.rx_errors;
			statistics.tx_errors = g_e1000.tx_errors;
			statistics.interrupts = g_e1000.irq_count;
			statistics.link_changes = g_e1000.link_changes;
		}
	};

	E1000LinkDevice g_e1000_link_device;

	bool configure_msi_interrupt(const uni::PCI::Device& dev) {
		const stduint cpu_id = Taskman::getID();
		auto* percore = (cpu_id < PCU_CORES_MAX) ? Taskman::PCU_CORES_PERCORE[cpu_id] : nullptr;
		if (!percore || percore->lapic_id >= LAPIC_ID_MAP_SIZE) {
			plogwarn("[E1000] MSI setup skipped: no valid LAPIC on cpu%u", (unsigned)cpu_id);
			return false;
		}
		const auto result = pci.configure_MSI_fixed_destination(dev,
			uint8(percore->lapic_id),
			uni::PCI::MSITriggerMode::Edge,
			uni::PCI::MSIDeliveryMode::Fixed,
			E1000_IRQ_VECTOR, 0);
		if (result) {
			plogwarn("[E1000] MSI setup failed: %s", result.Name());
			return false;
		}
		g_e1000.irq_vector = E1000_IRQ_VECTOR;
		g_e1000.msi_enabled = true;
		Devsman::AddIrqResource(g_e1000.node, E1000_IRQ_VECTOR);
		g_e1000.irq = Devsman::FindResource(g_e1000.node, DeviceResourceType::IrqLine, 0);
		ploginfo("[E1000] MSI enabled vector=%u lapic=%u",
			(unsigned)g_e1000.irq_vector, (unsigned)percore->lapic_id);
		return true;
	}

	bool configure_legacy_interrupt() {
		if (!g_e1000.irq) {
			plogwarn("[E1000] INTx setup skipped: no IRQ line");
			return false;
		}
		const uint8 line = uint8(g_e1000.irq->start);
		if (line >= 24) {
			plogwarn("[E1000] INTx setup skipped: unsupported IRQ line=%u", (unsigned)line);
			return false;
		}
		if (IC.getType() == 0) {
			plogwarn("[E1000] INTx setup skipped: PIC vector sharing is not wired");
			return false;
		}
		IC.IO_Writ64(0x10 + line * 2, E1000_IRQ_VECTOR);
		g_e1000.irq_vector = E1000_IRQ_VECTOR;
		g_e1000.intx_enabled = true;
		// ploginfo("[E1000] INTx enabled line=%u vector=%u",
		// 	(unsigned)line, (unsigned)g_e1000.irq_vector);
		return true;
	}

	stdsint e1000_read(DeviceNode* node, void* buf, stduint count, stduint idx, stduint flags) {
		(void)idx;
		(void)flags;
		if (!node || node->fields.binding.driver_data != &g_e1000) return -1;
		return g_e1000.read_frame(buf, count);
	}

	stdsint e1000_send(DeviceNode* node, const void* buf, stduint count, stduint idx, stduint flags) {
		(void)idx;
		(void)flags;
		if (!node || node->fields.binding.driver_data != &g_e1000) return -1;
		return g_e1000.send_frame(buf, count);
	}

	stdsint e1000_ctrl(DeviceNode* node, stduint cmd, void* args, stduint flags) {
		(void)flags;
		if (!node || node->fields.binding.driver_data != &g_e1000) return -1;
		switch (cmd) {
		case E1000_CTRL_GET_STATS:
			if (!args) return -1;
			g_e1000.fill_stats(*reinterpret_cast<E1000Stats*>(args));
			return 0;
		case E1000_CTRL_SERVICE_EVENTS:
			if (!args) return -1;
			*reinterpret_cast<uint32*>(args) = g_e1000.service_events();
			return 0;
		default:
			return -1;
		}
	}

	const DeviceNodeOps e1000_ops{
		.read = e1000_read,
		.send = e1000_send,
		.ctrl = e1000_ctrl,
	};

	uni::PCI::Device make_pci_device(const DeviceNode& node) {
		uni::PCI::Device dev{};
		dev.bus = node.fields.pci_bus;
		dev.device = node.fields.pci_device;
		dev.function = node.fields.pci_function;
		dev.header_type = pci.read_header_type(dev.bus, dev.device, dev.function);
		dev.class_code.base = node.fields.class_base;
		dev.class_code.sub = node.fields.class_sub;
		dev.class_code.interface = node.fields.class_if;
		return dev;
	}

	void enable_device_access(const DeviceNode& node) {
		auto dev = make_pci_device(node);
		const bool has_mmio = Devsman::FindResource(&node, DeviceResourceType::PciBarMmio, 0) != nullptr;
		const bool has_io = find_any_io_bar(&node) != nullptr;
		uint16 cmd = uint16(uni::PCI::read_config_register(dev, 0x04) & 0xFFFFu);
		cmd |= PCI_CMD_BUS_MASTER;
		if (has_mmio) cmd |= PCI_CMD_MEM_SPACE;
		if (has_io) cmd |= PCI_CMD_IO_SPACE;
		uni::PCI::write_config_register(dev, 0x04, cmd);
		if (has_mmio) pci.enable_MMIO(dev);
	}
}

void Handint_E1000() {
	const uint32 icr = g_e1000.handle_interrupt();
	(void)icr;
	IC.SendEOI(E1000_IRQ_VECTOR);
}

static bool start_e1000_driver(DeviceNode* node) {
	if (!is_e1000_device(node)) return false;
	auto dev = make_pci_device(*node);
	enable_device_access(*node);
	if (!g_e1000.bind(node)) {
		plogwarn("[E1000] Missing BAR resource");
		return false;
	}
	(void)configure_msi_interrupt(dev);
	g_e1000.reset();
	g_e1000.ctrl = g_e1000.read_reg(E1000Reg::CTRL);
	g_e1000.status = g_e1000.read_reg(E1000Reg::STATUS);
	g_e1000.eecd = g_e1000.read_reg(E1000Reg::EECD);
	g_e1000.read_mac();
	g_e1000.link_up = (g_e1000.status & E1000_STATUS_LU) != 0;
	if (!g_e1000.configure_rings()) {
		plogwarn("[E1000] ring allocation failed");
		return false;
	}
	if (!g_e1000.msi_enabled) {
		(void)configure_legacy_interrupt();
	}
	if (g_e1000.msi_enabled || g_e1000.intx_enabled) {
		g_e1000.enable_interrupts();
	}
	node->fields.binding.driver_data = &g_e1000;
	Devsman::SetOps(node, &e1000_ops);
	if (!Devsman::RegisterLinkDevice(&g_e1000_link_device)) {
		plogwarn("[E1000] link device registration failed");
	}

	if (g_e1000.use_mmio) {
		ploginfo("[E1000] %s MMIO=%[64H] len=%[64H]%s",
			node->link.addr ? node->link.addr : "(unnamed)",
			g_e1000.mmio->start, g_e1000.mmio->length,
			g_e1000.irq ? "" : " irq=none");
	}
	else {
		ploginfo("[E1000] %s IO=%[64H]%s",
			node->link.addr ? node->link.addr : "(unnamed)",
			g_e1000.io->start,
			g_e1000.irq ? "" : " irq=none");
	}
	// if (g_e1000.irq) {
	// 	ploginfo("[E1000] IRQ line=%u pin=%u",
	// 		(unsigned)g_e1000.irq->start, (unsigned)g_e1000.irq->extra);
	// }
	ploginfo("[E1000] ctrl=%[32H] status=%[32H] eecd=%[32H]",
		g_e1000.ctrl, g_e1000.status, g_e1000.eecd);
	ploginfo("[E1000] MAC=%[8H]:%[8H]:%[8H]:%[8H]:%[8H]:%[8H]",
		(stduint)g_e1000.mac[0], (stduint)g_e1000.mac[1], (stduint)g_e1000.mac[2],
		(stduint)g_e1000.mac[3], (stduint)g_e1000.mac[4], (stduint)g_e1000.mac[5]);
	ploginfo("[E1000] link=%s irq-mode=%s vector=%u",
		g_e1000.link_up ? "up" : "down",
		g_e1000.msi_enabled ? "msi" : (g_e1000.intx_enabled ? "intx" : "polling"),
		(unsigned)g_e1000.irq_vector);
	ploginfo("[E1000] RX/TX rings ready rx=%u tx=%u",
		(unsigned)E1000_RX_DESC_COUNT, (unsigned)E1000_TX_DESC_COUNT);
	return true;
}

_ESYM_C void R_E1000_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_E1000{
	.init = R_E1000_INIT,
	.name = "E1000",
};

void R_E1000_INIT() {
	if (!PCI_Init(pci)) {
		plogwarn("[E1000] No devices on PCI or PCI init failed.");
	}
	#if _MCCA == 0x8664
	IC[E1000_IRQ_VECTOR].setModeRupt(mglb(Handint_E1000_Entry), SegCo64);
	#else
	IC[E1000_IRQ_VECTOR].setRange(mglb(Handint_E1000_Entry), SegCo32);
	#endif
	register_interrupt_handler(E1000_IRQ_VECTOR, Handint_E1000);
	Devsman::RegisterDriverStarter("e1000", start_e1000_driver);
	Devsman::StartKnownDrivers();
}

#endif
