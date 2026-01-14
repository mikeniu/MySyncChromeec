/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_BTN_IGN_H
#define __CROS_EC_FPSENSOR_FPSENSOR_BTN_IGN_H

#include "ec_commands.h"

#include <cstdint>
#ifdef CONFIG_PLATFORM_EC_BTN_IGN_OUT_FINGERPRINT
#include <drivers/btn_ign.h>
#endif

inline void update_btn_ign(std::uint32_t sensor_mode)
{
#ifdef CONFIG_PLATFORM_EC_BTN_IGN_OUT_FINGERPRINT
	if (sensor_mode &
	    (FP_MODE_ENROLL_SESSION | FP_MODE_MATCH | FP_MODE_CAPTURE))
		btn_ign_activate();
	else
		btn_ign_deactivate();
#endif
}

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_BTN_IGN_H */
