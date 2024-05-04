// ASCII C++ TAB4 CRLF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Logging for Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef _MCCA_LOG
#define _MCCA_LOG

#include <c/consio.h>
extern int threadid();
extern void shutdown();

#if defined(_LOG_LEVEL_ERROR)

#define USE_LOG_ERROR

#endif // _LOG_LEVEL_ERROR

#if defined(_LOG_LEVEL_WARN)

#define USE_LOG_ERROR
#define USE_LOG_WARN

#endif // _LOG_LEVEL_ERROR

#if defined(_LOG_LEVEL_INFO)

#define USE_LOG_ERROR
#define USE_LOG_WARN
#define USE_LOG_INFO

#endif // _LOG_LEVEL_INFO

#if defined(_LOG_LEVEL_DEBUG)

#define USE_LOG_ERROR
#define USE_LOG_WARN
#define USE_LOG_INFO
#define USE_LOG_DEBUG

#endif // _LOG_LEVEL_DEBUG

#if defined(_LOG_LEVEL_TRACE)

#define USE_LOG_ERROR
#define USE_LOG_WARN
#define USE_LOG_INFO
#define USE_LOG_DEBUG
#define USE_LOG_TRACE

#endif // _LOG_LEVEL_TRACE

void log_error(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_debug(const char *fmt, ...);
void log_trace(const char *fmt, ...);
void log_panic(const char *fmt, ...);

#endif
