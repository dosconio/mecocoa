// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Disk - Floppy
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#if defined(_MCCA) && (_MCCA == 0x8632)
#include "../../include/mecocoa.hpp"
#include <c/storage/floppy.h>
#include <c/format/filesys.h>
#elif defined(_ACCM) && ((_ACCM & 0xFF00) == 0x8600)
#include <stdio.h>
#include <c/stdinc.h>
#include <c/storage/floppy.h>
#include <cpp/Device/DMA>
#include "../../include/taskman.com.hpp"
#include "../../include/syscall-pow.hpp"
#endif
#include "../../include/flopdisk.com.hpp"

#if _MCCA == 0x8632
_ESYM_C void R_FLP_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_FLP{
	.init = R_FLP_INIT,
	.name = "DISK-FLOPPY",
};

using namespace uni;

static DeviceNode* floppy_nodes[2] = { nullptr, nullptr };

PartitionSlice uni::FloppyDisk::getSlice(stduint dev) {
	PartitionSlice slice;
	slice.address = 0;
	slice.length = getUnits();
	slice.sys_id = FILESYS_FAT12;
	return slice;
}

struct FloppyDisk_Paged : public uni::FloppyDisk {
	FloppyDisk_Paged(byte _id = 0, FloppyDriveType type = FloppyDriveType::Drive_1_44MB_3_5) : FloppyDisk(_id, type) {}
	virtual bool Read(stduint BlockIden, void* Dest, stduint Times = 1) override;
	virtual bool Write(stduint BlockIden, const void* Sors, stduint Times = 1) override;
};

static stduint floppy_driver_tid = 0;

bool FloppyDisk_Paged::Read(stduint BlockIden, void* Dest, stduint Times) {
	if (Taskman::CurrentPID() == Task_Flp_Serv) {
		for0(t, Times) {
			FloppyDriverRequest request = { getID(), uint32(BlockIden + t) };
			syssend(floppy_driver_tid, &request, sizeof(request), _IMM(FloppyDriverMsg::Read));
			stduint status = 0;
			sysrecv(floppy_driver_tid, &status, sizeof(status));
			if (!status) return false;
			sysrecv(floppy_driver_tid, (byte*)Dest + t * Block_Size, Block_Size);
		}
		return true;
	}
	for0(t, Times) {
		stduint blk = BlockIden + t;
		byte* dst = (byte*)Dest + t * Block_Size;
		stduint to_args[2];
		to_args[0] = getID();
		to_args[1] = blk;
		syssend(Task_Flp_Serv, sliceof(to_args), _IMM(FiledevMsg::READ));
		// Receive ACK before data transfer
		stduint ack;
		sysrecv(Task_Flp_Serv, &ack, sizeof(ack));
		if (!ack) return false;
		sysrecv(Task_Flp_Serv, dst, Block_Size);
	}
	return true;
}

bool FloppyDisk_Paged::Write(stduint BlockIden, const void* Sors, stduint Times) {
	if (Taskman::CurrentPID() == Task_Flp_Serv) {
		for0(t, Times) {
			FloppyDriverRequest request = { getID(), uint32(BlockIden + t) };
			syssend(floppy_driver_tid, &request, sizeof(request), _IMM(FloppyDriverMsg::Write));
			syssend(floppy_driver_tid, (const byte*)Sors + t * Block_Size, Block_Size);
			stduint status = 0;
			sysrecv(floppy_driver_tid, &status, sizeof(status));
			if (!status) return false;
		}
		return true;
	}
	for0(t, Times) {
		stduint blk = BlockIden + t;
		const byte* src = (const byte*)Sors + t * Block_Size;
		stduint to_args[2];
		to_args[0] = getID();
		to_args[1] = blk;
		syssend(Task_Flp_Serv, sliceof(to_args), _IMM(FiledevMsg::WRITE));
		// Receive ACK before data transfer
		stduint ack;
		sysrecv(Task_Flp_Serv, &ack, sizeof(ack));
		if (!ack) return false;
		syssend(Task_Flp_Serv, src, Block_Size);
		sysrecv(Task_Flp_Serv, &ack, sizeof(ack));
		if (!ack) return false;
	}
	return true;
}

void R_FLP_INIT() {
	IC[IRQ_Floppy].setRange(mglb(Handint_FLP_Entry), SegCo32);
}

