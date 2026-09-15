/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bt_passthru_hci_interceptor.h"

#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(bt_passthru, CONFIG_BT_PASSTHRU_LOG_LEVEL);

namespace chre
{

void BtPassthruHciInterceptor::synthesizeReadSupportedCommands()
{
	alignas(uint16_t) uint8_t
		rsp[1 + sizeof(struct bt_hci_evt_hdr) +
		    sizeof(struct bt_hci_evt_cmd_complete) +
		    sizeof(struct bt_hci_rp_read_supported_commands)] = { 0 };

	rsp[0] = BT_HCI_H4_EVT;

	auto *evt_hdr = reinterpret_cast<struct bt_hci_evt_hdr *>(&rsp[1]);
	evt_hdr->evt = BT_HCI_EVT_CMD_COMPLETE;
	evt_hdr->len = sizeof(struct bt_hci_evt_cmd_complete) +
		       sizeof(struct bt_hci_rp_read_supported_commands);

	auto *cc_hdr = reinterpret_cast<struct bt_hci_evt_cmd_complete *>(
		evt_hdr->data);
	cc_hdr->ncmd = 1;
	cc_hdr->opcode = sys_cpu_to_le16(BT_HCI_OP_READ_SUPPORTED_COMMANDS);

	auto *rp = reinterpret_cast<struct bt_hci_rp_read_supported_commands *>(
		evt_hdr->data + sizeof(struct bt_hci_evt_cmd_complete));
	rp->status = 0; /* Success */

	enum SupportedCommandsOctet : uint8_t {
		kOctetExtAdv = 36,
		kOctetExtScan = 37,
	};

	/*
	 * Command bitmask definitions per Bluetooth Core Spec
	 * Vol 4, Part E, Section 6.27 (Supported commands).
	 */

	/* Octet 36 */
	constexpr uint8_t kCmdLeSetAdvSetRandomAddr = BIT(1); /* 0x2035 */
	constexpr uint8_t kCmdLeSetExtAdvParams = BIT(2); /* 0x2036 */
	constexpr uint8_t kCmdLeSetExtAdvData = BIT(3); /* 0x2037 */
	constexpr uint8_t kCmdLeSetExtScanRspData = BIT(4); /* 0x2038 */
	constexpr uint8_t kCmdLeSetExtAdvEnable = BIT(5); /* 0x2039 */

	/* Octet 37 */
	constexpr uint8_t kCmdLeSetExtScanParams = BIT(5); /* 0x2041 */
	constexpr uint8_t kCmdLeSetExtScanEnable = BIT(6); /* 0x2042 */

	rp->commands[kOctetExtAdv] =
		kCmdLeSetAdvSetRandomAddr | kCmdLeSetExtAdvParams |
		kCmdLeSetExtAdvData | kCmdLeSetExtScanRspData |
		kCmdLeSetExtAdvEnable;
	rp->commands[kOctetExtScan] = kCmdLeSetExtScanParams |
				      kCmdLeSetExtScanEnable;

	LOG_INF("Intercepted 0x1002: synthesized supported commands complete");
	pw::span<const uint8_t> event_span(rsp, sizeof(rsp));
	mTransport.Write(0 /* type */, event_span, true /* wakeUp */);
}

bool BtPassthruHciInterceptor::interceptCommand(const uint8_t *h4Buffer,
						size_t length,
						BtClientType client)
{
	ARG_UNUSED(client);

	/* Minimum H4 HCI Command size: [Type(1)] + struct bt_hci_cmd_hdr(3) */
	if (h4Buffer == nullptr ||
	    length < (1 + sizeof(struct bt_hci_cmd_hdr)) ||
	    h4Buffer[0] != BT_HCI_H4_CMD) {
		return false;
	}

	const auto *cmd_hdr =
		reinterpret_cast<const struct bt_hci_cmd_hdr *>(&h4Buffer[1]);
	if (length < (1 + sizeof(struct bt_hci_cmd_hdr) + cmd_hdr->param_len)) {
		LOG_WRN("Truncated HCI command: expected %zu, got %zu",
			1 + sizeof(struct bt_hci_cmd_hdr) + cmd_hdr->param_len,
			length);
		return false;
	}

	uint16_t opcode = sys_le16_to_cpu(cmd_hdr->opcode);
	switch (opcode) {
	case BT_HCI_OP_READ_SUPPORTED_COMMANDS:
		synthesizeReadSupportedCommands();
		return true;
	default:
		return false;
	}
}

} // namespace chre
