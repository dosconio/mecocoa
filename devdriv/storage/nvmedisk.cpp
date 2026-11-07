// ASCII g++ TAB4 LF
// ModuTitle: Disk - NVMe
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "../../include/devsman-storage.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/ISO_IEC_STD/memory>
#include <c/storage/harddisk.h>

namespace {
	uni::PCI pci;
	static constexpr byte NVME_ADMIN_OPC_IDENTIFY = 0x06;
	static constexpr byte NVME_ADMIN_OPC_CREATE_IO_SQ = 0x01;
	static constexpr byte NVME_ADMIN_OPC_CREATE_IO_CQ = 0x05;
	static constexpr byte NVME_ADMIN_OPC_SET_FEATURES = 0x09;
	static constexpr byte NVME_NVM_OPC_READ = 0x02;
	static constexpr byte NVME_NVM_OPC_WRITE = 0x01;
	static constexpr uint8 NVME_FEAT_INTERRUPT_COALESCING = 0x08;
	static constexpr uint16 NVME_ADMIN_DEPTH_LIMIT = 64;
	static constexpr stduint NVME_ADMIN_PAGE_SIZE = 0x1000;
	static constexpr uint16 NVME_IO_QID = 1;
	static constexpr uint16 NVME_IO_DEPTH_LIMIT = 64;
	// NVMe 1.4c, Read/Write CDW12.NLB: a 16-bit zero-based block count.
	static constexpr stduint NVME_IO_MAX_BLOCKS = 1u << 16;
	static constexpr uint8 IRQ_NVME = IRQ_RTC + 3;
	static constexpr stduint NVME_IRQ_WAIT_SPINS = 100000u;

	#define nvme_fence() _ASM volatile ("mfence":::"memory")

	_PACKED(struct) NVMe_Command {
		byte opc;
		byte fuse;
		uint16 cid;
		uint32 nsid;
		uint64 reserved0;
		uint64 mptr;
		uint64 prp1;
		uint64 prp2;
		uint32 cdw10;
		uint32 cdw11;
		uint32 cdw12;
		uint32 cdw13;
		uint32 cdw14;
		uint32 cdw15;
	};

	_PACKED(struct) NVMe_Completion {
		uint32 dw0;
		uint32 reserved0;
		uint16 sq_head;
		uint16 sq_id;
		uint16 cid;
		uint16 status;
	};

	static_assert(sizeof(NVMe_Command) == 64, "NVMe_Command size mismatch");
	static_assert(sizeof(NVMe_Completion) == 16, "NVMe_Completion size mismatch");

	struct NVMe_Controller {
		struct NamespaceInfo {
			uint32 nsid = 0;
			uint32 block_size = 0;
			uint64 total_blocks = 0;
			DeviceNode* device_node = nullptr;
			uni::Harddisk_NVMe disk = {};
		};

		stduint index = 0;
		DeviceNode* node = nullptr;
		volatile uni::NVME_BAR* regs = nullptr;
		const DeviceResource* bar0 = nullptr;
		const DeviceResource* irq = nullptr;
		Devs::StorageDmaBuffer admin_workspace, io_workspace, data_buffer;
		byte* admin_sq = nullptr;
		byte* admin_cq = nullptr;
		byte* identify_ctrl = nullptr;
		byte* identify_nslist = nullptr;
		byte* io_sq = nullptr;
		byte* io_cq = nullptr;
		byte* identify_ns = nullptr;
		byte* sector_buf = nullptr;
		uint64* io_prp_list = nullptr;
		uint16 admin_depth = 0;
		uint16 sq_tail = 0;
		uint16 cq_head = 0;
		uint16 next_cid = 1;
		uint16 io_depth = 0;
		uint16 io_sq_tail = 0;
		uint16 io_cq_head = 0;
		byte cq_phase = 1;
		byte io_cq_phase = 1;
		byte dstrd = 0;
		uint32 controller_version = 0;
		uint32 controller_nn = 0;
		uint8 irq_vector = 0xFF;
		volatile uint32 irq_count = 0;
		uni::Atomic<byte> io_wait_active{0};
		Devs::StorageWaitContext completion_wait;
		bool msi_enabled = false;
		bool irq_log_once = false;
		bool io_submit_log_once = false;
		bool faulted = false, started = false;
		Mutex transaction;
		uni::Vector<uni::SharedPtr<NamespaceInfo>> namespaces;

		static bool ExecBlocksThunk(void* context, uint32 nsid, uint64 lba,
			void* data_buf, stduint block_count, bool is_write) {
			auto* controller = reinterpret_cast<NVMe_Controller*>(context);
			if (!controller) return false;
			return controller->ExecuteBlocks(nsid, lba, data_buf, block_count, is_write);
		}

		bool Bind(DeviceNode* nvme_node) {
			if (!nvme_node) return false;
			node = nvme_node;
			bar0 = Devsman::FindResource(nvme_node, DeviceResourceType::PciBarMmio, 0);
			irq = Devsman::FindResource(nvme_node, DeviceResourceType::IrqLine, 0);
			if (!bar0) return false;
			regs = reinterpret_cast<volatile uni::NVME_BAR*>(stduint(bar0->start));
			return regs != nullptr;
		}

