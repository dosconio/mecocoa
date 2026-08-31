#ifndef TASKMAN_COM_HPP_
#define TASKMAN_COM_HPP_

#include <c/stdinc.h>
#include <cpp/string>

enum {
	Task_Kernel,
	Task_TaskMan,
	Task_Console,
	Task_ConsoleVideo,// [inner of Task_Console] manage mice and GUI
	Task_FileSys,
	Task_Devsman,
	Task_Memdisk_Serv,
	#if (_MCCA & 0xFF00) == 0x8600 || (_ACCM & 0xFF00) == 0x8600
	Task_Net_Serv,
	#endif
	#if _MCCA == 0x8632 || _ACCM == 0x8632
	Task_Hdd_Serv,
	Task_Flp_Serv,
	Task_Audio_Serv,
	#endif
	Task_Init,
	//
	TaskCount
};

// Graphic-thread IPC message types for GUI operations.
// Console forwards Form/VConsole requests to Graphic via these.
enum class GraphicMsg {
	FNEW,// new-form
	FDEL,// close-form
	FBID,// bind user pixel buffer
	FUPD,// update pixels from user buffer
	FMSG,// fetch form message
	FDRW,// draw shape
	FCHR,// draw string
	FTIM,// set timer
	FSIZ,// get screen size
	FCLEANPROC,// clean exiting process GUI resources
	VCON_CREATE,// create virtual console
	VCON_REMOVE,// remove virtual console
	DRV_ATTACH,// driver publishes display session
	DRV_DETACH,// driver releases display session
	DRV_SETMODE,// request driver mode switch
	DRV_FLUSH,// request driver dirty-rect flush
	SET_WALLPAPER,// set desktop wallpaper (usrp_buffer, width, height)
};

enum class NetworkMsg {
	DRV_ATTACH,// driver publishes link session
	DRV_DETACH,// driver releases link session
	DRV_SEND,// send one raw link frame
	DRV_RECV,// receive one raw link frame
	DRV_RX,// driver pushes one received raw link frame
};

enum NetworkDriverCaps : uint32 {
	NetworkDriverCap_Poll = 1 << 0,
	NetworkDriverCap_RxEvent = 1 << 1,
};

static constexpr uint32 NetworkDriverProtocolVersion = 1;
static constexpr uint32 NetworkDriverNameCapacity = 16;
static constexpr uint32 NetworkDriverFrameCapacity = 2048;

_PACKED(struct) FMT_NetworkMsg_DRV_ATTACH {
	uint32 version;
	uint32 caps;
	uint32 dev_handle;
	uint32 mtu;
	uint8 mac[6];
	uint8 link_state;
	uint8 reserved0;
	char name[NetworkDriverNameCapacity];
};

_PACKED(struct) FMT_NetworkMsg_DRV_FRAME {
	int32 status;
	uint32 length;
	uint32 capacity;
	uint32 reserved0;
	uint8 data[NetworkDriverFrameCapacity];
};

static constexpr const stduint ANYPROC = (_IMM0);
static constexpr const stduint INTRUPT = (~_IMM0);
static constexpr const stduint COMM_RECV = 0b10;
static constexpr const stduint COMM_SEND = 0b01;
static constexpr const stduint COMM_SEND_ASYNC = 0b100;
static constexpr const stduint LIMIT_THREAD_AMSG = 64;

struct CommMsg {
	uni::Slice data = {};
	stduint type = 0;
	stduint src = 0;// use if type is HARDRUPT
};

#endif
