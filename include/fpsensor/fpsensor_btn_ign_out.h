/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_BTN_IGN_OUT_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_BTN_IGN_OUT_H_

#include "ec_commands.h"

#include <cstdint>

namespace fp_btn_ign_out
{

#if defined(CONFIG_PLATFORM_EC_FINGERPRINT_BTN_IGN_OUT)
void update(std::uint32_t sensor_mode);
#else
inline void update(std::uint32_t sensor_mode)
{
}
#endif

} // namespace btn_ign_out

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_BTN_IGN_OUT_H_ */
