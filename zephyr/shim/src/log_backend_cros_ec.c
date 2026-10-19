/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "zephyr_console_shim.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/spinlock.h>

/*
 * Accumulate formatted characters into static line buffers before flushing to
 * zephyr_print() so each log message is written as a single chunk to the Zephyr
 * shell TX ring buffer, AP console buffer, and panic log without allocating a
 * large buffer on the caller's stack.
 *
 * Yielding threads use thread_line_ctx guarded by thread_log_mutex (since
 * zephyr_print() may sleep on the shell mutex). In immediate mode, ISRs and
 * other non-yielding contexts (exceptions, IRQ-locked sections, pre-kernel,
 * idle thread) use a separate isr_line_ctx guarded by isr_log_lock so an ISR
 * interrupting a logging thread does not corrupt its buffer.
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

static struct line_buf_ctx thread_line_ctx;
static uint8_t thread_raw_char;
LOG_OUTPUT_DEFINE(thread_log_output, char_out, &thread_raw_char,
		  sizeof(thread_raw_char));
static K_MUTEX_DEFINE(thread_log_mutex);

/*
 * A separate non-yielding buffer is only needed in CONFIG_LOG_MODE_IMMEDIATE,
 * where every LOG_*() call invokes process() synchronously in the caller's
 * context (so an ISR can preempt a thread mid-print and resume it afterwards).
 *
 * In CONFIG_LOG_MODE_DEFERRED, LOG_*() calls in ISRs and threads only enqueue
 * messages into the MPSC buffer and process() is called exclusively by the
 * dedicated logging thread (or during log_panic(), when preempted threads will
 * not resume). CONFIG_LOG_MODE_MINIMAL does not use log backends at all
 * (PLATFORM_EC_LOG_BACKEND_CROS_EC depends on !LOG_MODE_MINIMAL).
 */
#ifdef CONFIG_LOG_MODE_IMMEDIATE
static struct line_buf_ctx isr_line_ctx;
static uint8_t isr_raw_char;
LOG_OUTPUT_DEFINE(isr_log_output, char_out, &isr_raw_char,
		  sizeof(isr_raw_char));
static struct k_spinlock isr_log_lock;
#endif /* CONFIG_LOG_MODE_IMMEDIATE */

static void log_backend_cros_ec_init(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);

	log_output_ctx_set(&thread_log_output, &thread_line_ctx);
#ifdef CONFIG_LOG_MODE_IMMEDIATE
	log_output_ctx_set(&isr_log_output, &isr_line_ctx);
#endif /* CONFIG_LOG_MODE_IMMEDIATE */
}

static void process(const struct log_backend *const backend,
		    union log_msg_generic *msg)
{
	ARG_UNUSED(backend);

	uint32_t flags =
		(log_backend_std_get_flags() & ~LOG_OUTPUT_FLAG_COLORS) |
		LOG_OUTPUT_FLAG_CRLF_LFONLY;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_LOG_BACKEND_CROS_EC_REDUCED)) {
		flags = (flags & ~LOG_OUTPUT_FLAG_LEVEL) |
			LOG_OUTPUT_FLAG_SKIP_SOURCE;
	}

	log_format_func_t log_output_func =
		log_format_func_t_get(LOG_OUTPUT_TEXT);

#ifdef CONFIG_LOG_MODE_IMMEDIATE
	if (!k_can_yield()) {
		/*
		 * Use a spinlock to protect isr_line_ctx when interrupts are
		 * still enabled in a non-yielding context (e.g. the idle
		 * thread or nested interrupts) and across CPUs on SMP targets.
		 * Masking local interrupts also ensures zephyr_print() uses
		 * printk_unlocked() instead of trying to lock the shell from
		 * the idle thread.
		 */
		k_spinlock_key_t key = k_spin_lock(&isr_log_lock);

		isr_line_ctx.len = 0;
		log_output_func(&isr_log_output, &msg->log, flags);
		flush_line_buf(&isr_line_ctx);

		k_spin_unlock(&isr_log_lock, key);
		return;
	}
#endif /* CONFIG_LOG_MODE_IMMEDIATE */

	bool lock = k_can_yield();

	if (lock) {
		k_mutex_lock(&thread_log_mutex, K_FOREVER);
	}

	thread_line_ctx.len = 0;
	log_output_func(&thread_log_output, &msg->log, flags);
	flush_line_buf(&thread_line_ctx);

	if (lock) {
		k_mutex_unlock(&thread_log_mutex);
	}
}

static void panic(struct log_backend const *const backend)
{
	ARG_UNUSED(backend);

	/* Switch the shell to blocking mode in case it is used during panic. */
	zephyr_console_panic();
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);

	bool lock = k_can_yield();

	if (lock) {
		k_mutex_lock(&thread_log_mutex, K_FOREVER);
	}

	thread_line_ctx.len = 0;
	log_backend_std_dropped(&thread_log_output, cnt);
	flush_line_buf(&thread_line_ctx);

	if (lock) {
		k_mutex_unlock(&thread_log_mutex);
	}
}

const struct log_backend_api log_backend_cros_ec_api = {
	.process = process,
	.panic = panic,
	.init = log_backend_cros_ec_init,
	.dropped = IS_ENABLED(CONFIG_LOG_MODE_DEFERRED) ? dropped : NULL,
};
LOG_BACKEND_DEFINE(log_backend_cros_ec, log_backend_cros_ec_api, true);
