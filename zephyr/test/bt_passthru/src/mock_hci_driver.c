/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_hci_driver.h"

#include <string.h>

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT zephyr_bt_hci_mock

LOG_MODULE_REGISTER(mock_bt_hci, LOG_LEVEL_DBG);

#define MAX_TX_PACKETS 32
#define MAX_TX_PACKET_LEN 512

struct tx_packet {
	uint8_t data[MAX_TX_PACKET_LEN];
	size_t len;
};

static const struct device *g_mock_dev;
static struct tx_packet g_tx_packets[MAX_TX_PACKETS];
static size_t g_tx_packet_count;

static int mock_open(const struct device *dev)
{
	g_mock_dev = dev;
	LOG_INF("mock_bt_hci open called");
	return 0;
}

static int mock_send(const struct device *dev, struct net_buf *buf)
{
	if (g_tx_packet_count < MAX_TX_PACKETS) {
		size_t copy_len = buf->len < MAX_TX_PACKET_LEN ?
					  buf->len :
					  MAX_TX_PACKET_LEN;
		memcpy(g_tx_packets[g_tx_packet_count].data, buf->data,
		       copy_len);
		g_tx_packets[g_tx_packet_count].len = copy_len;
		g_tx_packet_count++;
	}

	LOG_HEXDUMP_DBG(buf->data, buf->len, "Mock Controller RX <- Zephyr");
	net_buf_unref(buf);
	return 0;
}

static DEVICE_API(bt_hci, mock_drv_api) = {
	.open = mock_open,
	.send = mock_send,
};

#define MOCK_DEVICE_INIT(inst)                                        \
	static struct bt_hci_driver_data mock_data_##inst = {};       \
	static const struct bt_hci_driver_config mock_config_##inst = \
		BT_DT_HCI_DRIVER_CONFIG_INST_GET(inst);               \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, &mock_data_##inst,    \
			      &mock_config_##inst, POST_KERNEL,       \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,     \
			      &mock_drv_api);

DT_INST_FOREACH_STATUS_OKAY(MOCK_DEVICE_INIT)

void mock_hci_driver_reset(void)
{
	g_tx_packet_count = 0;
}

size_t mock_hci_driver_get_tx_count(void)
{
	return g_tx_packet_count;
}

size_t mock_hci_driver_get_last_tx_packet(uint8_t *out_buf, size_t max_len)
{
	if (g_tx_packet_count == 0 || out_buf == NULL) {
		return 0;
	}
	return mock_hci_driver_get_tx_packet(g_tx_packet_count - 1, out_buf,
					     max_len);
}

size_t mock_hci_driver_get_tx_packet(size_t index, uint8_t *out_buf,
				     size_t max_len)
{
	if (index >= g_tx_packet_count || out_buf == NULL) {
		return 0;
	}
	size_t len = g_tx_packets[index].len < max_len ?
			     g_tx_packets[index].len :
			     max_len;
	memcpy(out_buf, g_tx_packets[index].data, len);
	return len;
}

int mock_hci_driver_inject_empty_rx(void)
{
	if (!g_mock_dev) {
		return -EINVAL;
	}

	struct net_buf *buf = bt_buf_get_rx(BT_BUF_EVT, K_NO_WAIT);
	if (!buf) {
		LOG_ERR("Failed to allocate mock RX buffer");
		return -ENOMEM;
	}

	net_buf_reset(buf);
	bt_hci_recv(g_mock_dev, buf);

	return 0;
}
