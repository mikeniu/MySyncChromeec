/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft98xx_bio_alg.h"

#include <stdio.h>

#include <zephyr/drivers/fingerprint/fingerprint_ft98xx_private.h>
#include <zephyr/logging/log.h>

#include <fingerprint/fingerprint_alg.h>

LOG_MODULE_REGISTER(ft98xx_bio_alg, LOG_LEVEL_INF);

/* algo buffer */
static uint8_t g_algo_buf[FT_ALGO_SIZE] __attribute__((aligned(4)));
/* feature data for one image */
static uint8_t g_feature[FT_TPL_SUBTPL_SIZE];

static int ft98xx_algorithm_init(const struct fingerprint_algorithm *const alg)
{
	int ret = 0;
	char alg_version[64];
	struct ft_libfp_data *data = (struct ft_libfp_data *)alg->data;

	if (!IS_ENABLED(CONFIG_HAVE_FT_LOCKER_PRIVATE_ALGORITHM)) {
		return 0;
	}

	algo_param_t algo_params = {
		.rows = ft_sensor_query_rows(),
		.cols = ft_sensor_query_cols(),
		.algo_size_limit = FT_ALGO_SIZE,
		.max_template_num = MAX_SUBTEMPLATE_COUNT_PER_FINGER,
		.enroll_template_num = SINGLE_FINGER_ENROLL_NUM,
		.enroll_similarity_enable = 2,
		.enroll_duplicated_finger_enable = 1,
		.image_quality_enable = 1,
		.update_template_enable = 1,
		.flash_erase_size = 0x1000,
		.update_frequency_num = 5,
		.policy_check_enable = 1,
		.enroll_quality_thr = 45,
		.enroll_area_thr = 50,
		.verify_quality_thr = 10,
		.verify_area_thr = 40,
		.use_desp_speed_up = 1,
		.enroll_reject_thr = 10,
		.enroll_continue_fail_thr = 3,
		.far_level = 1,
		.log_level = 1,
		.print_func_impl = NULL,
	};

	data->cols = algo_params.cols;
	data->rows = algo_params.rows;
	data->max_enroll_samples = algo_params.enroll_template_num;
	data->algo_buf = g_algo_buf;
	data->feature_buf = g_feature;

	ret = focal_algo_set_buffer(NULL, NULL, data->algo_buf);
	if (ret != 0) {
		LOG_ERR("memory must be 4-byte aligned(ret = %d)", ret);
	}

	ret = focal_algo_init(algo_params);
	if (ret != 0) {
		LOG_ERR("algorithm initial failed, ret = %d", ret);
	}

	focal_algo_get_finger_detailed_info(&data->tpl_finger_size,
					    &data->tpl_subtemplate_size,
					    &data->tpl_head_size);
	LOG_INF("algo:finger_size = %d, head_size = %d, sub_tpl_size= %d\n",
		data->tpl_finger_size, data->tpl_head_size,
		data->tpl_subtemplate_size);

	sprintf(alg_version, "api_%s_core_", LIBFP_API_VERSION);
	focal_algo_version((uint8_t *)&alg_version[strlen(alg_version)]);
	LOG_INF("algo ver: %s", alg_version);

	return 0;
}