		bool ConfigureInterrupts(const uni::PCI::Device& dev) {
			if (!node || !regs) return false;
			const stduint cpu_id = Taskman::getID();
			auto* percore = (cpu_id < PCU_CORES_MAX) ? Taskman::PCU_CORES_PERCORE[cpu_id] : nullptr;
			if (!percore || percore->lapic_id >= 0xFFu) {
				plogwarn("[NVMe] MSI setup skipped: no valid LAPIC on cpu%u", (unsigned)cpu_id);
				return false;
			}
			const auto result = pci.configure_MSI_fixed_destination(dev,
				uint8(percore->lapic_id),
				uni::PCI::MSITriggerMode::Edge,
				uni::PCI::MSIDeliveryMode::Fixed,
				IRQ_NVME, 0);
			if (result) {
				plogwarn("[NVMe] MSI setup failed: %s", result.Name());
				return false;
			}
			irq_vector = IRQ_NVME;
			msi_enabled = true;
			irq_count = 0;
			Devsman::AddIrqResource(node, IRQ_NVME);
			irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
			plogtrac("[NVMe] MSI enabled vector=%u lapic=%u",
				(unsigned)irq_vector, (unsigned)percore->lapic_id);
			return true;
		}

		void DumpSummary() const {
			if (!node || !regs || !bar0) return;
			const uint64 cap = regs->cap;
			const uint32 vs = regs->vs;
			const uint16 mqes = uint16((cap & NVME_CAP_MQES_MASK) + 1);
			const uint8 dstrd = uint8((cap >> NVME_CAP_DSTRD_SHIFT) & 0x0Fu);
			const uint8 major = uint8((vs >> 16) & 0xFFu);
			const uint8 minor = uint8((vs >> 8) & 0xFFu);
			const uint8 tertiary = uint8(vs & 0xFFu);
			plogtrac("[NVMe] ctlr %02x:%02x.%u BAR0=%[64H] len=%[64H]%s",
				(unsigned)node->fields.pci_bus,
				(unsigned)node->fields.pci_device,
				(unsigned)node->fields.pci_function,
				bar0->start,
				bar0->length,
				irq ? "" : " irq=none");
			plogtrac("[NVMe] cap=%[64H] vs=%u.%u.%u cc=%[32H] csts=%[32H]",
				cap, (unsigned)major, (unsigned)minor, (unsigned)tertiary,
				regs->cc, regs->csts);
			plogtrac("[NVMe] mqes=%u dstrd=%u aqa=%[32H] asq=%[64H] acq=%[64H]",
				(unsigned)mqes, (unsigned)dstrd, regs->aqa, regs->asq, regs->acq);
		}

		bool AllocateAdminBuffers() {
			if (!admin_workspace.Ensure(node, 4 * NVME_ADMIN_PAGE_SIZE)) return false;
			admin_sq = admin_workspace.virt;
			admin_cq = admin_sq + NVME_ADMIN_PAGE_SIZE;
			identify_ctrl = admin_cq + NVME_ADMIN_PAGE_SIZE;
			identify_nslist = identify_ctrl + NVME_ADMIN_PAGE_SIZE;
			MemSet(admin_sq, 0, NVME_ADMIN_PAGE_SIZE);
			MemSet(admin_cq, 0, NVME_ADMIN_PAGE_SIZE);
			MemSet(identify_ctrl, 0, NVME_ADMIN_PAGE_SIZE);
			MemSet(identify_nslist, 0, NVME_ADMIN_PAGE_SIZE);
			return true;
		}

		bool AllocateIoBuffers() {
			if (!io_workspace.Ensure(node, 5 * NVME_ADMIN_PAGE_SIZE)) return false;
			io_sq = io_workspace.virt;
			io_cq = io_sq + NVME_ADMIN_PAGE_SIZE;
			identify_ns = io_cq + NVME_ADMIN_PAGE_SIZE;
			sector_buf = identify_ns + NVME_ADMIN_PAGE_SIZE;
			io_prp_list = reinterpret_cast<uint64*>(sector_buf + NVME_ADMIN_PAGE_SIZE);
			MemSet(io_sq, 0, NVME_ADMIN_PAGE_SIZE);
			MemSet(io_cq, 0, NVME_ADMIN_PAGE_SIZE);
			MemSet(identify_ns, 0, NVME_ADMIN_PAGE_SIZE);
			MemSet(sector_buf, 0, NVME_ADMIN_PAGE_SIZE);
			MemSet(io_prp_list, 0, NVME_ADMIN_PAGE_SIZE);
			return true;
		}

		volatile uint32* DoorbellSq(uint16 qid) const {
			if (!regs) return nullptr;
			byte* doorbell_base = (byte*)&regs->doorbells[0];
			const stduint stride = 4u << dstrd;
			return reinterpret_cast<volatile uint32*>(doorbell_base + stduint(2 * qid) * stride);
		}

		volatile uint32* DoorbellCq(uint16 qid) const {
			if (!regs) return nullptr;
			byte* doorbell_base = (byte*)&regs->doorbells[0];
			const stduint stride = 4u << dstrd;
			return reinterpret_cast<volatile uint32*>(doorbell_base + stduint(2 * qid + 1) * stride);
		}

		struct CommandWait {
			NVMe_Controller* controller;
			volatile NVMe_Completion* cq;
			uint16* head;
			byte* phase;
			uint16 depth, qid;
			NVMe_Completion* completion;
		};

