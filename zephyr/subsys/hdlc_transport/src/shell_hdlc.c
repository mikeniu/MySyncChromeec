/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hdlc_transport.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/ring_buffer.h>

struct shell_hdlc {
	shell_transport_handler_t handler;
	void *context;
	struct ring_buf rx_ringbuf;
	uint8_t rx_buf[CONFIG_SHELL_BACKEND_HDLC_RX_RING_BUFFER_SIZE];
};

static struct shell_hdlc sh_hdlc;
static atomic_t shell_rx_dropped = ATOMIC_INIT(0);

static void shell_hdlc_rx_cb(const uint8_t *data, size_t len, void *user_data)
{
	struct shell_hdlc *sh = (struct shell_hdlc *)user_data;

	size_t written = ring_buf_put(&sh->rx_ringbuf, data, len);

	if (written < len) {
		atomic_add(&shell_rx_dropped, len - written);
	}
	if (written > 0 && sh->handler != NULL) {
		sh->handler(SHELL_TRANSPORT_EVT_RX_RDY, sh->context);
	}
}

static int init(const struct shell_transport *transport, const void *config,
		shell_transport_handler_t evt_handler, void *context)
{
	struct shell_hdlc *sh = (struct shell_hdlc *)transport->ctx;

	ARG_UNUSED(config);

	sh->handler = evt_handler;
	sh->context = context;

	ring_buf_init(&sh->rx_ringbuf, sizeof(sh->rx_buf), sh->rx_buf);

	return hdlc_register_handler(HDLC_ADDR_SHELL, shell_hdlc_rx_cb, sh);
}

static int uninit(const struct shell_transport *transport)
{
	ARG_UNUSED(transport);
	return hdlc_unregister_handler(HDLC_ADDR_SHELL);
}

static int enable(const struct shell_transport *transport, bool blocking_tx)
{
	ARG_UNUSED(transport);
	ARG_UNUSED(blocking_tx);
	return 0;
}

static int write_hdlc(const struct shell_transport *transport, const void *data,
		      size_t length, size_t *cnt)
{
	struct shell_hdlc *sh = (struct shell_hdlc *)transport->ctx;

	int ret = hdlc_send_frame(HDLC_ADDR_SHELL, data, length);

	if (ret == 0) {
		*cnt = length;
	} else {
		*cnt = 0;
		return ret;
	}

	if (sh->handler != NULL) {
		sh->handler(SHELL_TRANSPORT_EVT_TX_RDY, sh->context);
	}

	return 0;
}

static int read_hdlc(const struct shell_transport *transport, void *data,
		     size_t length, size_t *cnt)
{
	struct shell_hdlc *sh = (struct shell_hdlc *)transport->ctx;

	*cnt = ring_buf_get(&sh->rx_ringbuf, (uint8_t *)data, length);
	return 0;
}

const struct shell_transport_api shell_hdlc_transport_api = {
	.init = init,
	.uninit = uninit,
	.enable = enable,
	.write = write_hdlc,
	.read = read_hdlc,
};

static const struct shell_transport shell_transport_hdlc = {
	.api = &shell_hdlc_transport_api,
	.ctx = &sh_hdlc,
};

SHELL_DEFINE(shell_hdlc, CONFIG_SHELL_PROMPT_HDLC, &shell_transport_hdlc,
	     CONFIG_SHELL_BACKEND_HDLC_LOG_MESSAGE_QUEUE_SIZE,
	     CONFIG_SHELL_BACKEND_HDLC_LOG_MESSAGE_QUEUE_TIMEOUT,
	     SHELL_FLAG_OLF_CRLF);

const struct shell *shell_backend_hdlc_get_ptr(void)
{
	return &shell_hdlc;
}

static int enable_shell_hdlc(void)
{
	static const struct shell_backend_config_flags cfg_flags =
		SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

	/* Shell is NOT the log backend; dedicated HDLC log backend is used */
	return shell_init(&shell_hdlc, NULL, cfg_flags, false, 0);
}

SYS_INIT(enable_shell_hdlc, POST_KERNEL,
	 CONFIG_SHELL_BACKEND_HDLC_INIT_PRIORITY);
