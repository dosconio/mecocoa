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

//: You need a occupier if no parameters after fmt

#if !defined(USE_LOG_ERROR)
#define log_error(fmt,...)
#else
#define log_error(fmt,...) printlog(_LOG_ERROR, "TID %d: " fmt, threadid(), __VA_ARGS__)
#endif

#if !defined(USE_LOG_WARN)
#define log_warn(fmt,...)
#else
#define log_warn(fmt,...) printlog(_LOG_WARN, "TID %d: " fmt, threadid(), __VA_ARGS__)
#endif

#if !defined(USE_LOG_INFO)
#define log_info(fmt,...)
#else
#define log_info(fmt,...) printlog(_LOG_INFO, "TID %d: " fmt, threadid(), __VA_ARGS__)
#endif

#if !defined(USE_LOG_DEBUG)
#define log_debug(fmt,...)
#else
#define log_debug(fmt,...) printlog(_LOG_DEBUG, "TID %d: " fmt, threadid(), __VA_ARGS__)
#endif

#if !defined(USE_LOG_TRACE)
#define log_trace(fmt,...)
#else
#define log_trace(fmt,...) printlog(_LOG_TRACE, "TID %d: " fmt, threadid(), __VA_ARGS__)
#endif

void log_panic(const char *fmt, ...);
#define log_panic(a,...) log_panic("TID %d: " a,threadid(),__VA_ARGS__)

#endif