static stduint args[4];
FloppyDisk_Paged* paged_floppies[2] = { nullptr, nullptr };
static char paged_flp_buf[sizeof(FloppyDisk_Paged) * 2];

const char* drive_type_names[] = {
	"None",
	"360KB 5.25\"",
	"1.2MB 5.25\"",
	"720KB 3.5\"",
	"1.44MB 3.5\"",
	"2.88MB 3.5\""
};

void serv_dev_fl_loop()
{
	stduint sig_type = 0, sig_src;
	String lab;
	while (true) {
		sysrecv(ANYPROC, sliceof(args), &sig_type, &sig_src);
		if (sig_type == _IMM(FloppyDriverMsg::Attach)) {
			FloppyDriverAttach attach = {};
			MemCopyN(&attach, args, sizeof(attach));
			floppy_driver_tid = sig_src;
			stduint ready = 1;
			for0(i, 2) {
				if (!attach.drive_type[i] || attach.drive_type[i] > 5) continue;
				paged_floppies[i] = new (paged_flp_buf + i * sizeof(FloppyDisk_Paged))
					FloppyDisk_Paged(i, static_cast<FloppyDriveType>(attach.drive_type[i]));
				floppy_nodes[i] = Devsman::FindNamedNode(DeviceNodeType::StorageDevice,
					i ? "floppy@1" : "floppy@0");
				if (floppy_nodes[i]) Devsman::AttachStorageOps(floppy_nodes[i], paged_floppies[i]);
			}
			syssend(sig_src, &ready, sizeof(ready));
			for0(i, 2) {
				if (!paged_floppies[i] || !attach.media_present[i]) continue;
				ploginfo("[Floppy] Detect Floppy on Drive %c: %u KB (%s)",
					'A' + i, stduint(paged_floppies[i]->getUnits() * 512 / 1024),
					drive_type_names[attach.drive_type[i]]);
				lab = String::newFormat("/mnt/fl%d", i);
				if (Filesys::Mount(*paged_floppies[i], 0, lab.reference(), floppy_nodes[i]))
					ploginfo("[Floppy] Mounted on %s successfully", lab.reference());
			}
			continue;
		}
		switch ((FiledevMsg)sig_type)
		{
		case FiledevMsg::TEST:// (no-feedback)
			break;
		case FiledevMsg::RUPT:// (usercall-forbidden, no feedback)
			break;
		case FiledevMsg::CLOSE:// [diskno]
			break;
		case FiledevMsg::READ:// [diskno, lba]
		{
			stduint ack = 0;
			char sector[512] = {};
			if (args[0] < 2 && paged_floppies[args[0]] && floppy_driver_tid)
				ack = paged_floppies[args[0]]->Read(args[1], sector) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) syssend(sig_src, sector, sizeof(sector));
			break;
		}
		case FiledevMsg::WRITE:// [diskno, lba]
		{
			stduint ack = (args[0] < 2 && paged_floppies[args[0]] && floppy_driver_tid) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) {
				char sector[512] = {};
				sysrecv(sig_src, sector, sizeof(sector));
				ack = paged_floppies[args[0]]->Write(args[1], sector) ? 1 : 0;
				syssend(sig_src, &ack, sizeof(ack));
			}
			break;
		}
		case FiledevMsg::GETPS:
			if (args[0] < 2 && paged_floppies[args[0]] && sig_src) {
				PartitionSlice slice = paged_floppies[args[0]]->getSlice(0);
				syssend(sig_src, &slice, sizeof(slice));
			}
			break;
		default:
			plogerro("Bad TYPE in %s %s", __FILE__, __FUNCIDEN__);
			break;
		}
	}
}
#elif defined(_ACCM) && ((_ACCM & 0xFF00) == 0x8600)
struct FloppyInfo {
	bool has_drive_a;
	bool has_drive_b;
	byte type_a;
	byte type_b;
	int count;
};

FloppyInfo DetectFloppyDrives() {
	FloppyInfo info = {false, false, 0, 0, 0};
	outpb(0x70, 0x10);
	byte cmos_val = innpb(0x71);
	info.type_a = cmos_val >> 4;
	info.type_b = cmos_val & 0x0F;
	if (info.type_a != 0) {
		info.has_drive_a = true;
		info.count++;
	}
	if (info.type_b != 0) {
		info.has_drive_b = true;
		info.count++;
	}
	return info;
}

