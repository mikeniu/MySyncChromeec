/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_hostcmd_transport.h"
#include "zephyr_bt_sender.h"

#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

LOG_MODULE_REGISTER(bt_passthru, CONFIG_BT_PASSTHRU_LOG_LEVEL);

static K_FIFO_DEFINE(hci_rx_queue);

static chre::ZephyrBtSender g_bt_sender;
static chre::CrosHostcmdTransport g_host_transport;

static void hci_rx_thread_entry(void *p1, void *p2, void *p3)
{
	struct net_buf *buf;

	LOG_INF("BT Passthru RX Thread Started");

	while (1) {
		/* Block until the Zephyr Raw HCI driver pushes a packet */
		buf = static_cast<struct net_buf *>(
			k_fifo_get(&hci_rx_queue, K_FOREVER));

		if (buf && buf->len > 0) {
			uint8_t h4_type = buf->data[0];
			LOG_INF("RX from BT Controller: H:4 Type 0x%02X, Size %d bytes",
				h4_type, buf->len);

			pw::span<const uint8_t> rx_payload(buf->data, buf->len);

			/* Write to Ring Buffer and trigger MKBP interrupt */
			g_host_transport.Write(0 /* type ignored */, rx_payload,
					       true /* wakeUp */);

			/* Free the Zephyr memory pool buffer */
			net_buf_unref(buf);
		} else if (buf) {
			LOG_WRN("Received empty buffer from HCI RX queue");
			net_buf_unref(buf);
		}
	}
}
K_THREAD_DEFINE(hci_rx_tid, CONFIG_BT_PASSTHRU_STACK_SIZE, hci_rx_thread_entry,
		NULL, NULL, NULL, CONFIG_BT_PASSTHRU_THREAD_PRIORITY, 0, 0);

static int bt_passthru_init(void)
{
	int err;

	LOG_INF("Init BT Passthru");

	err = bt_enable_raw(&hci_rx_queue);
	if (err) {
		LOG_ERR("Failed to open BT HCI Raw (err %d)", err);
		return err;
	}

	/*
	 * Register the TX callback. When the AP sends an HCI Command down via
	 * Host Commands, it is passed directly to the BT Sender.
	 */
	g_host_transport.Start(
		[](uint32_t /* type */, pw::span<const uint8_t> data,
		   chre::HostTransport::RespondToHost & /* resp */) {
			LOG_INF("TX to BT Controller: Size %zu bytes",
				data.size());
			g_bt_sender.sendH4HciPacketToController(data.data(),
								data.size());
			return pw::OkStatus();
		});

	return 0;
}
SYS_INIT(bt_passthru_init, APPLICATION, CONFIG_BT_PASSTHRU_INIT_PRIORITY);
