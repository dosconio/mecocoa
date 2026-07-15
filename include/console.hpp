#ifndef CONSOLE_HPP_
#define CONSOLE_HPP_

// without boundary check
#define defVconIface(cname,unit) class cname : public VideoControlInterface {\
public:\
	unit& Locate(const Point& disp) const;\
	virtual ~cname() = default; cname() {}\
	virtual void SetCursor(const Point& disp) const override;\
	virtual Point GetCursor() const override;\
	virtual void DrawPoint(const Point& disp, Color color) const override;\
	virtual void DrawRectangle(const Rectangle& rect) const override;\
	virtual void DrawFont(const Point& disp, const DisplayFont& font, const String& str) const override;\
	virtual Color GetColor(Point p) const override;\
	virtual void DrawPoints(const Rectangle& rect, const Color* base) const override;\
};

#include <cpp/Device/_Video.hpp>
#include <c/driver/mouse.h>
#include <cpp/Witch/Form.hpp>
#include <cpp/Witch/TextChrome.hpp>
#include "taskman/lock.hpp"

struct FramebufferInfo {
	uni::Slice physical_range;
	uni::Size2 screen_size;
	uint32 pitch;
	uint32 bpp;
	uni::PixelFormat format;
};

extern FramebufferInfo sys_framebuffer;

struct VideoMode {
	uni::Size2 resolution;
	uni::PixelFormat format;
	uint32 refresh_rate;
	uint32 output_id;
};

class VideoDevice : public uni::VideoControlInterface {
public:
	virtual ~VideoDevice() = default;
	virtual const FramebufferInfo& GetFramebuffer() const = 0;
	virtual bool SetMode(const VideoMode& mode) { return false; }
	virtual void Flush(const Rectangle& rect) {}
};

extern Dchain ttys, vttys;
struct vtty_type_t {
	stduint blocked_pid = 0;
	QueueLimited innput_queue;
	QueueLimited output_queue;
	uni::Vector<stduint> proc_group;
	stduint master_pid = 0;
	stduint id = 0; // Fixed TTY ID from Bitmap
};
Dnode* VTTY_Append(Console_t* con);
inline static QueueLimited* VTTY_INNQ(Dnode* nod) {
	if (!nod || !nod->type) return nullptr;
	auto& block = *(vtty_type_t*)nod->type;
	return &block.innput_queue;
}
inline static QueueLimited* VTTY_OUTQ(Dnode* nod) {
	if (!nod || !nod->type) return nullptr;
	auto& block = *(vtty_type_t*)nod->type;
	return &block.output_queue;
}

enum class ConsoleMsg {
	TEST,
	READ,//(UNDO) R (dev, addr, len, pid)
	WRIT,//(UNDO) W (dev, addr, len, pid)
	INNC,// () noreturn
	FNEW,// new-form    (formid, u_rect)->formid
	FDEL,// close-form  (formid)
	FBID,//(UNDO)
	FUPD,//(UNDO)
	FMSG,// fetch-msg   (formid, if_blocked, &u_msg) -> 1 Ok 0 No-Msg
	FDRW,// draw        (formid, shape_type, usr_shape_info)
	FCHR,// draw-string (formid, u_point, u_str, color)
	FTIM,// set-timer   (formid, ms)
	// do not put above:
	FCLEANPROC,// internal: clean one exiting process GUI resources in console owner thread
};
struct BlockedFormMsg {
	stduint sig_src; // thread id
	stduint pform_id; // form index in pforms
	SheetMessage* usr_msg_ptr; // user space buffer pointer

	bool operator==(const BlockedFormMsg& other) const {
		return sig_src == other.sig_src && pform_id == other.pform_id && usr_msg_ptr == other.usr_msg_ptr;
	}
};
extern Vector<BlockedFormMsg> blocked_form_msgs;
extern void RefreshConsoleBlockedStateUnlocked();