namespace {
	stduint floppy_dev_handle = 0;
	stduint floppy_wait_token = 0;
	stduint floppy_dma_handle = 0;
	stduint floppy_dma_physical = 0;
	byte* floppy_dma_buffer = nullptr;

	void FloppyDmaWrite8(void*, uint16 port, byte value) {
		outpb(port, value);
	}

	const uni::DMA8237_IO floppy_dma_io{nullptr, FloppyDmaWrite8};
	const uni::DMA8237_t floppy_dma{floppy_dma_io};

	bool FloppyPrepareDma(bool write) {
		if (!floppy_dma_buffer) return false;
		return floppy_dma.Transfer(2, floppy_dma_physical, 512,
			write ? uni::DMA8237Direction::MemoryToDevice : uni::DMA8237Direction::DeviceToMemory);
	}

	bool FloppyInitDma(stduint device_handle, uni::FloppyDisk& disk) {
		const stdsint handle = Powercall::DevDmaAlloc(device_handle, 4096);
		if (handle <= 0) {
			printf("flopdisk: DMA allocation failed handle=%d\n\r", int(handle));
			return false;
		}
		PwcallDeviceDmaMapRequest mapping = {};
		mapping.map_flags = _IMM(PwcallDeviceDmaMapFlag::Writable);
		const stdsint mapped = Powercall::DevDmaMap(stduint(handle), &mapping);
		if (mapped != 0 ||
			!mapping.address || !mapping.physical || mapping.length < 512 ||
			mapping.physical > 0x00FFFE00u) {
			printf("flopdisk: DMA map failed rc=%d phys=%x addr=%x len=%u\n\r",
				int(mapped), unsigned(mapping.physical), unsigned(mapping.address), unsigned(mapping.length));
			Powercall::DevDmaFree(stduint(handle));
			return false;
		}
		floppy_dma_handle = stduint(handle);
		floppy_dma_physical = stduint(mapping.physical);
		floppy_dma_buffer = reinterpret_cast<byte*>(stduint(mapping.address));
		disk.Block_buffer = floppy_dma_buffer;
		disk.fn_dma_prepare = FloppyPrepareDma;
		disk.io_method = uni::IOMethod::DMA;
		return true;
	}

	bool FloppyAckInterrupt(const DeviceEvent& event) {
		if (event.version != DeviceEventProtocolVersion ||
			DeviceEventKind(event.kind) != DeviceEventKind::Interrupt ||
			event.device_handle != floppy_dev_handle) return false;
		return Powercall::DevAck(floppy_dev_handle, stduint(event.sequence), event.generation) == 0;
	}

	bool FloppyWaitInterrupt() {
		const stduint token = ++floppy_wait_token;
		if (Powercall::DevTimer(floppy_dev_handle, 100, token) != 0) return false;
		for (;;) {
			DeviceEvent event = {};
			CommMsg message = {};
			message.data.address = _IMM(&event);
			message.data.length = sizeof(event);
			if (Powercall::SysComm(COMM_RECV, ANYPROC, &message) != 0) return false;
			if (message.type == _IMM(KernelMsg::DeviceEvent)) {
				if (message.data.length == sizeof(event) && FloppyAckInterrupt(event)) return true;
			}
			else if (message.type == _IMM(KernelMsg::DeviceTimeout) &&
				stduint(event.version) == token) return false;
		}
	}

	bool FloppySend(stduint type, const void* data, stduint length) {
		CommMsg message = {};
		message.type = type;
		message.data.address = _IMM(data);
		message.data.length = length;
		return Powercall::SysComm(COMM_SEND, Task_Flp_Serv, &message) == 0;
	}

