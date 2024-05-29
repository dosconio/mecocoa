#ifndef _MY_STDDEF_H__ // __STDDEF_H__ can be used if UNISYM independs from stddef
#define _MY_STDDEF_H__

#include <c/stdinc.h>

/* Represents true-or-false values */
typedef int bool;

/* *
 * Pointers and addresses are 32 bits long.
 * We use pointer types to represent addresses,
 * uintptr_t to represent the numerical values of addresses.
 * */
#if __riscv_xlen == 64
typedef int64_t intptr_t;
typedef uint64_t uintptr_t;
#elif __riscv_xlen == 32
typedef int32_t intptr_t;
typedef uint32_t uintptr_t;
#endif

typedef stdsint ssize_t;

typedef int pid_t;

#define va_start(ap, last) (__builtin_va_start(ap, last))
#define va_arg(ap, type) (__builtin_va_arg(ap, type))
#define va_end(ap) (__builtin_va_end(ap))
#define va_copy(d, s) (__builtin_va_copy(d, s))
typedef __builtin_va_list va_list;

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR 0x002 // 可读可写
#define O_CREATE 0x200

#define DIR 0x040000
#define FILE 0x100000

#define AT_FDCWD -100

#define MAX_SYSCALL_NUM 500

typedef struct {
	uint64 sec; // 自 Unix 纪元起的秒数
	uint64 usec; // 微秒数
} TimeVal;

typedef struct {
	uint64 dev; // 文件所在磁盘驱动器号，不考虑
	uint64 ino; // inode 文件所在 inode 编号
	uint32 mode; // 文件类型
	uint32 nlink; // 硬链接数量，初始为1
	uint64 pad[7]; // 无需考虑，为了兼容性设计
} Stat;

typedef enum {
	UnInit,
	Ready,
	Running,
	Exited,
} TaskStatus;

typedef struct {
	TaskStatus status;
	unsigned int syscall_times[MAX_SYSCALL_NUM];
	int time;
} TaskInfo;

#define MAP_ANONYMOUS (0)
#define MAP_SHARED (1)

#endif // __STDDEF_H__