		static Devs::StorageWaitStatus ProbeCompletion(void* context) {
			auto& wait = *static_cast<CommandWait*>(context);
			const volatile NVMe_Completion& entry = wait.cq[*wait.head];
			if ((entry.status & 1u) != *wait.phase) return Devs::StorageWaitStatus::Pending;
			nvme_fence();
			auto& cpl = *wait.completion;
			cpl.dw0 = entry.dw0;
			cpl.reserved0 = entry.reserved0;
			cpl.sq_head = entry.sq_head;
			cpl.sq_id = entry.sq_id;
			cpl.cid = entry.cid;
			cpl.status = entry.status;
			// plogtrac("[NVMe] cqe cid=%u status=%[16H] sqh=%u sqid=%u dw0=%[32H]",
			// 	(unsigned)cpl.cid, (unsigned)cpl.status,
			// 	(unsigned)cpl.sq_head, (unsigned)cpl.sq_id, (unsigned)cpl.dw0);

			if (wait.qid == NVME_IO_QID) {
				// plogtrac("[NVMe] poll cqe cid=%u status=%[16H] sqh=%u sqid=%u dw0=%[32H]",
				// 	(unsigned)cpl.cid, (unsigned)cpl.status,
				// 	(unsigned)cpl.sq_head, (unsigned)cpl.sq_id, (unsigned)cpl.dw0);
			}
			if (++*wait.head == wait.depth) {
				*wait.head = 0;
				*wait.phase ^= 1;
			}
			*wait.controller->DoorbellCq(wait.qid) = *wait.head;
			nvme_fence();
			return Devs::StorageWaitStatus::Completed;
		}

		bool WaitReady(bool want_ready) const {
			if (!regs) return false;
			const stduint timeout_units = stduint((regs->cap >> NVME_CAP_TO_SHIFT) & 0xFFu);
			const stduint loops = 500000u * (timeout_units ? timeout_units : 1u);
			for (stduint spin = 0; spin < loops; ++spin) {
				const bool ready = (regs->csts & NVME_CSTS_RDY) != 0;
				if (ready == want_ready) return true;
				if (regs->csts & NVME_CSTS_CFS) return false;
				__asm__ __volatile__("pause" ::: "memory");
			}
			return false;
		}

		bool DisableController() {
			if (!regs) return false;
			regs->cc &= ~NVME_CC_EN;
			nvme_fence();
			if (!WaitReady(false)) {
				plogwarn("[NVMe] disable timeout cc=%[32H] csts=%[32H]", regs->cc, regs->csts);
				return false;
			}
			// NVMe CC.EN=0 is only a request; CSTS.RDY=0 confirms that queue/data DMA has stopped.
			admin_workspace.MarkQuiesced();
			io_workspace.MarkQuiesced();
			data_buffer.MarkQuiesced();
			return true;
		}

		uni::StorageRecoveryResult FailController() {
			faulted = true;
			io_wait_active.store(0, uni::MemoryOrder_Release);
			regs->intms = 0xFFFFFFFFu;
			// A failed stop leaves all active DMA storage retained and forbids subsequent submissions.
			const bool stopped = DisableController();
			return {false, stopped, stopped};
		}

		bool EnableController() {
			if (!regs) return false;
			uint32 cc = regs->cc;
			cc &= ~0x00FFF000u;
			cc &= ~NVME_CC_EN;
			cc |= (6u << 16); // IOSQES = 2^6 = 64 bytes
			cc |= (4u << 20); // IOCQES = 2^4 = 16 bytes
			cc |= NVME_CC_EN;
			regs->cc = cc;
			nvme_fence();
			if (!WaitReady(true)) {
				plogwarn("[NVMe] enable timeout cc=%[32H] csts=%[32H]", regs->cc, regs->csts);
				return false;
			}
			return true;
		}

		bool ConfigureAdminQueue() {
			if (!regs || faulted || !DisableController()) return false;
			if (!AllocateAdminBuffers()) {
				plogwarn("[NVMe] admin buffer allocation failed");
				return false;
			}
			const uint16 mqes = uint16((regs->cap & NVME_CAP_MQES_MASK) + 1);
			admin_depth = uint16(minof(mqes, NVME_ADMIN_DEPTH_LIMIT));
			if (admin_depth < 2) {
				plogwarn("[NVMe] invalid admin depth %u", (unsigned)admin_depth);
				return false;
			}
			dstrd = byte((regs->cap >> NVME_CAP_DSTRD_SHIFT) & 0x0Fu);
			sq_tail = 0;
			cq_head = 0;
			cq_phase = 1;
			next_cid = 1;

			regs->aqa = (uint32(admin_depth - 1) << 16) | uint32(admin_depth - 1);
			regs->asq = admin_workspace.device_address;
			regs->acq = admin_workspace.device_address + NVME_ADMIN_PAGE_SIZE;
			nvme_fence();
			admin_workspace.MarkInFlight();
			if (!EnableController()) {
				FailController();
				return false;
			}

			plogtrac("[NVMe] adminq depth=%u sq=%p cq=%p", (unsigned)admin_depth, admin_sq, admin_cq);
			return true;
		}

