#include "aaaaa.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>
#include "c/ustring.h"

struct __mcca_FILE {
	int fd;
	unsigned flags;
	size_t pos;
	size_t size;
	char* buffer;
};

enum : unsigned {
	MCCA_FILE_CAN_READ  = 1u << 0,
	MCCA_FILE_CAN_WRITE = 1u << 1,
	MCCA_FILE_MEMORY    = 1u << 2,
	MCCA_FILE_OWN_FD    = 1u << 3,
	MCCA_FILE_STDIO     = 1u << 4,
};

#if _ACCM == 0x8632

static __mcca_FILE g_stdin  = { 0, MCCA_FILE_CAN_READ  | MCCA_FILE_STDIO, 0, 0, nullptr };
static __mcca_FILE g_stdout = { 1, MCCA_FILE_CAN_WRITE | MCCA_FILE_STDIO, 0, 0, nullptr };
static __mcca_FILE g_stderr = { 2, MCCA_FILE_CAN_WRITE | MCCA_FILE_STDIO, 0, 0, nullptr };

extern "C" {
FILE* stdin = &g_stdin;
FILE* stdout = &g_stdout;
FILE* stderr = &g_stderr;
}

static bool mcca_mode_has_char(const char* mode, char target)
{
	if (!mode) return false;
	for (; *mode; ++mode) {
		if (*mode == target) return true;
	}
	return false;
}

static int mcca_calc_seek_pos(FILE* stream, long offset, int whence, size_t* out_pos)
{
	if (!stream || !out_pos) return -1;

	long base = 0;
	switch (whence) {
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = (long)stream->pos;
		break;
	case SEEK_END:
		base = (long)stream->size;
		break;
	default:
		return -1;
	}

	long next = base + offset;
	if (next < 0) return -1;
	if ((size_t)next > stream->size) return -1;

	*out_pos = (size_t)next;
	return 0;
}

static int mcca_vfprintf_impl(FILE* stream, const char* format, para_list args)
{
	if (!stream || !format) return -1;

	para_list args_len;
	va_copy(args_len, args);
	int need = outsfmtlstlen(format, args_len);
	va_end(args_len);
	if (need < 0) return -1;

	char* buf = (char*)malloc((size_t)need + 1);
	if (!buf) return -1;

	para_list args_fmt;
	va_copy(args_fmt, args);
	outsfmtlstbufn(buf, (stduint)need + 1, format, args_fmt);
	va_end(args_fmt);

	size_t wrote = fwrite(buf, 1, (size_t)need, stream);
	free(buf);
	return (wrote == (size_t)need) ? need : -1;
}

