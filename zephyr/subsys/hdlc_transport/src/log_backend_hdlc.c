/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hdlc_transport.h"

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_output.h>

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);
	hdlc_send_frame(HDLC_ADDR_LOG, data, length);
	return length;
}

static uint8_t hdlc_log_buf[CONFIG_LOG_BACKEND_HDLC_BUFFER_SIZE];
LOG_OUTPUT_DEFINE(hdlc_log_output, char_out, hdlc_log_buf,
		  sizeof(hdlc_log_buf));

static void process(const struct log_backend *const backend,
		    union log_msg_generic *msg)
{
	ARG_UNUSED(backend);
	uint32_t flags = log_backend_std_get_flags();
	log_format_func_t log_output_func =
		log_format_func_t_get(CONFIG_LOG_BACKEND_HDLC_OUTPUT_DEFAULT);

	log_output_func(&hdlc_log_output, &msg->log, flags);
}

static void panic(struct log_backend const *const backend)
{
	ARG_UNUSED(backend);
	hdlc_set_panic_mode();
	log_backend_std_panic(&hdlc_log_output);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);
	log_backend_std_dropped(&hdlc_log_output, cnt);
}

static void log_backend_hdlc_init(struct log_backend const *const backend)
{
	ARG_UNUSED(backend);
	log_output_ctx_set(&hdlc_log_output, NULL);
}

const struct log_backend_api log_backend_hdlc_api = {
	.process = process,
	.panic = panic,
	.init = log_backend_hdlc_init,
	.dropped = IS_ENABLED(CONFIG_LOG_MODE_IMMEDIATE) ? NULL : dropped,
};

LOG_BACKEND_DEFINE(log_backend_hdlc, log_backend_hdlc_api,
		   IS_ENABLED(CONFIG_LOG_BACKEND_HDLC_AUTOSTART));