		bool SubmitCommand(NVMe_Command& cmd, NVMe_Completion& cpl,
			byte* sq_mem, byte* cq_mem,
			uint16& sq_tail_ref, uint16& cq_head_ref, byte& cq_phase_ref,
			uint16 depth, uint16 qid, uni::StorageRecoveryResult* recovery = nullptr) {
			// This output describes only the current command, not any previously retained request.
			const bool can_submit = regs && !faulted && sq_mem && cq_mem && depth;
			if (recovery) *recovery = {can_submit, true, false};
			if (!can_submit) return false;
			auto* sq = reinterpret_cast<NVMe_Command*>(sq_mem);
			const uint16 cid = next_cid++;
			cmd.cid = cid;
			sq[sq_tail_ref] = cmd;
			nvme_fence();
			sq_tail_ref = uint16((sq_tail_ref + 1) % depth);
			const bool irq_completion = (qid == NVME_IO_QID) && msi_enabled;
			io_wait_active.store(irq_completion, uni::MemoryOrder_Release);
			if (recovery) recovery->requests_quiesced = false;
			*DoorbellSq(qid) = sq_tail_ref;
			nvme_fence();

			// Only this serialized submitting thread consumes CQEs; the MSI handler merely notifies it.
			CommandWait wait{this, reinterpret_cast<volatile NVMe_Completion*>(cq_mem),
				&cq_head_ref, &cq_phase_ref, depth, qid, &cpl};
			auto outcome = Devs::WaitStorage(completion_wait, ProbeCompletion, &wait,
				5 * CONFIG_SysTickFreq, 5000000u, irq_completion);
			io_wait_active.store(0, uni::MemoryOrder_Release);
			if (outcome == Devs::StorageWaitStatus::TimedOut) outcome = ProbeCompletion(&wait);
			if (outcome != Devs::StorageWaitStatus::Completed) {
				plogwarn("[NVMe] q%u cmd opcode=%[8H] timeout tail=%u head=%u cc=%[32H] csts=%[32H]",
					(unsigned)qid, (unsigned)cmd.opc, (unsigned)sq_tail_ref, (unsigned)cq_head_ref,
					regs->cc, regs->csts);
				const auto failure = FailController();
				if (recovery) *recovery = failure;
				return false;
			}
			if (cpl.cid != cid) {
				plogwarn("[NVMe] q%u cid mismatch got=%u want=%u", (unsigned)qid, (unsigned)cpl.cid, (unsigned)cid);
				const auto failure = FailController();
				if (recovery) *recovery = failure;
				return false;
			}
			// Only a matching completion retires this request, even when its protocol status is an error.
			if (recovery) *recovery = {true, true, false};
			if ((cpl.status >> 1) != 0) {
				const uint16 sc = (cpl.status >> 1) & 0xFFu;
				const uint16 sct = (cpl.status >> 9) & 0x7u;
				plogwarn("[NVMe] q%u cmd opcode=%[8H] cid=%u fail sc=%u sct=%u status=%[16H]",
					(unsigned)qid, (unsigned)cmd.opc, (unsigned)cid, (unsigned)sc, (unsigned)sct, (unsigned)cpl.status);
				return false;
			}
			return true;
		}

		bool SubmitAdminCommand(NVMe_Command& cmd, NVMe_Completion& cpl) {
			return SubmitCommand(cmd, cpl, admin_sq, admin_cq,
				sq_tail, cq_head, cq_phase, admin_depth, 0);
		}

		bool SubmitIoCommand(NVMe_Command& cmd, NVMe_Completion& cpl, uni::StorageRecoveryResult& recovery) {
			return SubmitCommand(cmd, cpl, io_sq, io_cq,
				io_sq_tail, io_cq_head, io_cq_phase, io_depth, NVME_IO_QID, &recovery);
		}

		static uint32 LoadLe32(const byte* src) {
			return uint32(src[0]) |
				(uint32(src[1]) << 8) |
				(uint32(src[2]) << 16) |
				(uint32(src[3]) << 24);
		}

		static uint64 LoadLe64(const byte* src) {
			return uint64(LoadLe32(src)) | (uint64(LoadLe32(src + 4)) << 32);
		}

		static void FormatAsciiField(char* dest, stduint dest_size, const byte* src, stduint src_size) {
			if (!dest || dest_size == 0) return;
			MemSet(dest, 0, dest_size);
			if (!src || src_size == 0) return;
			stduint copy_len = minof(dest_size - 1, src_size);
			for0(i, copy_len) {
				byte ch = src[i];
				dest[i] = (ch >= 0x20 && ch <= 0x7E) ? char(ch) : ' ';
			}
			while (copy_len && dest[copy_len - 1] == ' ') {
				dest[copy_len - 1] = '\0';
				--copy_len;
			}
		}

		NamespaceInfo* FindNamespace(uint32 nsid) {
			if (!nsid) return nullptr;
			for (auto& owner : namespaces) {
				if (owner && owner->nsid == nsid) return owner.get();
			}
			return nullptr;
		}

		void ResetNamespaces() {
			namespaces.Clear();
		}

		bool SupportsActiveNamespaceList() const {
			const uint8 major = uint8((controller_version >> 16) & 0xFFu);
			const uint8 minor = uint8((controller_version >> 8) & 0xFFu);
			return major > 1 || (major == 1 && minor >= 1);
		}

		bool AddNamespaceId(uint32 nsid) {
			if (!nsid) return false;
			for (auto& owner : namespaces) {
				if (owner && owner->nsid == nsid) return true;
			}
			auto owner = uni::make_shared<NamespaceInfo>();
			if (!owner) return false;
			owner->nsid = nsid;
			namespaces.Append(owner);
			return true;
		}

		bool PopulateNamespacesFromController() {
			ResetNamespaces();
			if (controller_nn == 0) return false;
			for (uint32 nsid = 1; nsid && nsid <= controller_nn; ++nsid) {
				if (!AddNamespaceId(nsid)) {
					plogwarn("[NVMe] namespace allocation failed at nsid=%[32H]", nsid);
					break;
				}
			}
			plogtrac("[NVMe] namespaces via nn=%u kept=%u first=%[32H]",
				(unsigned)controller_nn, (unsigned)namespaces.Count(),
				namespaces.Count() && namespaces[0] ? namespaces[0]->nsid : 0u);
			if (controller_nn > namespaces.Count()) {
				plogwarn("[NVMe] only %u of %u namespaces kept",
					(unsigned)namespaces.Count(), (unsigned)controller_nn);
			}
			return namespaces.Count() != 0;
		}

