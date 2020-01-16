/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>
#include "common.h"
#include "console.h"
#include "endian.h"
#include "fpsensor.h"
#include "gpio.h"
#include "link_defs.h"
#include "spi.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "fpsensor_state.h"
#include "shared_mem.h"
#include "math_util.h"

#include "elan_setting.h"


#define CPRINTF(format, args...) cprintf(CC_FP, format, ## args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ## args)


void fp_sensor_low_power(void)
{

}


/* Reset and initialize the sensor IC */
int fp_sensor_init(void)
{
	algorithm_parameter_setting();
	ElanFP_ExcuteCalibration();
	return EC_SUCCESS;
}

/* Deinitialize the sensor IC */
int fp_sensor_deinit(void)
{
	return EC_SUCCESS;
}

int fp_sensor_get_info(struct ec_response_fp_info *resp)
{
	return EC_SUCCESS;
}

int fp_finger_match(void *templ, uint32_t templ_count, uint8_t *image,
		    int32_t *match_index, uint32_t *update_bitmap)
{
	CPRINTS("fp_finger_match");
	return elan_match(templ, templ_count, image,
	match_index, update_bitmap);
}

int fp_enrollment_begin(void)
{
	CPRINTS("fp_enrollment_begin");
	return elan_enrollment_begin();
}

int fp_enrollment_finish(void *templ)
{
	CPRINTS("fp_enrollment_finish");
	return elan_enrollment_finish(templ);
}


int fp_finger_enroll(uint8_t *image, int *completion)
{

	CPRINTS("fp_finger_enroll");
	return elan_enroll(image, completion);
}

void fp_sensor_configure_detect(void)
{
	CPRINTS("fp_sensor_configure_detect");
	ElanFP_WOEMODE();
}


int fp_sensor_acquire_image_with_mode(uint8_t *image_data, int mode)
{
	CPRINTS("fp_sensor_acquire_image_with_mode");
	return elan_sensor_acquire_image_with_mode(image_data, mode);
}

enum finger_state fp_sensor_finger_status(void)
{
	CPRINTS("fp_sensor_finger_status");
	return elan_sensor_finger_status();
}
