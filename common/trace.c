/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <zephyr/sys/base64.h>
#include <zephyr/tracing/tracing.h>

static void _dump_trace(void)
{
	char out_buf[73];
	void *buf;
	unsigned buf_size, offset = 0, remain;
	size_t olen;

	xTraceGetEventBuffer(&buf, &buf_size);

	while (offset < buf_size) {
		remain = offset - buf_size;
		if (remain > 54)
			remain = 54;
		base64_encode(out_buf, sizeof(out_buf), &olen,
			      (const uint8_t *)buf + offset, remain);
		cprintf(CC_COMMAND, "%s\n", out_buf);
		offset += remain;
	}
}

void stop_trace(void)
{
	xTraceDisable();
}

static int command_trace(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (strcmp(argv[1], "start") == 0)
		xTraceEnable(TRC_START);
	else if (strcmp(argv[1], "stop") == 0)
		xTraceDisable();
	else if (strcmp(argv[1], "dump") == 0)
		_dump_trace();
	else
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}

DECLARE_SAFE_CONSOLE_COMMAND(trace, command_trace, NULL,
			     "Control trace with start|stop|dump");
