/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_ANRAGGAR_H_
#define PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_ANRAGGAR_H_

extern const struct ec_response_keybd_config anraggar_kb;

void board_setup_init(void);
void alt_sensor_init(void);

#endif /* PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_ANRAGGAR_H_ */
