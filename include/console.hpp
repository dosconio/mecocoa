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

extern Dchain ttys, vttys;
struct vtty_type_t {
	stduint blocked_pid;
	QueueLimited innput_queue;
	QueueLimited output_queue;
	uni::Vector<stduint> proc_group;
	stduint master_pid = 0;
	stduint id; // Fixed TTY ID from Bitmap

	// vtty_type_t() : innput_queue({ 0,0 }), output_queue({ 0,0 }) {}
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
extern LayerManager2 global_layman;
#endif

#if _MCCA == 0x8632
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
	uni::VideoConsole2* pcon;
	::uni::Witch::Form* pform;
	stduint tty_no;
};

struct Consman {
	// CLI
	static unsigned current_screen_TTY;// focus
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
void hand_kboard(keyboard_event_t mmsg);


class ProcessBlock;
class Spinlock;

#if _GUI_ENABLE
void Global_CleanProcessForms(ProcessBlock* pb);
extern RecursiveMutex gui_lock;
#endif

#endif

#endif
