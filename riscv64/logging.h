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

#define deflog(x) Letpara(paras, fmt);printlogx(x, fmt, paras)

inline static void log_error(rostr fmt, ...) {
#if defined(USE_LOG_ERROR)
	deflog(_LOG_ERROR);
#endif
}

inline static void log_warn(rostr fmt, ...) {
#if defined(USE_LOG_WARN)
	deflog(_LOG_WARN);
#endif
}

inline static void log_info(rostr fmt, ...) {
#if defined(USE_LOG_INFO)
	deflog(_LOG_INFO);
#endif
}

inline static void log_debug(rostr fmt, ...) {
#if defined(USE_LOG_DEBUG)
	deflog(_LOG_DEBUG);
#endif
}

inline static void log_trace(rostr fmt, ...) {
#if defined(USE_LOG_TRACE)
	deflog(_LOG_TRACE);
#endif
}

inline static void log_panic(rostr fmt, ...) {
	deflog(_LOG_PANIC);
}

#endif
