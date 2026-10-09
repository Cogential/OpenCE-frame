/*
ANDROID/LOG.H

The Steam Frame (Linux) build of the Android host: <android/log.h> as lines
on standard error, which run.sh sends to the launch log.
*/

#ifndef __HALO_FRAME_ANDROID_LOG_H
#define __HALO_FRAME_ANDROID_LOG_H

#include <stdarg.h>
#include <stdio.h>

enum
{
	ANDROID_LOG_INFO = 4,
	ANDROID_LOG_WARN = 5,
	ANDROID_LOG_ERROR = 6,
	ANDROID_LOG_FATAL = 7,
};

static inline int __android_log_vprint(int priority, const char *tag, const char *format, va_list arguments)
{
	static const char *const names[] = { "?", "?", "V", "D", "I", "W", "E", "F" };

	flockfile(stderr);
	fprintf(stderr, "%s/%s: ", names[priority & 7], tag);
	vfprintf(stderr, format, arguments);
	fputc('\n', stderr);
	funlockfile(stderr);
	return 0;
}

static inline int __android_log_print(int priority, const char *tag, const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	__android_log_vprint(priority, tag, format, arguments);
	va_end(arguments);
	return 0;
}

static inline int __android_log_write(int priority, const char *tag, const char *text)
{
	return __android_log_print(priority, tag, "%s", text);
}

#endif
