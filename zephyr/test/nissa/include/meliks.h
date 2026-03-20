/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_MELIKS_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_MELIKS_H_

void panel_power_detect_init(void);
void lcd_reset_detect_init(void);
void handle_tsp_ta(void);
void power_handler(struct ap_power_ev_callback *cb,
		   struct ap_power_ev_data data);
void board_init_battery_type(void);

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_MELIKS_H_ */
