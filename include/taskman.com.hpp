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
	#if (_MCCA & 0xFF00) == 0x8600 || (_ACCM & 0xFF00) == 0x8600
	Task_Net_Serv,
	#endif
	#if _MCCA == 0x8632 || _ACCM == 0x8632
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
	FSET,// set-prop
	FMIN,// minimize-form
	FMAX,// maximize/restore-form
	FRES,// restore-form
	FENUM,// enumerate-windows
	FCLEANPROC,// clean exiting process GUI resources
	VCON_CREATE,// create virtual console
	VCON_REMOVE,// remove virtual console
	DRV_ATTACH,// driver publishes display session
	DRV_DETACH,// driver releases display session
	DRV_SETMODE,// request driver mode switch
	DRV_FLUSH,// request driver dirty-rect flush
	SET_WALLPAPER,// set desktop wallpaper (usrp_buffer, width, height)
};

_PACKED(struct) WindowInfo {
	uint32 pid;
	uint32 form_id;
	uint8 state; // FormState
	uint8 is_top; // 1 if top active window, 0 otherwise
	uint8 reserved[2];
	int32 x;
	int32 y;
	uint32 width;
	uint32 height;
	char title[64];
};

enum class NetworkMsg {
	DRV_ATTACH,// driver publishes link session
	DRV_DETACH,// driver releases link session
	DRV_SEND,// send one raw link frame
	DRV_RECV,// receive one raw link frame
	DRV_RX,// driver pushes one received raw link frame
	TIMER_REFRESH,// network state changed; recompute the nearest deadline
	TIMER_EXPIRED,// one-shot network timer expired
};

enum class KernelMsg : stduint {
	Interrupt = 0x10000, // rupt_proc -> sysrecv(INTRUPT) or sysrecv(ANYPROC)
	DeviceEvent,// IRQ device_event_proc -> Driver
	TaskLifecycle,// Taskman -> parent service
	DeviceTimeout,// timer -> Ring1 driver
};



enum class TaskLifecycleEventKind : uint16 {
	None = 0,
	Exited,
};

static constexpr uint32 TaskLifecycleProtocolVersion = 1;

_PACKED(struct) TaskLifecycleEvent {
	uint32 version = TaskLifecycleProtocolVersion;
	uint16 kind = _IMM(TaskLifecycleEventKind::None);
	uint16 flags = 0;
	uint32 pid = 0;
	uint32 parent_pid = 0;
	int32 exit_status = 0;
	uint32 reserved = 0;
};

enum class DeviceEventKind : uint16 {
	None = 0,
	Interrupt,
	Removed,
	Fault,
	Shutdown,
	ConsoleWake,
	TimerExpired,
};

enum DeviceEventFlag : uint16 {
	DeviceEventFlag_None = 0,
	DeviceEventFlag_NeedsAck = 1 << 0,
	DeviceEventFlag_Coalesced = 1 << 1, // count represents multiple source events
	DeviceEventFlag_Overflow = 1 << 2, // consumer must rescan the complete device state
};

static constexpr uint32 DeviceEventProtocolVersion = 1;

_PACKED(struct) DeviceEvent {
	uint32 version = DeviceEventProtocolVersion;
	uint16 kind = _IMM(DeviceEventKind::None);
	uint16 flags = DeviceEventFlag_None;
	uint32 device_handle = 0;
	uint32 source = 0;
	uint32 count = 0;
	uint32 generation = 0;
	uint64 sequence = 0;
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

static constexpr const stduint SENTINEL_NONE = (_IMM0);
static constexpr const stduint SENTINEL_DEAD = (~_IMM0);
static constexpr const stduint ANYPROC = (SENTINEL_DEAD - 1);
static constexpr const stduint INTRUPT = (SENTINEL_DEAD - 2);
static constexpr const stduint COMM_RECV = 0b10;
static constexpr const stduint COMM_SEND = 0b01;
static constexpr const stduint COMM_SEND_ASYNC = 0b100;
static constexpr const stduint LIMIT_THREAD_AMSG = 64;

struct CommMsg {
	uni::Slice data = {};
	stduint type = 0;
	stduint src = 0;
};

#endif
