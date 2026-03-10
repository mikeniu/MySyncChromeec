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

<<<<<<< PATCH SET (20382701f0d1b80db4dfe5c816c7c0450f8da27c     1.Re-implement the command parsing logic.)
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
||||||| BASE      (2e6c46277614b9605a2c5ae9eba08e026a406e90 pdc: Add "USB Device Capable" to pdc info)
=======
static const char log_level_char[] = { 'A', 'V', 'D', 'I', 'W', 'E', 'U' };

void ff_util_hexdump(uint8_t mak, const char *info, const uint8_t *buf,
		     uint32_t len)
{
	uint16_t i = 0;
	char tmp_buf[17] = { 0 };
	char *p = NULL;
	uint8_t *addr = (uint8_t *)buf;

	if (0 == mak) {
		return;
	}
	if (info) {
		printf("%s: length = %d [0x%X]\r\n", info, len, len);
	}
	if (len == 0) {
		return;
	}
	p = tmp_buf;
	printf("%p  ", addr);

	for (i = 0; i < len; i++) {
		printf("%02X ", buf[i]);
		if ((buf[i] >= 0x20) && (buf[i] < 0x7F)) {
			*p++ = buf[i];
		} else {
			*p++ = '.';
		}
		if ((i + 1) % 16 == 0) {
			*p++ = 0; // string end
			printf("        | %s", tmp_buf);
			p = tmp_buf;

			printf("\r\n");

			if ((i + 1) < len) {
				addr += 16;
				printf("%p  ", addr);
			}
		} else if ((i + 1) % 8 == 0) {
			printf("- ");
		}
	}
	if (len % 16 != 0) {
		for (i = len % 16; i < 16; i++) {
			printf("   ");
			if (((i + 1) % 8 == 0) && ((i + 1) % 16 != 0)) {
				printf("- ");
			}
		}
		*p++ = 0; // string end
		printf("        | %s", tmp_buf);
		printf("\r\n");
	}
}

static void ft_get_time_local(char *time_buff)
{
	char buff[64];
	struct timeval tv;
	struct tm timeinfo;

	gettimeofday(&tv, NULL);

	localtime_r(&tv.tv_sec, &timeinfo);

	strftime(buff, sizeof(buff), "%m-%d %H:%M:%S", &timeinfo);
	snprintf(time_buff, sizeof(buff), "%s.%03ld", buff, tv.tv_usec / 1000);

	buff[sizeof(buff) - 1] = 0;
}

int log_printf(log_level_t level, const char *tag, const char *fmt, ...)
{
#ifdef __DEBUG_MODE__

	char buf[LOG_BUF_SIZE];
	char time_buff[64];
	if (level >= __LOG_LEVEL__) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, LOG_BUF_SIZE - 1, fmt, ap);
		va_end(ap);

		ft_get_time_local(time_buff);

		/*1.Write to console.*/
		if (level == LOG_LEVEL_ERR) {
			printf("\033[31m[%s] %c [%s] %s \033[0m\n", time_buff,
			       log_level_char[level], tag, buf);
		} else if (level == LOG_LEVEL_WRN) {
			printf("\033[33m[%s] %c [%s] %s \033[0m\n", time_buff,
			       log_level_char[level], tag, buf);
		} else if (level == LOG_LEVEL_INF) {
			printf("\033[36m[%s] %c [%s] %s \033[0m\n", time_buff,
			       log_level_char[level], tag, buf);
		} else {
			printf("[%s] %c [%s] %s\n", time_buff,
			       log_level_char[level], tag, buf);
		}
>>>>>>> BASE      (fcd9064294552a3cbf330ce87121caabb6c418c8 util: focaltech: Add focaltech IAP tool)
	}

#endif
	return FT_SUCCESS;
}
