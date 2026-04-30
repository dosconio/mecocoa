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
	virtual void DrawFont(const Point& disp, const DisplayFont& font) const override;\
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
};
Dnode* VTTY_Append(Console_t* con);
inline static QueueLimited* VTTY_INNQ(Dnode* nod) {
	if (!nod || !nod->type) return nullptr;
	auto& block = *(vtty_type_t*)nod->type;
	return &block.innput_queue;
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
};
_PACKED(struct) FMT_ConsoleMsg_FMSG {
	stduint pform_id;
	stduint if_blocked;
	SheetMessage* message;
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

extern unsigned current_screen_TTY;// focus
extern SheetTrait* last_click_sheet;

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


#if (_MCCA & 0xFF00) == 0x8600
#define TTY_NUMBER 4

// defVconIface(GloScreenRGB888, uint8);
defVconIface(GloScreenARGB8888, uint32);
defVconIface(GloScreenABGR8888, uint32);

struct _RET_CreateVconsole {
	uni::VideoConsole2* pcon;
	::uni::Witch::Form* pform;
	stduint tty_no;
	// -> pid
};
struct Consman {
	// GUI
	static bool ento_gui;
	static bool enable_dubuffer;
	static uni::VideoControlInterface* real_pvci;
	//
	static bool Initialize();
	static void enable_2buffer();
	static _RET_CreateVconsole CreateVconsole(const Rectangle& rect, rostr title);
	//{} TODO RemoveVconsole
};


void hand_mouse(MouseMessage mmsg);
void hand_kboard(keyboard_event_t mmsg);


#endif

#endif
