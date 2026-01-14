/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_BTN_IGN_OUT_H
#define __CROS_EC_FPSENSOR_FPSENSOR_BTN_IGN_OUT_H

#include "ec_commands.h"

#include <cstdint>

namespace btn_ign_out
{

void activate();
void deactivate();

inline void update(std::uint32_t sensor_mode)
{
#ifdef CONFIG_PLATFORM_EC_FINGERPRINT_BTN_IGN_OUT
	if (sensor_mode &
	    (FP_MODE_ENROLL_SESSION | FP_MODE_MATCH | FP_MODE_CAPTURE))
		activate();
	else
		deactivate();
#endif
}

} // namespace btn_ign_out

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_BTN_IGN_OUT_H */
