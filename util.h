#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdlib.h>
#include <stdarg.h>

void log_msg(const char *file, int line, const char *fmt, ...);
void shuffle(int *a, size_t n);

#define debug(...) log_msg(__FILE__, __LINE__, __VA_ARGS__);
#define warn(...) log_msg(NULL, 0, __VA_ARGS__);
#define fatal(...) log_msg(NULL, 0, __VA_ARGS__); exit(EXIT_FAILURE);

#endif
