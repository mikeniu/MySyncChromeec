/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TEST_FINGERPRINT_ALG_FT98XX_FOCAL_ALGO_MOCKS_H_
#define TEST_FINGERPRINT_ALG_FT98XX_FOCAL_ALGO_MOCKS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/fff.h>

#include <drivers/fingerprint/fingerprint_ft98xx_private.h>

/* Mock sensor query APIs. */
DECLARE_FAKE_VALUE_FUNC(uint16_t, ft_sensor_query_rows);
DECLARE_FAKE_VALUE_FUNC(uint16_t, ft_sensor_query_cols);

/*
 * Mock focal algorithm APIs matching fingerprint_ft98xx_private.h exact types.
 */
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_set_buffer, uint16_t *, uint8_t *,
			uint8_t *);

DECLARE_FAKE_VALUE_FUNC(int, focal_algo_init, algo_param_t);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_deinit);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_get_finger_detailed_info, int *, int *,
			int *);
DECLARE_FAKE_VOID_FUNC(focal_algo_version, uint8_t *);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_enroll_start);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_get_feature, uint8_t *, uint8_t *,
			int *);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_enroll_step, uint8_t *, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_enroll_finish, uint8_t *, uint32_t *);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_enroll_cancel);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_match, uint8_t *, uint8_t *, uint8_t *);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_anti_spoofing, uint16_t *);
DECLARE_FAKE_VALUE_FUNC(int, focal_algo_update_template_by_feature, uint8_t *,
			uint8_t *);

#endif /* TEST_FINGERPRINT_ALG_FT98XX_FOCAL_ALGO_MOCKS_H_ */
