
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_FPSENSOR_FPSENSOR_LED_H_
#define __CROS_EC_FPSENSOR_FPSENSOR_LED_H_

#include "ec_commands.h"

#include <cstdint>

namespace fp_led
{

void enroll();
void match();
void match_ok();
void match_nok();
void off();

inline void update_mode(uint32_t sensor_mode)
{
#ifdef CONFIG_PLATFORM_EC_FP_LED
	if (sensor_mode & FP_MODE_ENROLL_SESSION)
		enroll();
	else if (sensor_mode & FP_MODE_MATCH)
		match();
	else
		off();
#endif
}

inline void update_match(bool success)
{
#ifdef CONFIG_PLATFORM_EC_FP_LED
	if (success)
		match_ok();
	else
		match_nok();
#endif
}
}

#endif /*__CROS_EC_FPSENSOR_FPSENSOR_LED_H_*/