		bool IdentifyController() {
			if (!identify_ctrl) return false;
			NVMe_Command cmd{};
			NVMe_Completion cpl{};
			cmd.opc = NVME_ADMIN_OPC_IDENTIFY;
			cmd.nsid = 0;
			cmd.prp1 = admin_workspace.device_address + 2 * NVME_ADMIN_PAGE_SIZE;
			cmd.cdw10 = 1; // CNS = Identify Controller
			MemSet(identify_ctrl, 0, NVME_ADMIN_PAGE_SIZE);
			if (!SubmitAdminCommand(cmd, cpl)) return false;
			controller_version = regs ? regs->vs : 0;
			controller_nn = LoadLe32(identify_ctrl + 516);
			char serial[21] = {};
			char model[41] = {};
			char fw[9] = {};
			FormatAsciiField(serial, sizeof(serial), identify_ctrl + 4, 20);
			FormatAsciiField(model, sizeof(model), identify_ctrl + 24, 40);
			FormatAsciiField(fw, sizeof(fw), identify_ctrl + 64, 8);
			const uint16 vid = uint16(identify_ctrl[0]) | (uint16(identify_ctrl[1]) << 8);
			const uint16 ssvid = uint16(identify_ctrl[2]) | (uint16(identify_ctrl[3]) << 8);
			ploginfo("[NVMe] controller=%u identify vid=%[16H] ssvid=%[16H] sn=\"%s\" mn=\"%s\" fr=\"%s\"",
				index, (unsigned)vid, (unsigned)ssvid, serial, model, fw);
			return true;
		}

		bool DisableInterruptCoalescing() {
			NVMe_Command cmd{};
			NVMe_Completion cpl{};
			cmd.opc = NVME_ADMIN_OPC_SET_FEATURES;
			cmd.cdw10 = NVME_FEAT_INTERRUPT_COALESCING;
			cmd.cdw11 = 0;
			if (!SubmitAdminCommand(cmd, cpl)) {
				plogwarn("[NVMe] disable interrupt coalescing failed");
				return false;
			}
			plogtrac("[NVMe] interrupt coalescing disabled");
			return true;
		}

		bool IdentifyNamespaceList() {
			if (!identify_nslist) return false;
			if (!SupportsActiveNamespaceList()) {
				return PopulateNamespacesFromController();
			}
			ResetNamespaces();
			uint32 cursor = 0;
			for (;;) {
				NVMe_Command cmd{};
				NVMe_Completion cpl{};
				cmd.opc = NVME_ADMIN_OPC_IDENTIFY;
				cmd.nsid = cursor;
				cmd.prp1 = admin_workspace.device_address + 3 * NVME_ADMIN_PAGE_SIZE;
				cmd.cdw10 = 2; // CNS = Active Namespace ID List
				MemSet(identify_nslist, 0, NVME_ADMIN_PAGE_SIZE);
				if (!SubmitAdminCommand(cmd, cpl)) {
					if (!cursor && controller_nn) {
						plogwarn("[NVMe] namespace list unsupported, fallback to nn=%u", (unsigned)controller_nn);
						return PopulateNamespacesFromController();
					}
					return namespaces.Count() != 0;
				}
				auto* nsids = reinterpret_cast<uint32*>(identify_nslist);
				stduint page_count = 0;
				uint32 last = cursor;
				bool page_valid = true;
				for0(i, NVME_ADMIN_PAGE_SIZE / sizeof(uint32)) {
					if (!nsids[i]) break;
					if (nsids[i] <= last) {
						plogwarn("[NVMe] invalid namespace list order after nsid=%[32H]", last);
						page_valid = false;
						break;
					}
					if (!AddNamespaceId(nsids[i])) {
						plogwarn("[NVMe] namespace allocation failed at nsid=%[32H]", nsids[i]);
						page_valid = false;
						break;
					}
					last = nsids[i];
					++page_count;
				}
				if (!page_valid || !page_count || page_count < NVME_ADMIN_PAGE_SIZE / sizeof(uint32)) break;
				cursor = last;
			}
			plogtrac("[NVMe] active namespaces=%u first=%[32H]",
				(unsigned)namespaces.Count(),
				namespaces.Count() && namespaces[0] ? namespaces[0]->nsid : 0u);
			return namespaces.Count() != 0;
		}

		bool CreateIoQueues() {
			if (!AllocateIoBuffers()) {
				plogwarn("[NVMe] io buffer allocation failed");
				return false;
			}
			io_depth = uint16(minof(admin_depth, NVME_IO_DEPTH_LIMIT));
			if (io_depth < 2) {
				plogwarn("[NVMe] invalid io depth %u", (unsigned)io_depth);
				return false;
			}
			io_sq_tail = 0;
			io_cq_head = 0;
			io_cq_phase = 1;

			NVMe_Command cmd{};
			NVMe_Completion cpl{};

			cmd.opc = NVME_ADMIN_OPC_CREATE_IO_CQ;
			io_workspace.MarkInFlight();
			cmd.prp1 = io_workspace.device_address + NVME_ADMIN_PAGE_SIZE;
			cmd.cdw10 = uint32(NVME_IO_QID) | (uint32(io_depth - 1) << 16);
			cmd.cdw11 = msi_enabled ? 0x00000003u : 0x00000001u;
			if (msi_enabled) {
				regs->intmc = 0xFFFFFFFFu;
				nvme_fence();
			}
			if (!SubmitAdminCommand(cmd, cpl)) return false;

			cmd = {};
			cmd.opc = NVME_ADMIN_OPC_CREATE_IO_SQ;
			cmd.prp1 = io_workspace.device_address;
			cmd.cdw10 = uint32(NVME_IO_QID) | (uint32(io_depth - 1) << 16);
			cmd.cdw11 = 0x00000001u | (uint32(NVME_IO_QID) << 16);
			if (!SubmitAdminCommand(cmd, cpl)) return false;

			plogtrac("[NVMe] ioq qid=%u depth=%u sq=%p cq=%p",
				(unsigned)NVME_IO_QID, (unsigned)io_depth, io_sq, io_cq);
			return true;
		}

