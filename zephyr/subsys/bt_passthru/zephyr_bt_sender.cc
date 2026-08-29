/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "zephyr_bt_sender.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(bt_passthru, CONFIG_BT_PASSTHRU_LOG_LEVEL);

namespace chre
{

void ZephyrBtSender::sendH4HciPacketToController(const uint8_t *h4Buf,
						 size_t bytes)
{
	if (h4Buf == nullptr || bytes == 0) {
		return;
	}

	LOG_HEXDUMP_DBG(h4Buf, bytes, "CHRE -> Controller");

	/* Map the H:4 indicator (byte 0) to Zephyr's buffer type */
	enum bt_buf_type type;
	switch (h4Buf[0]) {
	case 0x01: /* HCI Command */
		type = BT_BUF_CMD;
		break;
	default:
		LOG_ERR("Unsupported H4 Type: 0x%02X", h4Buf[0]);
		return;
	}

	/* Skip h4Buf[0] as Zephyr's bt_buf_get_tx automatically prepends it */
	struct net_buf *buf =
		bt_buf_get_tx(type, K_NO_WAIT, h4Buf + 1, bytes - 1);
	if (!buf) {
		LOG_ERR("Failed to allocate Zephyr TX buffer (type %d, size %zu)",
			type, bytes - 1);
		return;
	}

	int err = bt_send(buf);
	if (err) {
		LOG_ERR("bt_send failed (err %d), dropping packet", err);
		net_buf_unref(buf);
	}
}

} // namespace chre
