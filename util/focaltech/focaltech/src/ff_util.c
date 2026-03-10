/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _POSIX_C_SOURCE 200809L

#include "ff_common.h"
#include "ff_log.h"
#include "ff_util.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <sys/time.h>
#include <unistd.h>

#ifdef __DEBUG_MODE__
static const char log_level_char[] = { 'A', 'V', 'D', 'I', 'W', 'E', 'U' };
static const char *log_colors[] = {
	"", /* ALL - no color */
	"", /* VBS */
	"", /* DBG */
	"\033[36m", /* INF - Cyan */
	"\033[33m", /* WRN - Yellow */
	"\033[31m" /* ERR - Red */
};
#endif

#ifdef __DEBUG_MODE__
static void ft_get_time_local(char *time_buff)
{
	int max_len = 64;
	char buff[max_len];
	struct timeval tv;
	struct tm timeinfo;

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &timeinfo);

	size_t len = strftime(time_buff, max_len, "%m-%d %H:%M:%S", &timeinfo);
	if (len > 0 && len < max_len) {
		snprintf(time_buff + len, max_len - len, ".%03ld",
			 tv.tv_usec / 1000);
	}

	buff[sizeof(buff) - 1] = 0;
}
#endif

int log_printf(log_level_t level, const char *tag, const char *fmt, ...)
{
#ifdef __DEBUG_MODE__

	char buf[LOG_BUF_SIZE];
	char time_buff[64];

	if (level >= __LOG_LEVEL__ && level <= LOG_LEVEL_ERR) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);

		const char *color = log_colors[level];
		const char *reset = (color[0] != '\0') ? "\033[0m" : "";
		ft_get_time_local(time_buff);

		/*1.Write to console.*/
		printf("%s[%s] %c [%s] %s %s\n", color, time_buff,
		       log_level_char[level], tag, buf, reset);
	}

#endif
	return FT_SUCCESS;
}