		bool IdentifyNamespace(NamespaceInfo& ns) {
			if (!identify_ns || !ns.nsid) return false;
			NVMe_Command cmd{};
			NVMe_Completion cpl{};
			cmd.opc = NVME_ADMIN_OPC_IDENTIFY;
			cmd.nsid = ns.nsid;
			cmd.prp1 = io_workspace.device_address + 2 * NVME_ADMIN_PAGE_SIZE;
			cmd.cdw10 = 0; // CNS = Identify Namespace
			MemSet(identify_ns, 0, NVME_ADMIN_PAGE_SIZE);
			if (!SubmitAdminCommand(cmd, cpl)) return false;

			ns.total_blocks = LoadLe64(identify_ns + 0);
			const byte flbas = identify_ns[26];
			const byte format_index = flbas & 0x0Fu;
			const stduint lbaf_offset = 128 + stduint(format_index) * 4;
			if (lbaf_offset + 3 >= NVME_ADMIN_PAGE_SIZE) {
				plogwarn("[NVMe] invalid LBAF index %u", (unsigned)format_index);
				return false;
			}
			const byte lbads = identify_ns[lbaf_offset + 2];
			ns.block_size = 1u << lbads;
			ns.disk.Bind(this, ns.nsid, ns.block_size, ns.total_blocks, ExecBlocksThunk);
			plogtrac("[NVMe] nsid=%[32H] nsze=%[64H] flbas=%u lbads=%u block=%u",
				ns.nsid, ns.total_blocks, (unsigned)format_index, (unsigned)lbads, (unsigned)ns.block_size);
			return ns.block_size != 0 && ns.total_blocks != 0;
		}

		bool PrepareDataPrps(NVMe_Command& cmd, byte* data_buf, uint32 block_size,
			stduint want_blocks, stduint& chunk_blocks) {
			if (!data_buf || data_buf != data_buffer.virt || !block_size || !want_blocks) return false;
			const uint64 start_addr = data_buffer.device_address;
			const stduint page_mask = NVME_ADMIN_PAGE_SIZE - 1;
			const stduint page_off = stduint(start_addr & page_mask);
			const stduint first_page_bytes = NVME_ADMIN_PAGE_SIZE - page_off;
			const stduint prp_entry_cap = NVME_ADMIN_PAGE_SIZE / sizeof(uint64);
			const uint64 max_chunk_bytes =
				uint64(first_page_bytes) + uint64(prp_entry_cap) * NVME_ADMIN_PAGE_SIZE;
			const uint64 want_bytes = uint64(want_blocks) * block_size;
			const uint64 chunk_bytes64 = minof(want_bytes, max_chunk_bytes);
			chunk_blocks = stduint(chunk_bytes64 / block_size);
			if (!chunk_blocks) return false;

			const uint64 chunk_bytes = uint64(chunk_blocks) * block_size;
			if (chunk_bytes > data_buffer.size) return false;
			cmd.prp1 = start_addr;
			cmd.prp2 = 0;
			if (chunk_bytes <= first_page_bytes) {
				return true;
			}

			const uint64 remaining_bytes = chunk_bytes - first_page_bytes;
			const stduint extra_pages = stduint(
				(remaining_bytes + NVME_ADMIN_PAGE_SIZE - 1) / NVME_ADMIN_PAGE_SIZE);
			const uint64 second_page_addr =
				(start_addr & ~uint64(page_mask)) + NVME_ADMIN_PAGE_SIZE;
			if (extra_pages == 1) {
				cmd.prp2 = second_page_addr;
				return true;
			}
			if (!io_prp_list || extra_pages > prp_entry_cap) return false;
			for0(i, extra_pages) {
				io_prp_list[i] = second_page_addr + uint64(i) * NVME_ADMIN_PAGE_SIZE;
			}
			cmd.prp2 = io_workspace.device_address + 4 * NVME_ADMIN_PAGE_SIZE;
			return true;
		}

		bool SubmitIoBlocks(uint32 nsid, uint64 lba, byte* data_buf,
			uint32 block_size, stduint block_count, bool is_write, uni::StorageRecoveryResult& recovery) {
			// PRP/argument rejection occurs before DMA submission; the caller can retire its current buffer.
			recovery = {regs && !faulted && io_sq && io_cq && io_depth, true, false};
			if (!data_buf || !block_count || block_count > NVME_IO_MAX_BLOCKS) return false;
			NVMe_Command cmd{};
			NVMe_Completion cpl{};
			stduint prepared_blocks = 0;
			if (!PrepareDataPrps(cmd, data_buf, block_size, block_count, prepared_blocks)) {
				return false;
			}
			if (prepared_blocks != block_count) return false;
			if (!io_submit_log_once) {
				io_submit_log_once = true;
				// plogwarn("[NVMe] io submit nsid=%[32H] lba=%[64H] blocks=%u wr=%u",
				// 	nsid, lba, (unsigned)block_count, (unsigned)is_write);
			}
			cmd.opc = is_write ? NVME_NVM_OPC_WRITE : NVME_NVM_OPC_READ;
			cmd.nsid = nsid;
			cmd.cdw10 = uint32(lba);
			cmd.cdw11 = uint32(lba >> 32);
			cmd.cdw12 = uint32(block_count - 1);
			if (!SubmitIoCommand(cmd, cpl, recovery)) return false;
			// ploginfo("[NVMe] io complete cid=%u sqh=%u sqid=%u dw0=%[32H] wr=%u",
			// 	(unsigned)cpl.cid, (unsigned)cpl.sq_head, (unsigned)cpl.sq_id, cpl.dw0, (unsigned)is_write);
			return true;
		}

