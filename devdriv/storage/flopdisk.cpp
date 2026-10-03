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
	enum class FloppyRequestState : byte {
		Idle,
		Preparing,
		WaitingInterrupt,
		Completed,
		Failed,
	};

	struct FloppyRequestRuntime {
		FloppyRequestState state = FloppyRequestState::Idle;
		stduint source = 0;
		uint32 unit = 0;
		bool cancelled = false;
		stduint wait_token = 0;
	};

	static constexpr stduint FloppyInterruptTimeout = 100;
	static constexpr stduint FloppyMotorStopDelay = 200;
	static constexpr uint32 FloppyRequestRetryLimit = 3;
	static constexpr stduint FloppyTimerSerialMask = 0x0FFFFFFF;
	static constexpr stduint FloppyWaitTimerTag = 0x10000000;
	static constexpr stduint FloppyMotorATimerTag = 0x20000000;
	static constexpr stduint FloppyMotorBTimerTag = 0x30000000;

	stduint floppy_dev_handle = 0;
	stduint floppy_dma_handle = 0;
	stduint floppy_dma_physical = 0;
	byte floppy_dma_channel = 0xFF;
	byte* floppy_dma_buffer = nullptr;
	stduint floppy_timer_serial = 0;
	stduint floppy_motor_stop_token[2] = {};
	uni::FloppyDisk* floppy_drives[2] = {};
	byte floppy_drive_types[2] = {};
	bool floppy_media_present[2] = {};
	bool floppy_device_available = true;
	bool floppy_recovery_required = false;
	bool floppy_shutdown_requested = false;
	FloppyRequestRuntime floppy_request = {};

	void FloppyDmaWrite8(void*, uint16 port, byte value) {
		outpb(port, value);
	}

	const uni::DMA8237_IO floppy_dma_io{nullptr, FloppyDmaWrite8};
	const uni::DMA8237_t floppy_dma{floppy_dma_io};

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

	stduint FloppyNextTimerToken(stduint tag) {
		floppy_timer_serial = (floppy_timer_serial + 1) & FloppyTimerSerialMask;
		if (!floppy_timer_serial) floppy_timer_serial = 1;
		return tag | floppy_timer_serial;
	}

	stduint FloppyMessageToken(const void* payload) {
		stduint token = 0;
		MemCopyN(&token, payload, sizeof(token));
		return token;
	}

	bool FloppyFindDmaChannel(stduint device_handle, byte& channel) {
		const stdsint count = Powercall::DevGetResourceCount(device_handle);
		if (count <= 0) return false;
		for (stdsint i = 0; i < count; ++i) {
			PwcallDeviceResourceQuery query = {};
			query.index = uint32(i);
			if (Powercall::DevGetResource(device_handle, &query) != 0 ||
				PwcallDeviceResourceType(query.resource.type) != PwcallDeviceResourceType::DmaChannel)
				continue;
			if (query.resource.length != 1 || query.resource.start > 3 ||
				query.resource.extra != 8) return false;
			channel = byte(query.resource.start);
			return true;
		}
		return false;
	}

	bool FloppyPrepareDma(bool write) {
		if (!floppy_dma_buffer || floppy_dma_channel > 3 ||
			floppy_request.state != FloppyRequestState::Preparing) return false;
		return floppy_dma.Transfer(floppy_dma_channel, floppy_dma_physical, 512,
			write ? uni::DMA8237Direction::MemoryToDevice : uni::DMA8237Direction::DeviceToMemory);
	}

	bool FloppyInitDma(stduint device_handle, uni::FloppyDisk& disk) {
		if (!FloppyFindDmaChannel(device_handle, floppy_dma_channel)) {
			printf("flopdisk: invalid DMA resource\n\r");
			return false;
		}
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
			mapping.physical > 0x00FFFE00u ||
			(mapping.physical & 0xFFFFu) + 512 > 0x10000u) {
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

	void FloppyAbortTransfer() {
		if (floppy_dma_channel <= 3) (void)floppy_dma.Abort(floppy_dma_channel);
	}

	void FloppyStopMotors() {
		for0(i, 2) {
			floppy_motor_stop_token[i] = 0;
			if (floppy_drives[i] && floppy_drive_types[i]) floppy_drives[i]->Motor(false);
		}
	}

	void FloppyHandleMotorTimeout(stduint token) {
		for0(i, 2) {
			if (!token || floppy_motor_stop_token[i] != token) continue;
			floppy_motor_stop_token[i] = 0;
			if (floppy_request.state != FloppyRequestState::Idle && floppy_request.unit == i) return;
			if (floppy_drives[i]) floppy_drives[i]->Motor(false);
			return;
		}
	}

	void FloppyReleaseMotor(uni::FloppyDisk* disk) {
		if (!disk || disk->getID() >= 2) return;
		const stduint unit = disk->getID();
		const stduint tag = unit ? FloppyMotorBTimerTag : FloppyMotorATimerTag;
		const stduint token = FloppyNextTimerToken(tag);
		floppy_motor_stop_token[unit] = token;
		if (Powercall::DevTimer(floppy_dev_handle, FloppyMotorStopDelay, token) == 0) return;
		floppy_motor_stop_token[unit] = 0;
		disk->Motor(false);
	}

	bool FloppyHandleDeviceEvent(const DeviceEvent& event) {
		if (event.version != DeviceEventProtocolVersion ||
			event.device_handle != floppy_dev_handle) return false;
		switch (DeviceEventKind(event.kind)) {
		case DeviceEventKind::Interrupt:
			if (!FloppyAckInterrupt(event)) return true;
			if ((event.flags & (DeviceEventFlag_Coalesced | DeviceEventFlag_Overflow)) ||
				event.count != 1) {
				(void)Powercall::DevAck(floppy_dev_handle, 0, 0);
				floppy_recovery_required = true;
				return true;
			}
			if (floppy_request.state == FloppyRequestState::WaitingInterrupt)
				floppy_request.state = FloppyRequestState::Completed;
			else floppy_recovery_required = true;
			return true;
		case DeviceEventKind::Removed:
		case DeviceEventKind::Fault:
		case DeviceEventKind::Shutdown:
			floppy_device_available = false;
			floppy_shutdown_requested = true;
			floppy_media_present[0] = false;
			floppy_media_present[1] = false;
			floppy_request.cancelled = true;
			floppy_request.state = FloppyRequestState::Failed;
			FloppyAbortTransfer();
			FloppyStopMotors();
			return true;
		default:
			return false;
		}
	}

	void FloppyReplyBusy(stduint target, stduint request_type) {
		StorageDriverReply reply = {};
		reply.status = -2;
		const StorageDriverMsg response = request_type == _IMM(StorageDriverMsg::Write) ?
			StorageDriverMsg::Ready : StorageDriverMsg::Complete;
		(void)FloppySend(target, _IMM(response), &reply, sizeof(reply));
	}

	bool FloppyWaitInterrupt() {
		const stduint token = FloppyNextTimerToken(FloppyWaitTimerTag);
		floppy_request.wait_token = token;
		floppy_request.state = FloppyRequestState::WaitingInterrupt;
		if (Powercall::DevTimer(floppy_dev_handle, FloppyInterruptTimeout, token) != 0) {
			floppy_request.state = FloppyRequestState::Failed;
			return false;
		}
		for (;;) {
			byte payload[sizeof(DeviceEvent)] = {};
			CommMsg message = {};
			if (!FloppyReceive(ANYPROC, payload, sizeof(payload), message)) {
				floppy_request.state = FloppyRequestState::Failed;
				return false;
			}
			if (message.type == _IMM(KernelMsg::DeviceEvent)) {
				if (message.data.length < sizeof(DeviceEvent)) {
					floppy_recovery_required = true;
					continue;
				}
				DeviceEvent event = {};
				MemCopyN(&event, payload, sizeof(event));
				(void)FloppyHandleDeviceEvent(event);
				if (floppy_request.state == FloppyRequestState::Completed) return true;
				if (floppy_request.state == FloppyRequestState::Failed) return false;
			}
			else if (message.type == _IMM(KernelMsg::DeviceTimeout)) {
				if (message.data.length < sizeof(stduint)) continue;
				const stduint expired = FloppyMessageToken(payload);
				if (expired == token) {
					floppy_request.state = FloppyRequestState::Failed;
					FloppyAbortTransfer();
					(void)Powercall::DevAck(floppy_dev_handle, 0, 0);
					floppy_recovery_required = true;
					return false;
				}
				FloppyHandleMotorTimeout(expired);
			}
			else if (message.src == floppy_request.source &&
				message.type == _IMM(StorageDriverMsg::Cancel)) {
				floppy_request.cancelled = true;
				floppy_request.state = FloppyRequestState::Failed;
				FloppyAbortTransfer();
				StorageDriverReply reply = {};
				reply.status = 0;
				(void)FloppySend(message.src, _IMM(StorageDriverMsg::Complete), &reply, sizeof(reply));
				return false;
			}
			else if (message.src < Task_Init &&
				(message.type == _IMM(StorageDriverMsg::Read) ||
				message.type == _IMM(StorageDriverMsg::Write))) {
				FloppyReplyBusy(message.src, message.type);
			}
		}
	}

	bool FloppyRunRequest(uni::FloppyDisk& disk, stduint source,
		uint32 unit, uint32 block, bool write, byte* payload) {
		if (!floppy_device_available || floppy_request.state != FloppyRequestState::Idle) return false;
		for0(i, 2) {
			if (i != unit && floppy_drives[i]) floppy_drives[i]->Motor(false);
		}
		floppy_motor_stop_token[0] = 0;
		floppy_motor_stop_token[1] = 0;
		floppy_request.source = source;
		floppy_request.unit = unit;
		floppy_request.cancelled = false;
		floppy_request.wait_token = 0;
		bool completed = false;
		for (uint32 attempt = 0; attempt < FloppyRequestRetryLimit; ++attempt) {
			floppy_request.state = FloppyRequestState::Preparing;
			if (attempt || floppy_recovery_required) {
				if (!disk.Reset() || floppy_request.state == FloppyRequestState::Failed ||
					floppy_request.cancelled || !floppy_device_available) continue;
				floppy_recovery_required = false;
				floppy_request.state = FloppyRequestState::Preparing;
			}
			const bool result = write ? disk.Write(block, payload) : disk.Read(block, payload);
			if (result && floppy_request.state != FloppyRequestState::Failed &&
				!floppy_request.cancelled && floppy_device_available) {
				floppy_request.state = FloppyRequestState::Completed;
				floppy_recovery_required = false;
				completed = true;
				break;
			}
			floppy_request.state = FloppyRequestState::Failed;
			FloppyAbortTransfer();
			disk.Motor(false);
			floppy_recovery_required = true;
			if (floppy_request.cancelled || !floppy_device_available) break;
		}
		floppy_request.state = FloppyRequestState::Idle;
		floppy_request.wait_token = 0;
		return completed;
	}

	struct FloppyRuntimeGuard {
		~FloppyRuntimeGuard() {
			FloppyStopMotors();
			FloppyAbortTransfer();
			if (floppy_dma_handle) {
				(void)Powercall::DevDmaFree(floppy_dma_handle);
				floppy_dma_handle = 0;
			}
			if (floppy_dev_handle) {
				(void)Powercall::DevClose(floppy_dev_handle);
				floppy_dev_handle = 0;
			}
		}
	};
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
	floppy_drives[0] = &drive_a;
	floppy_drives[1] = &drive_b;
	floppy_drive_types[0] = info.type_a;
	floppy_drive_types[1] = info.type_b;
	FloppyRuntimeGuard runtime_guard;
	if (!FloppyInitDma(floppy_dev_handle, drive_a)) return -4;
	drive_b.Block_buffer = floppy_dma_buffer;
	drive_b.fn_dma_prepare = FloppyPrepareDma;
	drive_b.io_method = uni::IOMethod::DMA;
	drive_a.fn_int_wait = FloppyWaitInterrupt;
	drive_b.fn_int_wait = FloppyWaitInterrupt;
	drive_a.fn_motor_release = FloppyReleaseMotor;
	drive_b.fn_motor_release = FloppyReleaseMotor;
	drive_a.react_type = uni::FloppyDisk::ReactType::Rupt;
	drive_b.react_type = uni::FloppyDisk::ReactType::Rupt;
	if (Powercall::DevPublish(floppy_dev_handle, PwcallDevicePublishCommand::Started) != 0) {
		printf("flopdisk: DevPublish failed handle=%u\n\r", unsigned(floppy_dev_handle));
		return -5;
	}

	for (stduint i = 0; i < 2; ++i) {
		if (!floppy_drive_types[i]) continue;
		floppy_request.state = FloppyRequestState::Preparing;
		const bool recovered = floppy_drives[i]->Reset();
		floppy_request.state = FloppyRequestState::Preparing;
		floppy_media_present[i] = recovered && floppy_drives[i]->IsMediaPresent();
		if (!recovered) floppy_recovery_required = true;
		floppy_request.state = FloppyRequestState::Idle;
		StorageDriverInfo registration = {};
		registration.dev_handle = floppy_dev_handle;
		registration.unit = i;
		registration.block_size = floppy_drives[i]->Block_Size;
		registration.block_count = floppy_drives[i]->getUnits();
		registration.flags = StorageDriverFlag_Readable | StorageDriverFlag_Writable |
			(floppy_media_present[i] ? StorageDriverFlag_MediaPresent : 0);
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
			if (message.data.length < sizeof(DeviceEvent)) continue;
			DeviceEvent event = {};
			MemCopyN(&event, payload, sizeof(event));
			(void)FloppyHandleDeviceEvent(event);
			if (floppy_shutdown_requested) return -14;
			continue;
		}
		if (message.type == _IMM(KernelMsg::DeviceTimeout)) {
			if (message.data.length < sizeof(stduint)) continue;
			FloppyHandleMotorTimeout(FloppyMessageToken(payload));
			continue;
		}
		if (message.src < Task_Init && message.type == _IMM(StorageDriverMsg::Cancel)) {
			StorageDriverReply result = {};
			result.status = 0;
			if (!FloppySend(message.src, _IMM(StorageDriverMsg::Complete), &result, sizeof(result))) return -9;
			continue;
		}
		if (message.src >= Task_Init ||
			(message.type != _IMM(StorageDriverMsg::Read) &&
			message.type != _IMM(StorageDriverMsg::Write)) ||
			message.data.length < sizeof(StorageDriverRequest)) continue;
		StorageDriverRequest request = {};
		MemCopyN(&request, payload, sizeof(request));
		StorageDriverReply result = {};
		const bool valid = request.version == StorageDriverProtocolVersion &&
			request.unit < 2 && floppy_drive_types[request.unit] &&
			floppy_media_present[request.unit] && floppy_device_available &&
			request.block < floppy_drives[request.unit]->getUnits();
		if (message.type == _IMM(StorageDriverMsg::Read)) {
			if (valid && FloppyRunRequest(*floppy_drives[request.unit], message.src,
				request.unit, request.block, false, payload)) {
				result.status = 0;
				result.bytes = 512;
			}
			if (!FloppySend(message.src, _IMM(StorageDriverMsg::Complete), &result, sizeof(result))) return -9;
			if (!result.status && !FloppySend(message.src, _IMM(StorageDriverMsg::Data), payload, 512)) return -10;
			if (floppy_shutdown_requested) return -14;
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
			data_message.type != _IMM(StorageDriverMsg::Data) ||
			data_message.data.length != 512) return -12;
		result.status = FloppyRunRequest(*floppy_drives[request.unit], message.src,
			request.unit, request.block, true, payload) ? 0 : -1;
		result.bytes = result.status ? 0 : 512;
		if (!FloppySend(message.src, _IMM(StorageDriverMsg::Complete), &result, sizeof(result))) return -13;
		if (floppy_shutdown_requested) return -14;
	}
}
#endif
