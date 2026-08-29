// ASCII CPP TAB4 CRLF
// Docutitle: (Device) Bochs VBE Graphics Driver
// Attribute: Arn-Covenant x86 Ring1 Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include <c/stdinc.h>
#include <c/consio.h>
#include "../../include/taskman.com.hpp"
#include "../../include/console.com.hpp"
#include "../../include/syscall-pow.hpp"

#if defined(_ACCM) && ((_ACCM & 0xFF00) == 0x8600)

static constexpr uint16 BochsVendorId = 0x1234;
static constexpr uint16 BochsDeviceId = 0x1111;
static constexpr uint16 BochsIoBase = 0x01CE;

enum BochsRegister : uint16 {
	BochsRegId = 0,
	BochsRegXres = 1,
	BochsRegYres = 2,
	BochsRegBpp = 3,
	BochsRegEnable = 4,
	BochsRegBank = 5,
	BochsRegVirtWidth = 6,
	BochsRegVirtHeight = 7,
	BochsRegXOffset = 8,
	BochsRegYOffset = 9,
	BochsRegVideoMemory64K = 10,
};

enum BochsEnable : uint16 {
	BochsDisabled = 0x00,
	BochsEnabled = 0x01,
	BochsLfbEnabled = 0x40,
};

static bool BochsIoWrite16(stduint dev_handle, uint16 port, uint16 value) {
	PwcallDeviceIoRequest request = {};
	request.resource_type = _IMM(PwcallDeviceResourceType::IoPortRange);
	request.resource_index = 0;
	request.width = 2;
	request.offset = port - BochsIoBase;
	request.value = value;
	return Powercall::DevIoWrite(dev_handle, &request) == 0;
}

static bool BochsIoRead16(stduint dev_handle, uint16 port, uint16* value) {
	if (!value) return false;
	PwcallDeviceIoRequest request = {};
	request.resource_type = _IMM(PwcallDeviceResourceType::IoPortRange);
	request.resource_index = 0;
	request.width = 2;
	request.offset = port - BochsIoBase;
	if (Powercall::DevIoRead(dev_handle, &request) != 0) return false;
	*value = uint16(request.value);
	return true;
}

static bool BochsWriteRegister(stduint dev_handle, BochsRegister index, uint16 value) {
	return BochsIoWrite16(dev_handle, BochsIoBase, uint16(index)) &&
		BochsIoWrite16(dev_handle, BochsIoBase + 1, value);
}

static bool BochsReadRegister(stduint dev_handle, BochsRegister index, uint16* value) {
	return BochsIoWrite16(dev_handle, BochsIoBase, uint16(index)) &&
		BochsIoRead16(dev_handle, BochsIoBase + 1, value);
}

static bool BochsSetResolution(stduint dev_handle, uint16 width, uint16 height, uint16 bpp) {
	if (!BochsWriteRegister(dev_handle, BochsRegEnable, BochsDisabled)) return false;
	if (!BochsWriteRegister(dev_handle, BochsRegXres, width)) return false;
	if (!BochsWriteRegister(dev_handle, BochsRegYres, height)) return false;
	if (!BochsWriteRegister(dev_handle, BochsRegBpp, bpp)) return false;
	if (!BochsWriteRegister(dev_handle, BochsRegVirtWidth, width)) return false;
	if (!BochsWriteRegister(dev_handle, BochsRegVirtHeight, height)) return false;
	if (!BochsWriteRegister(dev_handle, BochsRegXOffset, 0)) return false;
	if (!BochsWriteRegister(dev_handle, BochsRegYOffset, 0)) return false;
	if (!BochsWriteRegister(dev_handle, BochsRegEnable, BochsEnabled | BochsLfbEnabled)) return false;

	uint16 actual_width = 0;
	uint16 actual_height = 0;
	if (!BochsReadRegister(dev_handle, BochsRegXres, &actual_width)) return false;
	if (!BochsReadRegister(dev_handle, BochsRegYres, &actual_height)) return false;
	return actual_width == width && actual_height == height;
}