		bool ExecuteBlocks(uint32 nsid, uint64 lba, void* data_buf, stduint block_count, bool is_write) {
			auto* ns = FindNamespace(nsid);
			if (!data_buf || !block_count || !ns || !ns->block_size) return false;
			if (lba >= ns->total_blocks || block_count > ns->total_blocks - lba) return false;
			MutexLocal guard(&transaction);
			if (faulted) return false;

			byte* user_buf = reinterpret_cast<byte*>(data_buf);
			uint64 current_lba = lba;
			stduint remaining_blocks = block_count;
			// Each command is bounded by PRP-list coverage and CDW12.NLB; its data buffer grows on demand.
			const stduint max_bytes = (1 + NVME_ADMIN_PAGE_SIZE / sizeof(uint64)) * NVME_ADMIN_PAGE_SIZE;
			const stduint max_blocks = minof(max_bytes / ns->block_size, NVME_IO_MAX_BLOCKS);
			if (!max_blocks) return false;
			while (remaining_blocks) {
				const stduint chunk_blocks = minof(remaining_blocks, max_blocks);
				const stduint chunk_bytes = chunk_blocks * ns->block_size;
				if (!data_buffer.Ensure(node, chunk_bytes)) return false;
				if (is_write) MemCopyN(data_buffer.virt, user_buf, chunk_bytes);
				data_buffer.MarkInFlight();
				uni::StorageRecoveryResult recovery{};
				const bool completed = SubmitIoBlocks(nsid, current_lba, data_buffer.virt, ns->block_size, chunk_blocks, is_write, recovery);
				if (recovery.requests_quiesced) data_buffer.MarkQuiesced();
				if (!completed) return false;
				if (!is_write) MemCopyN(user_buf, data_buffer.virt, chunk_bytes);
				user_buf += chunk_bytes;
				current_lba += chunk_blocks;
				remaining_blocks -= chunk_blocks;
			}
			return true;
		}

		void RegisterStorageNode(NamespaceInfo& ns) {
			if (!node || !ns.nsid || ns.device_node) return;
			String name = String::newFormat("nvme-disk@%un%u",
				index, (stduint)ns.nsid);
			ns.device_node = Devsman::RegisterStorageDevice(
				node, name.reference(), DeviceBusType::PCI, "nvme-disk", &ns.disk);
			String alias = String::newFormat("nvme%un%u",
				index, (stduint)ns.nsid);
			Devsman::RegisterDevAlias(ns.device_node, alias.reference());
		}

		void ParsePartitions(NamespaceInfo& ns) {
			if (ns.disk.hd_info_valid) return;
			DiscPartition::Partition(ns.disk, ns.disk.hd_info, sector_buf, 0);
			ns.disk.hd_info_valid = true;
		}

		void RegisterPartitionNodes(NamespaceInfo& ns) {
			if (!ns.device_node) return;
			auto& hdinfo = ns.disk.hd_info;
			for (stduint part_dev = 1; part_dev <= hdinfo.part_count; ++part_dev) {
				uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
				if (slice.sys_id == ClassicFilesysType::None || slice.sys_id == ClassicFilesysType::EXT || slice.length == 0) continue;
				String name = String::newFormat("partition@%u", part_dev);
				Devsman::RegisterStoragePartition(ns.device_node, name.reference(), ns.disk, part_dev);
			}
		}

		void MountPartitions(NamespaceInfo& ns) {
			auto& hdinfo = ns.disk.hd_info;
			for (stduint part_dev = 1; part_dev <= hdinfo.part_count; ++part_dev) {
				uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
				if (slice.sys_id == ClassicFilesysType::None) continue;
				if (slice.sys_id == ClassicFilesysType::EXT) continue;
				String node_name = String::newFormat("partition@%u", part_dev);
				DeviceNode* part_node = Devsman::RegisterStoragePartition(
					ns.device_node, node_name.reference(), ns.disk, part_dev);
				String lab = String::newFormat("/mnt/nvme%un%u.%u",
					index, (stduint)ns.nsid, part_dev);
				if (auto fs = Filesys::Mount(ns.disk, part_dev, lab.reference(), part_node)) {
					ploginfo("[NVMe] mount %s on %s", fs->name, lab.reference());
				}
			}
		}

