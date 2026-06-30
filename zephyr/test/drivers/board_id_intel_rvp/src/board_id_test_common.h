/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_BOARD_ID_INTEL_RVP_COMMON_H_
#define ZEPHYR_TEST_DRIVERS_BOARD_ID_INTEL_RVP_COMMON_H_

#include "cros_board_info.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/fff.h>

#include <ap_power/ap_pwrseq.h>
#include <drivers/rvp_board_id.h>

DECLARE_FAKE_VALUE_FUNC(int, cbi_get_model_id, uint32_t *);
DECLARE_FAKE_VALUE_FUNC(int, cbi_set_model_id, uint32_t);

void rvp_id_handler(void);
int board_get_version(void);

/* Emulated strap GPIO controller shared by the board-id test suites. */
extern const struct device *const rvp_gpio_dev;

/* Shared state consumed by cbi_get_model_id_match(). */
extern uint32_t cbi_model_id_value;

/* Custom fake returning cbi_model_id_value as the CBI model id. */
int cbi_get_model_id_match(uint32_t *id);

/* Drive the emulated strap GPIOs for each identifier. */
void set_board_gpios(uint32_t board_id);
void set_fab_gpios(uint32_t raw_fab);
void set_bom_gpios(uint32_t bom_id);

/*
 * Initialize the deferred strap controller if it is not ready yet. This is a
 * no-op once the controller has been initialized.
 */
void ensure_straps_ready(void);

/* Invoke the driver's registered S5-exit callbacks. */
void send_ap_pwrseq_s5_exit(enum ap_pwrseq_state entry);

/* Reset the fakes and, when the straps are ready, their pins before a test. */
void board_id_intel_rvp_reset(void);

#endif /* ZEPHYR_TEST_DRIVERS_BOARD_ID_INTEL_RVP_COMMON_H_ */
