/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CEC_BITBANG_CHIP_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CEC_BITBANG_CHIP_H_

/* 1:1 conversion between us and ticks for testing purposes */

#include <zephyr/drivers/counter.h>
#define cec_counter_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_cec_counter))

#if DT_NODE_HAS_STATUS(DT_CHOSEN(cros_ec_cec_counter), okay) && \
	!defined(CONFIG_TEST)
#define CEC_US_TO_TICKS(t) counter_us_to_ticks(cec_counter_dev, t)
#define CEC_TICKS_TO_US(ticks) counter_ticks_to_us(cec_counter_dev, ticks);
#else
/* Time in us to timer clock ticks */
#define CEC_US_TO_TICKS(t) (t)
#ifdef CONFIG_CEC_DEBUG
/* Timer clock ticks to us */
#define CEC_TICKS_TO_US(ticks) (ticks)
#endif
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CEC_BITBANG_CHIP_H_ */
