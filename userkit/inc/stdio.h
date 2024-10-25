#ifndef __STDIO_H__
#define __STDIO_H__

#define stdin 0
#define stdout 1
#define stderr 2


typedef unsigned long int uintmax_t;
typedef long int intmax_t;
extern int buffer_lock_enabled;

int getchar();
int putchar(int);
int puts(const char *s);
void printf(const char *fmt, ...);
int fflush(int);
void enable_thread_io_buffer();

#define EOF (-1)

#endif // __STDIO_H__
