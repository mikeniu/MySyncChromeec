/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ELAN Platform Abstraction Layer */

#include "clock.h"
#include "elan_misc.h"
#include "fpsensor/fpsensor_console.h"
#include "timer.h"
#include "uart.h"

#include <stdlib.h>

__staticlib_hook void *elan_malloc(uint32_t size)
{
	void *data = malloc(size);

	if (data == NULL) {
		CPRINTS("Error - %s of size %u failed.", __func__, size);
		return NULL;
	}
	return data;
}

__staticlib_hook void elan_free(void *data)
{
	free(data);
}

__staticlib_hook void elan_log_var(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	uart_vprintf(format, args);
	va_end(args);
}

__staticlib_hook uint32_t elan_get_tick(void)
{
	clock_t time;

	time = clock();

	return (uint32_t)time;
}
