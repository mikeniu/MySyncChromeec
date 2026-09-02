/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_BT_PASSTHRU_MOCK_HCI_DRIVER_H_
#define ZEPHYR_TEST_BT_PASSTHRU_MOCK_HCI_DRIVER_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <vector>
extern "C" {
#endif

void mock_hci_driver_reset(void);
size_t mock_hci_driver_get_tx_count(void);
size_t mock_hci_driver_get_last_tx_packet(uint8_t *out_buf, size_t max_len);
size_t mock_hci_driver_get_tx_packet(size_t index, uint8_t *out_buf,
				     size_t max_len);
int mock_hci_driver_inject_empty_rx(void);

#ifdef __cplusplus
}

/* C++ helper functions */
inline std::vector<uint8_t> mock_hci_driver_get_last_tx_packet_vec(void)
{
	uint8_t buf[512];
	size_t len = mock_hci_driver_get_last_tx_packet(buf, sizeof(buf));
	if (len == 0) {
		return {};
	}
	return std::vector<uint8_t>(buf, buf + len);
}

inline std::vector<uint8_t> mock_hci_driver_get_tx_packet_vec(size_t index)
{
	uint8_t buf[512];
	size_t len = mock_hci_driver_get_tx_packet(index, buf, sizeof(buf));
	if (len == 0) {
		return {};
	}
	return std::vector<uint8_t>(buf, buf + len);
}
#endif

#endif /* ZEPHYR_TEST_BT_PASSTHRU_MOCK_HCI_DRIVER_H_ */
