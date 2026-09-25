/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_SERIAL_H_
#define PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_SERIAL_H_

#include <stdbool.h>
#include <stdint.h>

void serial_init(void);
void serial_send_host_char(uint8_t data);
bool serial_receive_host_char(uint8_t *rx_data);
enum failure_resp_type serial_receive_host_bytes(uint8_t *buff, uint8_t len);

#endif /* PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_SERIAL_H_ */
