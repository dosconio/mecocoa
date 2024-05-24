// ASCII C++ TAB4 LF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Logging for Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#ifndef _RiscV64
#define _RiscV64
#endif
#include "log.h"
#include "console.h"
#include <stdarg.h>

void log_error(const char* fmt, ...)
{
	#if !defined(USE_LOG_ERROR)
		return;
	#endif
	va_list paras;
	va_start(paras, fmt);
	do {
		int tid = threadid();
		outsfmt("\x1b[%dm[%s %d]", CON_FORE_RED, "ERROR", tid);
		outsfmtlst(fmt, paras);
		outs("\x1b[0m\n");
	} while (0);
	va_end(paras);
}

void log_warn(const char* fmt, ...)
{
	#if !defined(USE_LOG_WARN)
		return;
	#endif
	va_list paras;
	va_start(paras, fmt);
	do {
		int tid = threadid();
		outsfmt("\x1b[%dm[%s %d]", CON_FORE_YELLOW, "WARN ", tid);
		outsfmtlst(fmt, paras);
		outs("\x1b[0m\n");
	} while (0);
	va_end(paras);
}

void log_info(const char* fmt, ...)
{
	#if !defined(USE_LOG_INFO)
		return;
	#endif
	va_list paras;
	va_start(paras, fmt);
	do {
		int tid = threadid();
		outsfmt("\x1b[%dm[%s %d]", CON_FORE_BLUE, "INFO ", tid);
		outsfmtlst(fmt, paras);
		outs("\x1b[0m\n");
	} while (0);
	va_end(paras);
}

void log_debug(const char* fmt, ...)
{
	#if !defined(USE_LOG_DEBUG)
		return;
	#endif
	va_list paras;
	va_start(paras, fmt);
	do {
		int tid = threadid();
		outsfmt("\x1b[%dm[%s %d]", CON_FORE_GREEN, "DEBUG", tid);
		outsfmtlst(fmt, paras);
		outs("\x1b[0m\n");
	} while (0);
	va_end(paras);
}

void log_trace(const char* fmt, ...)
{
	#if !defined(USE_LOG_TRACE)
		return;
	#endif
	va_list paras;
	va_start(paras, fmt);
	do {
		int tid = threadid();
		outsfmt("\x1b[%dm[%s %d]", CON_FORE_GRAY, "TRACE", tid);
		outsfmtlst(fmt, paras);
		outs("\x1b[0m\n");
	} while (0);
	va_end(paras);
}

void log_panic(const char* fmt, ...)
{
	va_list paras;
	va_start(paras, fmt);
	int tid = threadid();
	outsfmt("\x1b[%dm[%s %d]", CON_FORE_RED, "PANIC", tid);
	outsfmtlst(fmt, paras);
	outs("\x1b[0m\n");
	shutdown();
}