extern "C" FILE* fopen(const char* filename, const char* mode)
{
	if (!filename || !mode || !mode[0]) return nullptr;

	const bool plus_mode = mcca_mode_has_char(mode, '+');
	const char primary = mode[0];

	int oflag = 0;
	unsigned stream_flags = MCCA_FILE_OWN_FD;
	switch (primary) {
	case 'r':
		oflag = plus_mode ? O_RDWR : O_RDONLY;
		stream_flags |= MCCA_FILE_CAN_READ;
		if (plus_mode) stream_flags |= MCCA_FILE_CAN_WRITE;
		break;
	case 'w':
		oflag = (plus_mode ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
		stream_flags |= MCCA_FILE_CAN_WRITE;
		if (plus_mode) stream_flags |= MCCA_FILE_CAN_READ;
		break;
	case 'a':
		oflag = (plus_mode ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
		stream_flags |= MCCA_FILE_CAN_WRITE;
		if (plus_mode) stream_flags |= MCCA_FILE_CAN_READ;
		break;
	default:
		return nullptr;
	}

	int fd = open(filename, oflag, 0);
	if (fd < 0) return nullptr;

	FILE* stream = (FILE*)malloc(sizeof(*stream));
	if (!stream) {
		close(fd);
		return nullptr;
	}

	stream->fd = fd;
	stream->flags = stream_flags;
	stream->pos = 0;
	stream->size = 0;
	stream->buffer = nullptr;

	// Without lseek support, read-capable regular files are loaded into memory once.
	if (stream->flags & MCCA_FILE_CAN_READ) {
		struct stat st;
		if (fstat(fd, &st) == 0 && st.st_size >= 0) {
			stream->size = (size_t)st.st_size;
			if (stream->size > 0) {
				stream->buffer = (char*)malloc(stream->size);
				if (!stream->buffer) {
					close(fd);
					free(stream);
					return nullptr;
				}

				size_t done = 0;
				while (done < stream->size) {
					stdsint got = read(fd, stream->buffer + done, stream->size - done);
					if (got <= 0) {
						free(stream->buffer);
						close(fd);
						free(stream);
						return nullptr;
					}
					done += (size_t)got;
				}
			}
			stream->flags |= MCCA_FILE_MEMORY;
		}
	}

	if (primary == 'a') {
		stream->pos = stream->size;
	}

	return stream;
}

extern "C" int fclose(FILE* stream)
{
	if (!stream) return -1;
	if (stream == stdin || stream == stdout || stream == stderr) return 0;

	if ((stream->flags & MCCA_FILE_OWN_FD) && stream->fd >= 0) {
		close(stream->fd);
	}
	if (stream->buffer) {
		free(stream->buffer);
	}
	free(stream);
	return 0;
}

extern "C" size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
	if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
	if (!(stream->flags & MCCA_FILE_CAN_READ)) return 0;

	size_t total = size * nmemb;
	if (total / size != nmemb) return 0;

	if (stream->flags & MCCA_FILE_MEMORY) {
		size_t avail = (stream->pos < stream->size) ? (stream->size - stream->pos) : 0;
		size_t take = (total < avail) ? total : avail;
		for (size_t i = 0; i < take; ++i) {
			((char*)ptr)[i] = stream->buffer[stream->pos + i];
		}
		stream->pos += take;
		return take / size;
	}

	stdsint got = read(stream->fd, ptr, total);
	if (got <= 0) return 0;
	stream->pos += (size_t)got;
	return (size_t)got / size;
}

extern "C" size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream)
{
	if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
	if (!(stream->flags & MCCA_FILE_CAN_WRITE)) return 0;

	size_t total = size * nmemb;
	if (total / size != nmemb) return 0;

	stdsint put = write(stream->fd, ptr, total);
	if (put <= 0) return 0;
	stream->pos += (size_t)put;
	if (stream->pos > stream->size) {
		stream->size = stream->pos;
	}
	return (size_t)put / size;
}

extern "C" int fseek(FILE* stream, long offset, int whence)
{
	if (!stream) return -1;

	size_t next_pos = 0;
	if (mcca_calc_seek_pos(stream, offset, whence, &next_pos) < 0) {
		return -1;
	}

	stream->pos = next_pos;
	return 0;
}

extern "C" long ftell(FILE* stream)
{
	if (!stream) return -1;
	return (long)stream->pos;
}

extern "C" int fgetpos(FILE* stream, fpos_t* pos)
{
	if (!stream || !pos) return -1;
	*pos = (fpos_t)stream->pos;
	return 0;
}

extern "C" int fsetpos(FILE* stream, const fpos_t* pos)
{
	if (!stream || !pos) return -1;
	return fseek(stream, (long)*pos, SEEK_SET);
}

extern "C" void rewind(FILE* stream)
{
	if (!stream) return;
	stream->pos = 0;
}

extern "C" int fflush(FILE* stream)
{
	(void)stream;
	return 0;
}

extern "C" int vfprintf(FILE* stream, const char* format, para_list arg)
{
	return mcca_vfprintf_impl(stream, format, arg);
}

extern "C" int fprintf(FILE* stream, const char* format, ...)
{
	para_list args;
	para_ento(args, format);
	int ret = mcca_vfprintf_impl(stream, format, args);
	para_endo(args);
	return ret;
}

extern "C" int fgetc(FILE* stream)
{
	unsigned char ch = 0;
	return (fread(&ch, 1, 1, stream) == 1) ? (int)ch : EOF;
}

extern "C" char* fgets(char* str, int n, FILE* stream)
{
	if (!str || n <= 0 || !stream) return nullptr;
	if (n == 1) {
		str[0] = '\0';
		return str;
	}
	int i = 0;
	while (i < n - 1) {
		int c = fgetc(stream);
		if (c == EOF) break;
		str[i++] = (char)c;
		if (c == '\n') break;
	}
	if (i == 0) return nullptr;
	str[i] = '\0';
	return str;
}

extern "C" int fputc(int c, FILE* stream)
{
	unsigned char ch = (unsigned char)c;
	return (fwrite(&ch, 1, 1, stream) == 1) ? (int)ch : EOF;
}

extern "C" int fputs(const char* str, FILE* stream)
{
	if (!str || !stream) return EOF;
	size_t len = 0;
	while (str[len]) {
		++len;
	}
	if (len == 0) return 0;
	if (fwrite(str, 1, len, stream) == len) {
		return 1;
	}
	return EOF;
}

#endif
