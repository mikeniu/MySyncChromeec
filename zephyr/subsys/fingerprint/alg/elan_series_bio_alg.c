/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/fingerprint/fingerprint_elan_series_private.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <fingerprint/fingerprint_alg.h>

LOG_MODULE_REGISTER(elan_series_alg, LOG_LEVEL_INF);

/*
 * This file contains implementation of the algorithm API of the ELAN
 * elan_series library (algorithm).
 *
 */

static int
elan_series_algorithm_init(const struct fingerprint_algorithm *const alg)
{
	return 0;
}

static int
elan_series_algorithm_exit(const struct fingerprint_algorithm *const alg)
{
	return 0;
}

static int
elan_series_enroll_start(const struct fingerprint_algorithm *const alg)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN_SERIES_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	return elan_enrollment_begin();
}

static int
elan_series_enroll_step(const struct fingerprint_algorithm *const alg,
			const uint8_t *const image, int *completion)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN_SERIES_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	return elan_enroll((uint8_t *)image, completion);
}

static int
elan_series_enroll_finish(const struct fingerprint_algorithm *const alg,
			  void *templ)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN_SERIES_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	return elan_enrollment_finish(templ);
}

static int elan_series_match(const struct fingerprint_algorithm *const alg,
			     void *templ, uint32_t templ_count,
			     const uint8_t *const image, bool template_update,
			     int32_t *match_index, uint32_t *update_bitmap)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN_SERIES_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	int res = elan_match(templ, templ_count, (uint8_t *)image, match_index,
			     update_bitmap);
	if (res == FP_MATCH_RESULT_MATCH && template_update)
		res = elan_template_update(templ, *match_index);

	return res;
}

const struct fingerprint_algorithm_api elan_series_api = {
	.init = elan_series_algorithm_init,
	.exit = elan_series_algorithm_exit,
	.enroll_start = elan_series_enroll_start,
	.enroll_step = elan_series_enroll_step,
	.enroll_finish = elan_series_enroll_finish,
	.match = elan_series_match,
};

FINGERPRINT_ALGORITHM_DEFINE(elan_series_algorithm, NULL, &elan_series_api);
