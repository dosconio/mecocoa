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
#include <c/format/filesys.h>
#include <cpp/Device/DMA>
#include "../../include/taskman.com.hpp"
#include "../../include/syscall-pow.hpp"
#endif
#include "../../include/devsman-storage.hpp"

#if _MCCA == 0x8632
_ESYM_C void R_FLP_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_FLP{
	.init = R_FLP_INIT,
	.name = "DISK-FLOPPY",
};

void R_FLP_INIT() {
	IC[IRQ_Floppy].setRange(mglb(Handint_FLP_Entry), SegCo32);
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
				if (FloppyAckInterrupt(event)) return true;
			}
			else if (message.type == _IMM(KernelMsg::DeviceTimeout) &&
				stduint(event.version) == token) return false;
		}
	}

	bool FloppySend(stduint target, stduint type, const void* data, stduint length) {
		CommMsg message = {};
		message.type = type;
		message.data.address = _IMM(data);
		message.data.length = length;
		return Powercall::SysComm(COMM_SEND, target, &message) == 0;
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

	uni::FloppyDisk* drives[2] = { &drive_a, &drive_b };
	byte drive_types[2] = { info.type_a, info.type_b };
	bool media_present[2] = {};
	for (stduint i = 0; i < 2; ++i) {
		if (!drive_types[i]) continue;
		drives[i]->Reset();
		media_present[i] = drives[i]->IsMediaPresent();
		StorageDriverInfo registration = {};
		registration.dev_handle = floppy_dev_handle;
		registration.unit = i;
		registration.block_size = drives[i]->Block_Size;
		registration.block_count = drives[i]->getUnits();
		registration.flags = StorageDriverFlag_Readable | StorageDriverFlag_Writable |
			(media_present[i] ? StorageDriverFlag_MediaPresent : 0);
		registration.slice_type = FILESYS_FAT12;
		if (i == 0) MemCopyN(registration.name, "floppy@0", sizeof("floppy@0"));
		else MemCopyN(registration.name, "floppy@1", sizeof("floppy@1"));
		if (!FloppySend(Task_Devsman, _IMM(StorageDriverMsg::Attach), &registration, sizeof(registration))) {
			printf("flopdisk: attach send failed unit=%u\n\r", unsigned(i));
			return -6;
		}
		StorageDriverReply result = {};
		CommMsg reply = {};
		if (!FloppyReceive(Task_Devsman, &result, sizeof(result), reply) ||
			reply.type != _IMM(StorageDriverMsg::Attach) ||
			result.version != StorageDriverProtocolVersion || result.status != 0) {
			printf("flopdisk: attach reply failed unit=%u status=%d type=%u\n\r",
				unsigned(i), int(result.status), unsigned(reply.type));
			return -7;
		}
	}

	for (;;) {
		byte payload[512] = {};
		CommMsg message = {};
		if (!FloppyReceive(ANYPROC, payload, sizeof(payload), message)) {
			printf("flopdisk: receive failed\n\r");
			return -8;
		}
		if (message.type == _IMM(KernelMsg::DeviceEvent)) {
			DeviceEvent event = {};
			MemCopyN(&event, payload, sizeof(event));
			(void)FloppyAckInterrupt(event);
			continue;
		}
		if (message.src >= Task_Init ||
			(message.type != _IMM(StorageDriverMsg::Read) &&
			message.type != _IMM(StorageDriverMsg::Write))) continue;
		StorageDriverRequest request = {};
		MemCopyN(&request, payload, sizeof(request));
		StorageDriverReply result = {};
		const bool valid = request.version == StorageDriverProtocolVersion &&
			request.unit < 2 && drive_types[request.unit] && media_present[request.unit] &&
			request.block < drives[request.unit]->getUnits();
		if (message.type == _IMM(StorageDriverMsg::Read)) {
			if (valid && drives[request.unit]->Read(request.block, payload)) {
				result.status = 0;
				result.bytes = 512;
			}
			if (!FloppySend(message.src, _IMM(StorageDriverMsg::Complete), &result, sizeof(result))) return -9;
			if (!result.status && !FloppySend(message.src, _IMM(StorageDriverMsg::Data), payload, 512)) return -10;
			continue;
		}
		if (!valid) {
			if (!FloppySend(message.src, _IMM(StorageDriverMsg::Ready), &result, sizeof(result))) return -11;
			continue;
		}
		result.status = 0;
		result.bytes = 512;
		if (!FloppySend(message.src, _IMM(StorageDriverMsg::Ready), &result, sizeof(result))) return -11;
		CommMsg data_message = {};
		MemSet(payload, 0, sizeof(payload));
		if (!FloppyReceive(message.src, payload, 512, data_message) ||
			data_message.type != _IMM(StorageDriverMsg::Data)) return -12;
		result.status = drives[request.unit]->Write(request.block, payload) ? 0 : -1;
		result.bytes = result.status ? 0 : 512;
		if (!FloppySend(message.src, _IMM(StorageDriverMsg::Complete), &result, sizeof(result))) return -13;
	}
}
#endif
