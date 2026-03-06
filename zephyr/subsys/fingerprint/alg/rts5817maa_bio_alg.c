/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>

#include <zephyr/drivers/fingerprint/fingerprint_rts5817maa_private.h>
#include <zephyr/drivers/fingerprint/fingerprint_rts5817maa_pal.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <fingerprint/fingerprint_alg.h>
#include <zephyr/include/drivers/fingerprint.h>

/*
 * This file contains implementation of the algorithm API of the RTS5817MAA
 * library (algorithm).
 */

LOG_MODULE_REGISTER(rts5817maa_alg, LOG_LEVEL_INF);

const rts5817maa_bio_info_t rts_bio_info = {
	.template_size = CONFIG_FP_ALGORITHM_TEMPLATE_SIZE,
};

static int
rts5817maa_algorithm_init(const struct fingerprint_algorithm *const alg)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	return rts_bio_algorithm_init();
}

static int
rts5817maa_algorithm_exit(const struct fingerprint_algorithm *const alg)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	rts_bio_algorithm_exit();

	return 0;
}

static int
rts5817maa_enroll_start(const struct fingerprint_algorithm *const alg)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	return rts_bio_enroll_start();
}

static int rts5817maa_enroll_step(const struct fingerprint_algorithm *const alg,
				  const uint8_t *const image, int *completion)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	rts_fp_return_t ret = rts_bio_enroll_step(image, completion);

	switch (ret) {
	case RTS_FP_SUCCESS:
		return FP_ENROLLMENT_RESULT_OK;
	case RTS_FP_IMMOBILE:
		return FP_ENROLLMENT_RESULT_IMMOBILE;
	case RTS_FP_POOR_QUALITY:
		return FP_ENROLLMENT_RESULT_LOW_QUALITY;
	case RTS_FP_TOO_HIGH:
	case RTS_FP_TOO_LOW:
	case RTS_FP_TOO_LEFT:
	case RTS_FP_TOO_RIGHT:
		return FP_ENROLLMENT_RESULT_LOW_COVERAGE;
	default:
		LOG_ERR("rts5817maa_enroll_step() failed, result %d", ret);
		return -EINVAL;
	}
}

static int
rts5817maa_enroll_finish(const struct fingerprint_algorithm *const alg,
			 void *templ)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	rts_bio_enroll_finish(templ);

	return 0;
}

static int rts5817maa_match(const struct fingerprint_algorithm *const alg,
			    void *templ, uint32_t templ_count,
			    const uint8_t *const image, bool template_update,
			    int32_t *match_index, uint32_t *update_bitmap)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	rts_fp_return_t ret = rts_bio_match(templ, templ_count, image,
		template_update, match_index, update_bitmap);

	switch (ret) {
	case RTS_FP_SUCCESS:
		return FP_MATCH_RESULT_MATCH;
	case RTS_FP_MATCH_UPDATED:
		return FP_MATCH_RESULT_MATCH_UPDATED;
	case RTS_FP_MATCH_FAIL:
		return FP_MATCH_RESULT_NO_MATCH;
	case RTS_FP_POOR_QUALITY:
		return FP_MATCH_RESULT_LOW_QUALITY;
	case RTS_FP_TOO_HIGH:
	case RTS_FP_TOO_LOW:
	case RTS_FP_TOO_LEFT:
	case RTS_FP_TOO_RIGHT:
		return FP_MATCH_RESULT_LOW_COVERAGE;
	default:
		assert(ret < 0);
		LOG_ERR("rts5817maa_match() failed, result %d", ret);
		return -EINVAL;
	}
}

const struct fingerprint_algorithm_api rts5817maa_api = {
	.init = rts5817maa_algorithm_init,
	.exit = rts5817maa_algorithm_exit,
	.enroll_start = rts5817maa_enroll_start,
	.enroll_step = rts5817maa_enroll_step,
	.enroll_finish = rts5817maa_enroll_finish,
	.match = rts5817maa_match,
};

FINGERPRINT_ALGORITHM_DEFINE(rts5817maa_algorithm, NULL, &rts5817maa_api);
