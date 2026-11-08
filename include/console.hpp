#ifndef CONSOLE_HPP_
#define CONSOLE_HPP_

#include "console.com.hpp"
#include <cpp/ISO_IEC_STD/pair>
#include <cpp/Device/_Video.hpp>
#include <c/driver/mouse.h>
#include <cpp/Witch/Form.hpp>
#include <cpp/Witch/TextChrome.hpp>

namespace uni { class TouchControlBlock; }

extern OstreamTrait* con0_out;

extern FramebufferInfo sys_framebuffer;

extern Dchain ttys, vttys;
// tty input element: first is char (0 means none), second is keycode, first bit of char marks release
enum {
	_VTTY_MASK_INN_UP = 0x80,
	_VTTY_MASK_INN_CH = 0x7F,
};
struct vtty_type_t {
	stduint blocked_pid = 0;
	uni::Queue<uni::Pair<byte, byte>> innput_queue;
	QueueLimited output_queue;
	uni::Vector<stduint> proc_group;
	stduint master_pid = 0;
	stduint id = 0; // Fixed TTY ID from Bitmap
};
Dnode* VTTY_Append(Console_t* con);
inline static uni::Queue<uni::Pair<byte, byte>>* VTTY_INNQ(Dnode* nod) {
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
	FUPD,// update-area (formid, &rect)
	FMSG,// fetch-msg   (formid, if_blocked, &u_msg) -> 1 Ok 0 No-Msg
	FDRW,// draw        (formid, shape_type, usr_shape_info)
	FCHR,// draw-string (formid, u_point, u_str, color)
	FTIM,// set-timer   (formid, ms)
	FSET,// set-prop    (formid, prop, value)
	FMIN,// minimize    (formid)
	FRES,// restore     (formid)
	FENUM,// enumerate   (&WindowInfo, max_count, &out_count)
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
enum class DrawArea : stduint {
	Client = 0,
	Window = 1,
};
struct FMT_ConsoleMsg_FCHR {
	stduint pform_id;// in pforms
	const Point* usrp_vertex;
	const char* usrp_str;
	Color color;
	stduint pixel_size;// 0 selects the default 16-pixel size
	DrawArea area;
};
static_assert(sizeof(FMT_ConsoleMsg_FCHR) == 6 * sizeof(stduint), "FCHR must occupy six IPC words");
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
	DrawArea area;
};
static_assert(sizeof(FMT_ConsoleMsg_FDRW) <= 4 * sizeof(stduint), "FDRW must fit in four IPC words");
_PACKED(struct) FMT_ConsoleMsg_FSET {
	stduint pform_id;// in pforms
	stduint prop;
	void* value;
};

#if _GUI_ENABLE
#if _GUI_FREETYPE && _MCCA == 0x8632
bool DrawFreeTypeString(Color* pixel_buffer, stduint pitch_pixels, const Size2& buffer_size, const Point& vertex,
	const String& str, Color color, stduint pixel_size, Rectangle& dirty_rect);
#endif

class LayerManager2;
struct GraphicLayer {
	uni::VideoDevice* screen = nullptr;
	LayerManager2* manager = nullptr;
	uni::VideoControlInterface* surface = nullptr;
	uni::Rectangle window;
	uni::Size2 capacity;
	uni::PixelFormat format = uni::PixelFormat::ARGB8888;
	stduint address = 0, bytes = 0, pitch = 0;
	uint32 id = 0;// zero-based within the owning screen
	bool enabled = false, input_passthrough = false;
	bool (*Place)(GraphicLayer&, const Point&) = nullptr;
	void (*Disable)(GraphicLayer&) = nullptr;
};