static int ft98xx_algorithm_exit(const struct fingerprint_algorithm *const alg)
{
	if (!IS_ENABLED(CONFIG_HAVE_FT_LOCKER_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	focal_algo_deinit();

	return 0;
}

static int ft98xx_enroll_start(const struct fingerprint_algorithm *const alg)
{
	struct ft_libfp_data *data = (struct ft_libfp_data *)alg->data;

	if (!IS_ENABLED(CONFIG_HAVE_FT_LOCKER_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	data->remain = data->max_enroll_samples;
	focal_algo_enroll_start();

	return 0;
}

static int ft98xx_enroll_step(const struct fingerprint_algorithm *const alg,
			      const uint8_t *const image, int *completion)
{
	struct ft_libfp_data *data = (struct ft_libfp_data *)alg->data;

	int32_t ret = 0;
	int32_t feature_size = 0;
	uint8_t enroll_index = 0;

	if ((!IS_ENABLED(CONFIG_HAVE_FT_LOCKER_PRIVATE_ALGORITHM)) ||
	    (data->feature_buf == NULL)) {
		return -ENOTSUP;
	}

	if (data->remain == 0) {
		data->remain = data->max_enroll_samples;
	}

	enroll_index = data->max_enroll_samples - data->remain;

	memset(data->feature_buf, 0, FT_TPL_SUBTPL_SIZE);
	ret = focal_algo_get_feature((uint8_t *)image, data->feature_buf,
				     &feature_size);

	if (ret != 0) {
		LOG_ERR("focal_getfeature failed: ret = %d", ret);
		*completion = (data->max_enroll_samples - data->remain) * 100 /
			      data->max_enroll_samples;
		return FP_ENROLLMENT_RESULT_LOW_QUALITY;
	}

	ret = focal_algo_enroll_step(data->feature_buf, enroll_index);
	if (ret == 0) {
		data->remain -= 1;
		*completion = (data->max_enroll_samples - data->remain) * 100 /
			      data->max_enroll_samples;
		return FP_ENROLLMENT_RESULT_OK;
	} else {
		LOG_ERR("enroll failed: %d", ret);
		*completion = (data->max_enroll_samples - data->remain) * 100 /
			      data->max_enroll_samples;
		return FP_ENROLLMENT_RESULT_IMMOBILE;
	}

	return FP_ENROLLMENT_RESULT_INTERNAL_ERROR;
}

static int ft98xx_enroll_finish(const struct fingerprint_algorithm *const alg,
				void *templ)
{
	struct ft_libfp_data *data = (struct ft_libfp_data *)alg->data;
	uint32_t enroll_finger_size;

	if ((!IS_ENABLED(CONFIG_HAVE_FT_LOCKER_PRIVATE_ALGORITHM))) {
		return -ENOTSUP;
	}

	if (templ) {
		focal_algo_enroll_finish(templ, &enroll_finger_size);
	} else {
		focal_algo_enroll_cancel();
	}

	if (data)
		data->remain = 0;

	return FP_ENROLLMENT_RESULT_OK;
}

static int ft98xx_match(const struct fingerprint_algorithm *const alg,
			void *templ, uint32_t templ_count,
			const uint8_t *const image, int32_t *match_index,
			uint32_t *update_bitmap)
{
	int32_t ret = 0;
	int32_t feature_size = 0;
	uint8_t update_flag = 0;
	struct ft_libfp_data *data = (struct ft_libfp_data *)alg->data;

	if ((!IS_ENABLED(CONFIG_HAVE_FT_LOCKER_PRIVATE_ALGORITHM)) ||
	    (data->feature_buf == NULL)) {
		return -ENOTSUP;
	}

	memset(data->feature_buf, 0, FT_TPL_SUBTPL_SIZE);
	ret = focal_algo_get_feature((uint8_t *)image, data->feature_buf,
				     &feature_size);

	if (ret != 0) {
		LOG_ERR("focal_getfeature failed: ret = %d", ret);
		return FP_MATCH_RESULT_NO_MATCH;
	}

	for (int i = 0; i < templ_count; i++) {
		ret = focal_algo_match(data->feature_buf,
				       templ + i * FINGER_TEMPLATE_SIZE,
				       &update_flag);
		LOG_DBG("identify : %d %d", ret, update_flag);
		if (ret == 0) {
			/*match*/
			*match_index = i;

			if (update_flag) {
				/*update template*/
				ret = focal_algo_update_template_by_feature(
					data->feature_buf,
					templ + i * FINGER_TEMPLATE_SIZE);
				if (ret == 0) {
					*update_bitmap = (0x01 << i);
					return FP_MATCH_RESULT_MATCH_UPDATED;
				} else {
					return FP_MATCH_RESULT_MATCH_UPDATE_FAILED;
				}
			} else {
				return FP_MATCH_RESULT_MATCH;
			}
		}
	}

	return FP_MATCH_RESULT_NO_MATCH;
}

const struct fingerprint_algorithm_api ft98xx_api = {
	.init = ft98xx_algorithm_init,
	.exit = ft98xx_algorithm_exit,
	.enroll_start = ft98xx_enroll_start,
	.enroll_step = ft98xx_enroll_step,
	.enroll_finish = ft98xx_enroll_finish,
	.match = ft98xx_match,
};

static struct ft_libfp_data ft98xx_libfp_data;

FINGERPRINT_ALGORITHM_DEFINE(ft98xx_algorithm, &ft98xx_libfp_data, &ft98xx_api);
