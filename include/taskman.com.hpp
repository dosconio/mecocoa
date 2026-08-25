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
	#if (_MCCA & 0xFF00) == 0x8600
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
