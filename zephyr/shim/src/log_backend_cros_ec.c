/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "zephyr_console_shim.h"

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

BUILD_ASSERT(!IS_ENABLED(CONFIG_PIGWEED_LOG_TOKENIZED_LIB),
	     "PLATFORM_EC_LOG_BACKEND_CROS_EC is incompatible with "
	     "PIGWEED_LOG_TOKENIZED_LIB");

/*
 * Accumulate formatted characters into a stack-allocated line buffer before
 * flushing to zephyr_print() so each log message is written as a single chunk
 * to the Zephyr shell TX ring buffer, AP console buffer, and panic log without
 * holding any backend lock across zephyr_print().
 */
struct line_buf_ctx {
	char buf[CONFIG_SHELL_PRINTF_BUFF_SIZE];
	size_t len;
};

static void flush_line_buf(struct line_buf_ctx *ctx)
{
	if (ctx->len == 0) {
		return;
	}

	/*
	 * Add '\0' for "%s" callers inside zephyr_print() (e.g.
	 * printk_unlocked), without incrementing len so '\0' is not written to
	 * size-based outputs.
	 */
	ctx->buf[ctx->len] = '\0';
	zephyr_print(ctx->buf, ctx->len);
	ctx->len = 0;
}

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	struct line_buf_ctx *line_ctx = ctx;

	for (size_t i = 0; i < length; i++) {
		char c = (char)data[i];

		/*
		 * shell_write_str() inside zephyr_print() expands '\n' to
		 * "\r\n", so skip '\r' to avoid "\r\r\n" when cr_out_func is
		 * used by LOG_PRINTK.
		 */
		if (c == '\r') {
			continue;
		}

		line_ctx->buf[line_ctx->len++] = c;
		/* Reserve one byte for the trailing '\0' in flush_line_buf. */
		if (line_ctx->len == sizeof(line_ctx->buf) - 1) {
			flush_line_buf(line_ctx);
		}
	}

	return length;
}

static void process(const struct log_backend *const backend,
		    union log_msg_generic *msg)
{
	ARG_UNUSED(backend);

	struct line_buf_ctx line_ctx;
	/*
	 * log_output.c ignores .buf in CONFIG_LOG_MODE_IMMEDIATE (calling
	 * char_out 1 byte at a time), but requires .size >= 1 in
	 * CONFIG_LOG_MODE_DEFERRED. Using a 1-byte buffer here unifies both
	 * modes into char_out() -> line_ctx.buf.
	 */
	uint8_t raw_char;
	struct log_output_control_block control_block = {
		.ctx = &line_ctx,
	};
	const struct log_output log_output = {
		.func = char_out,
		.control_block = &control_block,
		.buf = &raw_char,
		.size = sizeof(raw_char),
	};
	uint32_t flags =
		(log_backend_std_get_flags() & ~LOG_OUTPUT_FLAG_COLORS) |
		LOG_OUTPUT_FLAG_CRLF_LFONLY;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_LOG_BACKEND_CROS_EC_REDUCED)) {
		flags = (flags & ~LOG_OUTPUT_FLAG_LEVEL) |
			LOG_OUTPUT_FLAG_SKIP_SOURCE;
	}

	line_ctx.len = 0;
	log_output_msg_process(&log_output, &msg->log, flags);
	flush_line_buf(&line_ctx);
}

static void panic(struct log_backend const *const backend)
{
	ARG_UNUSED(backend);

	/* Switch the shell to blocking mode in case it is used during panic. */
	zephyr_console_panic();
}

static void notify_dropped(const struct log_backend *const backend,
			   uint32_t cnt)
{
	char buf[sizeof("--- 9999 messages dropped ---\n")];
	int len;

	ARG_UNUSED(backend);

	len = snprintk(buf, sizeof(buf), "--- %u messages dropped ---\n",
		       MIN(cnt, 9999U));
	if (len > 0) {
		zephyr_print(buf, MIN((size_t)len, sizeof(buf) - 1));
	}
}

const struct log_backend_api log_backend_cros_ec_api = {
	.process = process,
	.panic = panic,
	.dropped = IS_ENABLED(CONFIG_LOG_MODE_DEFERRED) ? notify_dropped : NULL,
};
LOG_BACKEND_DEFINE(log_backend_cros_ec, log_backend_cros_ec_api, true);