class LayerManager2 : public uni::LayerManager {
public:
	using LayerManager::LayerManager;
	bool lazy_update = false;
	// Request an update, if lazy_update is true, it just adds to dirty area
	virtual void Update(SheetTrait* who, const Rectangle& rect) override;
	// Force the update immediately (Composition)
	void UpdateForce(SheetTrait* who, const Rectangle& rect);
	virtual void setCursorType(uni::CursorType type) override;
	SheetTrait* FindInputSheet(const Point& position);
	bool RaiseSheet(SheetTrait* sheet);
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
#if _GUI_ENABLE
class Cursor: public uni::SheetTrait
{
public:
	virtual void doshow(void* _) override;
	virtual void onrupt(uni::SheetEvent event, Point rel_p, ...) override {}
	void setType(uni::CursorType type);
	Point GetScreenPosition() const;
	void MoveTo(const Point& position);
	void MoveBy(stdsint dx, stdsint dy);
	bool MoveToLayer(GraphicLayer* layer);
	GraphicLayer* GetDisplayLayer() const { return display_layer_; }
public:// single instance
	static Cursor* global_cursor;
	static SheetTrait* moving_sheet;
	static SheetTrait* resizing_sheet;
	static uni::Witch::FormHitTest resize_hit_mode;
	static Point resize_start_cursor;
	static Rectangle resize_start_rect;
	static Rectangle ghost_rect;
	static bool mouse_btnl_dn;
	static bool mouse_btnm_dn;
	static bool mouse_btnr_dn;
	uni::CursorType cur_type = uni::CursorType::Normal;
public:
	Cursor(uni::VideoControlInterface* writer) : uni::SheetTrait(), pixel_writer_{ writer } {}
	void setSheet(LayerManager& layman, const Point& vertex);
public:
	uni::VideoControlInterface* pixel_writer_ = nullptr;
private:
	GraphicLayer* display_layer_ = nullptr;
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
	static void DispatchDeferredWake();

	// GUI
	static bool ento_gui;
	static bool enable_dubuffer;
	static SheetTrait* last_click_sheet;
	static VideoDevice* current_video_device;
	static uni::VideoControlInterface* real_pvci;
	//
	static bool Initialize();
	static bool AdoptVideoDevice(VideoDevice* dev);
	static void enable_2buffer();
	static _RET_CreateVconsole CreateVconsole(const Rectangle& rect, rostr title);

	// ---- Form

	// Detaches one Form subtree from GUI roots, clears graf input references that still point into it, and returns the dirty area that should be refreshed afterward.
	static Rectangle DetachForm(::uni::Witch::Form* pfrm, SheetTrait* exact_sheet = nullptr);
	static void RemoveVconsole(Dnode* nod);
	static void SwitchForm(SheetTrait* form);
	static void MinimizeForm(::uni::Witch::Form* pfrm);
	static void MaximizeForm(::uni::Witch::Form* pfrm);
	static void RestoreForm(::uni::Witch::Form* pfrm);
	static Rectangle GetWorkArea();

	// ---- Touch
	
	static bool RegisterTouchDevice(uni::TouchControlBlock* control);
	static void UnregisterTouchDevice(uni::TouchControlBlock* control);
	static bool IsTouchDeviceRegistered(uni::TouchControlBlock* control);
};

#if _GUI_ENABLE
#define TTY_NUMBER 4


// Internal Graphic-thread dispatch shared with graphic-touch.cpp.
void HandlePointerMessage(MouseMessage mmsg, stdsint movement_x, stdsint movement_y);
void ConsumeMouseForGraphic(MouseMessage message);
bool PumpTouchDevices();
#if defined(_MCU_STM32H7x)
void RgbLcd_PumpPresentation();
#endif

void hand_mouse(MouseMessage mmsg);
void hand_mouse_from_device(MouseMessage mmsg, const void* source);
void hand_mouse_usb(MouseMessage mmsg);
void hand_kboard(keyboard_event_t mmsg);


class ProcessBlock;
class Spinlock;
extern ProcessBlock* Bcons_pcot[TTY_NUMBER];
ProcessBlock* EnsureCotForVtty(Dnode* tty_node, ProcessBlock** cache_slot);
ProcessBlock* Bcons_EnsureCot(unsigned tty_no);

void Global_CleanProcessForms(ProcessBlock* pb);
void QueueGuiCleanupForProcess(ProcessBlock* pb);

#endif

#endif