static bool FindDeviceResource(stduint dev_handle, PwcallDeviceResourceType type, uint32 index, PwcallDeviceResourceInfo* out) {
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

static stdsint PublishFramebuffer(stduint dev_handle, const PwcallDeviceResourceInfo& fb, uint32 width, uint32 height, uint32 pitch) {
	const uint64 fb_length = uint64(pitch) * height;
	if (!fb.start || !fb.length || !fb_length || fb_length > fb.length || pitch < width * 4) {
		outsfmt("[bochs-video] publish failed %ux%u pitch=%u need=%u fb_len=%u\n\r",
			width, height, pitch, (uint32)fb_length, (uint32)fb.length);
		return -1;
	}

	FMT_GraphicMsg_DRV_ATTACH attach = {};
	attach.version = GraphicDriverProtocolVersion;
	attach.caps = GraphicDriverCap_SetMode;
	attach.dev_handle = uint32(dev_handle);
	attach.resource_type = _IMM(PwcallDeviceResourceType::PciBarMmio);
	attach.resource_index = fb.index;
	attach.format = GraphicDriverPixelFormatARGB8888;
	attach.fb_start = fb.start;
	attach.fb_offset = 0;
	attach.fb_length = fb.length;
	attach.width = width;
	attach.height = height;
	attach.pitch = pitch;

	stduint args[4] = { _IMM(&attach), 0, 0, 0 };
	CommMsg send_msg = {};
	send_msg.data.address = _IMM(args);
	send_msg.data.length = sizeof(args);
	send_msg.type = _IMM(GraphicMsg::DRV_ATTACH);
	if (Powercall::SysComm(COMM_SEND, Task_ConsoleVideo, &send_msg)) return -1;

	stdsint result = -1;
	CommMsg recv_msg = {};
	recv_msg.data.address = _IMM(&result);
	recv_msg.data.length = sizeof(result);
	if (Powercall::SysComm(COMM_RECV, Task_ConsoleVideo, &recv_msg)) return -1;
	return result;
}

static bool PublishFramebufferAperture(stduint dev_handle, const PwcallDeviceResourceInfo& fb, PwcallDeviceResourceInfo* updated_fb) {
	uint16 video_memory_64k = 0;
	if (!BochsReadRegister(dev_handle, BochsRegVideoMemory64K, &video_memory_64k) || !video_memory_64k) return false;
	PwcallDeviceFramebufferAperture aperture = {};
	aperture.resource_type = _IMM(PwcallDeviceResourceType::PciBarMmio);
	aperture.resource_index = fb.index;
	aperture.start = fb.start;
	aperture.length = uint64(video_memory_64k) * 64 * 1024;
	if (Powercall::DevPublish(dev_handle, PwcallDevicePublishCommand::FramebufferAperture, &aperture) != 0) return false;
	if (updated_fb) {
		*updated_fb = fb;
		updated_fb->length = aperture.length;
	}
	return true;
}

static stdsint HandleSetMode(stduint dev_handle, const PwcallDeviceResourceInfo& fb, const stduint* args) {
	const uint32 width = uint32(args[0]);
	const uint32 height = uint32(args[1]);
	if (!width || !height || width > 0xFFFF || height > 0xFFFF) return -1;
	if (!BochsSetResolution(dev_handle, uint16(width), uint16(height), 32)) return -1;
	uint16 actual_width = 0;
	uint16 actual_height = 0;
	uint16 virt_width = 0;
	BochsReadRegister(dev_handle, BochsRegXres, &actual_width);
	BochsReadRegister(dev_handle, BochsRegYres, &actual_height);
	if (!BochsReadRegister(dev_handle, BochsRegVirtWidth, &virt_width) || !virt_width) virt_width = uint16(width);
	PwcallDeviceResourceInfo updated_fb = fb;
	PublishFramebufferAperture(dev_handle, fb, &updated_fb);
	return PublishFramebuffer(dev_handle, updated_fb, actual_width, actual_height, uint32(virt_width) * 4);
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	if (Powercall::Hello() != 0) return -1;

	const stduint cls = (stduint(BochsVendorId) << 16) | BochsDeviceId;
	const stdsint opened = Powercall::DevOpen(0, cls, 0);
	if (opened <= 0) return -1;
	const stduint dev_handle = stduint(opened);

	PwcallDeviceResourceInfo fb = {};
	if (!FindDeviceResource(dev_handle, PwcallDeviceResourceType::PciBarMmio, 0, &fb)) {
		Powercall::DevClose(dev_handle);
		return -1;
	}

	uint16 xres = 0;
	uint16 yres = 0;
	uint16 bpp = 0;
	BochsReadRegister(dev_handle, BochsRegXres, &xres);
	BochsReadRegister(dev_handle, BochsRegYres, &yres);
	BochsReadRegister(dev_handle, BochsRegBpp, &bpp);
	uint16 virt_width = 0;
	BochsReadRegister(dev_handle, BochsRegVirtWidth, &virt_width);

	if (!xres || !yres || bpp != 32) {
		xres = 1024;
		yres = 768;
		if (!BochsSetResolution(dev_handle, xres, yres, 32)) {
			Powercall::DevClose(dev_handle);
			return -1;
		}
		virt_width = xres;
	}
	if (!virt_width) virt_width = xres;

	PublishFramebufferAperture(dev_handle, fb, &fb);
	if (PublishFramebuffer(dev_handle, fb, xres, yres, uint32(virt_width) * 4) != 0) {
		Powercall::DevClose(dev_handle);
		return -1;
	}
	if (Powercall::DevPublish(dev_handle, PwcallDevicePublishCommand::Started) != 0) {
		Powercall::DevClose(dev_handle);
		return -1;
	}

	for (;;) {
		stduint args[4] = {};
		CommMsg recv_msg = {};
		recv_msg.data.address = _IMM(args);
		recv_msg.data.length = sizeof(args);
		if (Powercall::SysComm(COMM_RECV, ANYPROC, &recv_msg)) {
			syscall(syscall_t::REST, 1, 1000);
			continue;
		}
		if (recv_msg.src != Task_ConsoleVideo) continue;

		switch (GraphicMsg(recv_msg.type)) {
		case GraphicMsg::DRV_SETMODE:
			HandleSetMode(dev_handle, fb, args);
			break;
		default:
			break;
		}
	}
}

#else

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return -1;
}

#endif
