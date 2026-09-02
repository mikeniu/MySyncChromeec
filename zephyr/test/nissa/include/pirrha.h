/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_PIRRHA_H_
#define PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_PIRRHA_H_

extern const struct ec_response_keybd_config pirrha_kb_legacy;

void panel_power_detect_init(void);
void lcd_reset_detect_init(void);
void handle_tsp_ta(void);
void pirrha_callback_init(void);

#endif /* PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_PIRRHA_H_ */
