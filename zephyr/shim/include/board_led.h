/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BOARD_LED_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BOARD_LED_H_

#include <stdint.h>

#include <zephyr/drivers/pwm.h>

#ifdef __cplusplus
extern "C" {
#endif

struct board_led_pwm_dt_channel {
	const struct device *dev;
	uint32_t channel;
	pwm_flags_t flags;
};

#define BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(node_id)        \
	{                                                    \
		.dev = DEVICE_DT_GET(DT_PWMS_CTLR(node_id)), \
		.channel = DT_PWMS_CHANNEL(node_id),         \
		.flags = DT_PWMS_FLAGS(node_id),             \
	}

#define BOARD_LED_HZ_TO_PERIOD_NS(freq_hz) (NSEC_PER_SEC / freq_hz)

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BOARD_LED_H_ */