		bool ReadLba0(NamespaceInfo& ns) {
			if (!sector_buf || ns.block_size == 0) return false;
			DeviceNode* storage_node = ns.device_node;
			stduint node_block_size = 0;
			if (!storage_node) return false;
			if (Devsman::Ctrl(storage_node, (stduint)DeviceCtrlCommand::GetBlockSize, &node_block_size) != 0) {
				plogwarn("[NVMe] ctrl nsid=%[32H] get block size failed", ns.nsid);
				return false;
			}
			if (node_block_size == 0) return false;
			// ploginfo("[NVMe] probe lba0 begin");
			MemSet(sector_buf, 0, NVME_ADMIN_PAGE_SIZE);
			if (Devsman::Read(storage_node, sector_buf, node_block_size, 0, 0) != (stdsint)node_block_size) {
				plogwarn("[NVMe] read nsid=%[32H] lba0 failed", ns.nsid);
				return false;
			}
			if (node_block_size >= 512) {
				// ploginfo("[NVMe] lba0 sig=%02X%02X bytes=%02X %02X %02X %02X %02X %02X %02X %02X",
				// 	(unsigned)sector_buf[511], (unsigned)sector_buf[510],
				// 	(unsigned)sector_buf[0], (unsigned)sector_buf[1],
				// 	(unsigned)sector_buf[2], (unsigned)sector_buf[3],
				// 	(unsigned)sector_buf[4], (unsigned)sector_buf[5],
				// 	(unsigned)sector_buf[6], (unsigned)sector_buf[7]);
			}
			else {
				// ploginfo("[NVMe] lba0 first=%02X %02X %02X %02X %02X %02X %02X %02X",
				// 	(unsigned)sector_buf[0], (unsigned)sector_buf[1],
				// 	(unsigned)sector_buf[2], (unsigned)sector_buf[3],
				// 	(unsigned)sector_buf[4], (unsigned)sector_buf[5],
				// 	(unsigned)sector_buf[6], (unsigned)sector_buf[7]);
			}
			return true;
		}

		void EnumerateNamespaces() {
			for (auto& owner : namespaces) {
				if (!owner) continue;
				auto& ns = *owner;
				if (!IdentifyNamespace(ns)) continue;
				RegisterStorageNode(ns);
				if (!ReadLba0(ns)) continue;
				ParsePartitions(ns);
				RegisterPartitionNodes(ns);
				MountPartitions(ns);
			}
		}
	};

	uni::Vector<uni::SharedPtr<NVMe_Controller>> nvme_controllers;

	NVMe_Controller* FindNvmeController(DeviceNode* node) {
		for (auto& owner : nvme_controllers) {
			if (owner && owner->node == node) return owner.get();
		}
		return nullptr;
	}

	void Handint_NVME() {
		// plogerro(">>>");
		for (auto& owner : nvme_controllers) {
			if (!owner || !owner->regs || !owner->msi_enabled) continue;
			if (!owner->io_wait_active.load(uni::MemoryOrder_Acquire)) continue;
			// Controllers share this MSI vector; each waiter checks its own CQ before accepting completion.
			Devs::NotifyStorageWait(owner->completion_wait);
			++owner->irq_count;
			if (!owner->irq_log_once) {
				owner->irq_log_once = true;
				// plogwarn("[NVMe] irq received vector=%u",
				// 	(unsigned)IRQ_NVME);
			}
		}
		IC.SendEOI(IRQ_NVME);
	}

	_ESYM_C void Handint_NVME_Entry();

	static uni::PCI::Device make_pci_device(const DeviceNode& node) {
		uni::PCI::Device dev{};
		dev.segment_group = node.fields.pci_segment;
		dev.bus = node.fields.pci_bus;
		dev.device = node.fields.pci_device;
		dev.function = node.fields.pci_function;
		dev.header_type = pci.read_header_type(dev);
		dev.class_code.base = node.fields.class_base;
		dev.class_code.sub = node.fields.class_sub;
		dev.class_code.interface = node.fields.class_if;
		return dev;
	}
}

static bool start_nvme_driver(DeviceNode* nvme_node) {
	if (!nvme_node) return false;
	if (NVMe_Controller* existing = FindNvmeController(nvme_node)) {
		nvme_node->fields.binding.driver_data = existing;
		return existing->started && !existing->faulted;
	}
	auto dev = make_pci_device(*nvme_node);
	pci.enable_MMIO(dev);
	auto owner = uni::make_shared<NVMe_Controller>();
	if (!owner) return false;
	NVMe_Controller* controller = owner.get();
	controller->index = nvme_controllers.Count();
	if (!controller->Bind(nvme_node)) {
		plogwarn("[NVMe] Failed to bind controller %s",
			nvme_node->link.addr ? nvme_node->link.addr : "(unnamed)");
		return false;
	}
	// Keep the controller and its DMA ownership alive even if initialization or a later stop fails.
	const bool interrupts_enabled = IC.TryMaskInterrupt();
	const bool retained = nvme_controllers.Append(owner);
	if (interrupts_enabled) IC.enInterrupt(true);
	if (!retained) return false;
	controller->DumpSummary();
	if (!controller->ConfigureAdminQueue()) {
		return false;
	}
	if (!controller->IdentifyController()) {
		return false;
	}
	(void)controller->DisableInterruptCoalescing();
	const bool namespaces_ready = controller->IdentifyNamespaceList();
	if (controller->faulted) return false;
	nvme_node->fields.binding.driver_data = controller;
	if (!controller->ConfigureInterrupts(dev)) {
		plogwarn("[NVMe] keep polling completion");
	}
	if (!namespaces_ready) {
		plogwarn("[NVMe] no active namespace reported");
	}
	else if (controller->CreateIoQueues()) {
		controller->EnumerateNamespaces();
	}
	controller->started = !controller->faulted;
	return controller->started;
}

_ESYM_C void R_NVME_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_NVME{
	.init = R_NVME_INIT,
	.name = "NVMe",
};

void R_NVME_INIT() {
	if (!PCI_Init(pci)) {
		plogwarn("[NVMe] No devices on PCI or PCI init failed.");
	}
	#if _MCCA == 0x8664
	IC[IRQ_NVME].setModeRupt(mglb(Handint_NVME_Entry), SegCo64);
	#else
	IC[IRQ_NVME].setRange(mglb(Handint_NVME_Entry), SegCo32);
	#endif
	register_interrupt_handler(IRQ_NVME, Handint_NVME);
	Devsman::RegisterDriverStarter("nvme", start_nvme_driver);
	Devsman::StartKnownDrivers();
}

#endif