	bool FloppyReceive(stduint source, void* data, stduint length, CommMsg& message) {
		message = {};
		message.data.address = _IMM(data);
		message.data.length = length;
		return Powercall::SysComm(COMM_RECV, source, &message) == 0;
	}
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	if (Powercall::Hello() != 0) {
		printf("flopdisk: hello failed\n\r");
		return -2;
	}
	const FloppyInfo info = DetectFloppyDrives();
	if (!info.count) return 0;
	const stdsint opened = Powercall::DevOpen(0, 0, _IMM(PwcallDeviceOpenFlag::Interrupt));
	if (opened <= 0) {
		printf("flopdisk: DevOpen failed rc=%d drives=%u/%u\n\r",
			int(opened), unsigned(info.type_a), unsigned(info.type_b));
		return -3;
	}
	floppy_dev_handle = stduint(opened);

	uni::FloppyDisk drive_a(0, static_cast<uni::FloppyDriveType>(info.type_a));
	uni::FloppyDisk drive_b(1, static_cast<uni::FloppyDriveType>(info.type_b));
	if (!FloppyInitDma(floppy_dev_handle, drive_a)) return -4;
	drive_b.Block_buffer = floppy_dma_buffer;
	drive_b.fn_dma_prepare = FloppyPrepareDma;
	drive_b.io_method = uni::IOMethod::DMA;
	drive_a.fn_int_wait = FloppyWaitInterrupt;
	drive_b.fn_int_wait = FloppyWaitInterrupt;
	drive_a.react_type = uni::FloppyDisk::ReactType::Rupt;
	drive_b.react_type = uni::FloppyDisk::ReactType::Rupt;
	if (Powercall::DevPublish(floppy_dev_handle, PwcallDevicePublishCommand::Started) != 0) {
		printf("flopdisk: DevPublish failed handle=%u\n\r", unsigned(floppy_dev_handle));
		return -5;
	}

	FloppyDriverAttach attach = {};
	attach.drive_type[0] = info.type_a;
	attach.drive_type[1] = info.type_b;
	uni::FloppyDisk* drives[2] = { &drive_a, &drive_b };
	for (stduint i = 0; i < 2; ++i) {
		if (!attach.drive_type[i]) continue;
		drives[i]->Reset();
		attach.media_present[i] = drives[i]->IsMediaPresent() ? 1 : 0;
	}
	if (!FloppySend(_IMM(FloppyDriverMsg::Attach), &attach, sizeof(attach))) {
		printf("flopdisk: attach send failed\n\r");
		return -6;
	}
	stduint ready = 0;
	CommMsg reply = {};
	if (!FloppyReceive(Task_Flp_Serv, &ready, sizeof(ready), reply) ||
		reply.data.length != sizeof(ready) || !ready) {
		printf("flopdisk: attach reply failed ready=%u len=%u type=%u\n\r",
			unsigned(ready), unsigned(reply.data.length), unsigned(reply.type));
		return -7;
	}

	for (;;) {
		byte payload[512] = {};
		CommMsg message = {};
		if (!FloppyReceive(ANYPROC, payload, sizeof(payload), message)) {
			printf("flopdisk: receive failed\n\r");
			return -8;
		}
		if (message.type == _IMM(KernelMsg::DeviceEvent) && message.data.length == sizeof(DeviceEvent)) {
			DeviceEvent event = {};
			MemCopyN(&event, payload, sizeof(event));
			(void)FloppyAckInterrupt(event);
			continue;
		}
		if (message.src != Task_Flp_Serv ||
			(message.type != _IMM(FloppyDriverMsg::Read) &&
			message.type != _IMM(FloppyDriverMsg::Write))) continue;
		FloppyDriverRequest request = {};
		MemCopyN(&request, payload, sizeof(request));
		stduint status = 0;
		const bool valid = request.drive < 2 && attach.media_present[request.drive] &&
			request.lba < drives[request.drive]->getUnits();
		if (valid) {
			if (message.type == _IMM(FloppyDriverMsg::Read)) {
				status = drives[request.drive]->Read(request.lba, payload) ? 1 : 0;
				if (!FloppySend(message.type, &status, sizeof(status))) return -9;
				if (status && !FloppySend(message.type, payload, 512)) return -10;
				continue;
			}
		}
		if (message.type == _IMM(FloppyDriverMsg::Write)) {
			CommMsg data_message = {};
			if (!FloppyReceive(Task_Flp_Serv, payload, 512, data_message)) return -11;
			if (valid)
				status = drives[request.drive]->Write(request.lba, payload) ? 1 : 0;
		}
		if (!FloppySend(message.type, &status, sizeof(status))) return -12;
	}
}
#endif
