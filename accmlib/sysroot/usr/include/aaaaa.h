#include "c/stdinc.h"
#include "c/datype/ruststyle.h"
#ifdef _INC_CPP
using namespace uni;
#endif
// #include "../../include/syscall.hpp"
#include "../../../../include/taskman.hpp"
#include "../../../../include/fileman.hpp"
#include "../../../../include/console.hpp"

#define sysrecv(pid,msg) syscomm(0,pid,msg)
#define syssend(pid,msg) syscomm(1,pid,msg)

#ifdef _INC_CPP
extern "C" {
#endif
	// int main(int argc, char** argv);//{} envp, vector...
	//
	void sysouts(const char* str);// 00
	int sysinnc();// 01
	stduint syssecond();// 03
	void sysrest(stduint unit, stduint num);// 04
	void sysshutdown();
	void sysreboot();

	void syscomm(int send_recv, stduint obj, struct CommMsg* msg);// 05

	void sysdelay(unsigned dword);

	// return negative if failed. Create and Open
	int sys_createfil(rostr fullpath);
	//
	int sysopen(rostr fullpath);
	//
	
	// return nil for success
	int sys_removefil(rostr fullpath);


	int spawnve(const char* path, char* const argv[], char* const envp[]);
	int execv(const char* path, char* const argv[]);
	int execl(const char* path, const char* arg, ...);
	int spawnl(const char* path, const char* arg, ...);
	

	stduint get_core_id(stduint* ptr_hid);

	void* malloc(size_t size);
	void free(void* ptr);

#ifdef _INC_CPP
}
#endif

// CONSOLE
#ifdef _INC_CPP
extern "C" {
	#endif

	stdsint sys_create_form(stduint form_id, const Rectangle* rect, stduint flags = 0);

	stdsint sys_close_form(stduint form_id);

	stdsint sys_fetch_msg(stduint formid, stduint if_blocked, SheetMessage* u_msg);

	stdsint sys_draw_default_string(stduint form_id, Point vertex, rostr string, Color color);

	stdsint sys_draw_point(stduint form_id, Point po, Color co);

	stdsint sys_draw_line(stduint form_id, Point disp, Size2 size, Color co);

	stdsint sys_draw_rectangle(stduint form_id, const Rectangle* rect);
	stdsint sys_set_timer(stduint form_id, stduint ms);
	stdsint sys_set_form_buffer(stduint form_id, void* buffer);
	stdsint sys_update_form(stduint form_id, const Rectangle* rect);
	stdsint sys_get_screen_size(Size2* size);



	#ifdef _INC_CPP
}
#endif

#ifdef _INC_CPP
class GraphicForm {
private:
	stdsint form_id_;
	uni::Rectangle rect_;
	uni::Color* fb_buffer_;
	uni::VideoControlInterfaceMARGB8888* pvci_;
	uni::LayerManager* playman_;
	uni::SheetTrait* focus_sheet_;
public:
	GraphicForm(const uni::Rectangle& rect, const char* title = "Form");
	~GraphicForm();
	stdsint getFormId() const;
	uni::Color* getBuffer() const;
	uni::LayerManager& getLayerManager();
	stduint getClientWidth() const;
	stduint getClientHeight() const;
	void HandleEvent(const uni::SheetMessage& smsg);
	void DrawString(const uni::Point& vertex, const char* str, uni::Color col);
};
#endif

