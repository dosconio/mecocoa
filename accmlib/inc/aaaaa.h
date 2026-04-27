#include "c/stdinc.h"
#include "c/datype/ruststyle.h"
#ifdef _INC_CPP
using namespace uni;
#endif
// #include "../../include/syscall.hpp"
#include "../../include/taskman.hpp"
#include "../../include/fileman.hpp"
#include "../../include/console.hpp"

#define sysrecv(pid,msg) syscomm(0,pid,msg)
#define syssend(pid,msg) syscomm(1,pid,msg)

#ifdef _INC_CPP
extern "C" {
#endif
	int main(int argc, char** argv);
	//
	void sysouts(const char* str);// 00
	int sysinnc();// 01
	stduint syssecond();// 03
	void sysrest(stduint unit, stduint num);// 04

	void syscomm(int send_recv, stduint obj, struct CommMsg* msg);// 05

	void sysdelay(unsigned dword);
	void sysquit(int code);

	stduint systest(unsigned t, unsigned e, unsigned s);

	// return negative if failed. Create and Open
	int sys_createfil(rostr fullpath);
	//
	int sysopen(rostr fullpath);
	// return nil for success
	int sysclose(int fd);
	//
	stduint sysread(int fd, void* buf, stduint size);
	//
	stduint syswrite(int fd, const void* buf, stduint size);
	// return nil for success
	int sys_removefil(rostr fullpath);

	// int exec(rostr path, rostr argstr);
	int execv(const char* path, char* argv[]);
	int execl(const char* path, const char* arg, ...);
	int spawnl(const char* path, const char* arg, ...);
	

	stduint get_core_id(stduint* ptr_hid);

	// ---- POSIX:unistd ---- //
	int fork();
	[[noreturn]] void exit(int status); // 02
	int _Comment(pid) wait(int* status);

#ifdef _INC_CPP
}
#endif

// CONSOLE
#ifdef _INC_CPP
extern "C" {
	#endif

	stdsint sys_create_form(stduint form_id, const Rectangle* rect);

	stdsint sys_draw_default_string(stduint form_id, Point vertex, rostr string, Color color);

	stdsint sys_draw_point(stduint form_id, Point po, Color co);

	stdsint sys_draw_line(stduint form_id, Point disp, Size2 size, Color co);

	stdsint sys_draw_rectangle(stduint form_id, const Rectangle* rect);

	#ifdef _INC_CPP
}
#endif