_PACKED(struct) FMT_ConsoleMsg_RDWR {
	stduint usr_addr;
	stduint len;
	stduint pid;
};
_PACKED(struct) FMT_ConsoleMsg_FCLEANPROC {
	pureptr_t process_block;
};
_PACKED(struct) FMT_ConsoleMsg_FMSG {
	stduint pform_id;
	stduint if_blocked;
	SheetMessage* message;
};
_PACKED(struct) FMT_ConsoleMsg_FBID {
	stduint pform_id;
	void* usrp_buffer;
};
_PACKED(struct) FMT_ConsoleMsg_FUPD {
	stduint pform_id;
	Rectangle* usrp_rect;
};
_PACKED(struct) FMT_ConsoleMsg_FDRW {
	stduint pform_id;// in pforms
	enum class Shape {
		Point,
		Line,
		Rect,
	} shape_type;
	union ShapeInfo {
		struct ColorPoint { Point2 po; Color co; } *cpoint;
		struct ColorLine { Point disp; Size2 size; Color color; } *cline;
		Rectangle *crect;
	} usr_shape_info;
};


#if (_MCCA & 0xFF00) == 0x8600
class LayerManager2 : public uni::LayerManager {
public:
	using LayerManager::LayerManager;
	bool lazy_update = false;
	// Request an update, if lazy_update is true, it just adds to dirty area
	virtual void Update(SheetTrait* who, const Rectangle& rect) override;
	// Force the update immediately (Composition)
	void UpdateForce(SheetTrait* who, const Rectangle& rect);
};
extern SpinlockBlock<LayerManager2> global_layman;
#endif

#if (_MCCA & 0xFF00) == 0x8600
struct KeyboardBridge : public OstreamTrait // // scan code set 1
{
	virtual int out(const char* str, stduint len) override;
};
#endif

// Cursor
#if (_MCCA & 0xFF00) == 0x8600
class Cursor: public uni::SheetTrait
{
public:
	virtual void doshow(void* _) override;
	virtual void onrupt(uni::SheetEvent event, Point rel_p, ...) override {}
public:// single instance
	static Cursor* global_cursor;
	static SheetTrait* moving_sheet;
	static bool mouse_btnl_dn;
	static bool mouse_btnm_dn;
	static bool mouse_btnr_dn;
public:
	Cursor(uni::VideoControlInterface* writer) : uni::SheetTrait(), pixel_writer_{ writer } {}
	void setSheet(LayerManager& layman, const Point& vertex);
public:
	uni::VideoControlInterface* pixel_writer_ = nullptr;
};
#endif


struct _RET_CreateVconsole {
	uni::VideoConsole2* pcon = nullptr;
	::uni::Witch::Form* pform = nullptr;
	stduint tty_no = 0;
	Dnode* tty_node = nullptr; // stable pointer, avoids racy index lookup on vttys
};

struct Consman {
	// CLI
	static unsigned current_screen_TTY;// focus
	static void WakeBlockedWaiters();
	static void WakeBlockedWaitersDeferred();
	#if (_MCCA & 0xFF00) == 0x8600
	// GUI
	static bool ento_gui;
	static bool enable_dubuffer;
	static SheetTrait* last_click_sheet;
	static uni::VideoControlInterface* real_pvci;
	//
	static bool Initialize();
	static void enable_2buffer();
	static _RET_CreateVconsole CreateVconsole(const Rectangle& rect, rostr title);
	// Detaches one Form subtree from GUI roots, clears graf input references that still point into it, and returns the dirty area that should be refreshed afterward.
	static Rectangle DetachForm(::uni::Witch::Form* pfrm, SheetTrait* exact_sheet = nullptr);
	static void RemoveVconsole(Dnode* nod);
	static void SwitchForm(SheetTrait* form);
	#endif
};

#if (_MCCA & 0xFF00) == 0x8600
#define TTY_NUMBER 4

// defVconIface(GloScreenRGB888, uint8);
defVconIface(GloScreenARGB8888, uint32);
defVconIface(GloScreenABGR8888, uint32);


void hand_mouse(MouseMessage mmsg);
void hand_mouse_usb(MouseMessage mmsg);
void hand_kboard(keyboard_event_t mmsg);


class ProcessBlock;
class Spinlock;
extern ProcessBlock* Bcons_pcot[TTY_NUMBER];
ProcessBlock* Bcons_EnsureCot(unsigned tty_no);

#if _GUI_ENABLE
void Global_CleanProcessForms(ProcessBlock* pb);
void QueueGuiCleanupForProcess(ProcessBlock* pb);
#endif

#endif

#endif
