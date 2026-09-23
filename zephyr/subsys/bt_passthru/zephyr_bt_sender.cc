/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "zephyr_bt_sender.h"

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(bt_passthru, CONFIG_BT_PASSTHRU_LOG_LEVEL);

namespace chre
{

static bool h4TypeToBtBufType(uint8_t h4_type, enum bt_buf_type &type)
{
	switch (h4_type) {
	case BT_HCI_H4_CMD:
		type = BT_BUF_CMD;
		return true;
	default:
		LOG_ERR("Unsupported H4 Type: 0x%02X", h4_type);
		return false;
	}
}

void ZephyrBtSender::sendH4HciPacketToController(const uint8_t *h4Buf,
						 size_t bytes)
{
	/* Minimum H4 HCI Command size: [Type(1)] + struct bt_hci_cmd_hdr(3) */
	if (h4Buf == nullptr || bytes < (1 + sizeof(struct bt_hci_cmd_hdr))) {
		LOG_WRN("Malformed or truncated HCI packet: %zu bytes", bytes);
		return;
	}

	enum bt_buf_type type;
	if (!h4TypeToBtBufType(h4Buf[0], type)) {
		return;
	}

	const auto *cmd_hdr =
		reinterpret_cast<const struct bt_hci_cmd_hdr *>(&h4Buf[1]);
	if (bytes < (1 + sizeof(struct bt_hci_cmd_hdr) + cmd_hdr->param_len)) {
		LOG_WRN("Truncated HCI command: expected %zu, got %zu",
			1 + sizeof(struct bt_hci_cmd_hdr) + cmd_hdr->param_len,
			bytes);
		return;
	}

	LOG_HEXDUMP_DBG(h4Buf, bytes, "CHRE -> Controller");

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
