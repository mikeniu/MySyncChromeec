/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_

#include <cstdint>

inline constexpr uint32_t FP_MODE_ANY_CAPTURE =
	(FP_MODE_CAPTURE | FP_MODE_ENROLL_IMAGE | FP_MODE_MATCH);

inline constexpr uint32_t FP_MODE_ANY_DETECT_FINGER =
	(FP_MODE_FINGER_DOWN | FP_MODE_FINGER_UP | FP_MODE_ANY_CAPTURE);

inline constexpr uint32_t FP_MODE_ANY_WAIT_IRQ =
	(FP_MODE_FINGER_DOWN | FP_MODE_ANY_CAPTURE);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_ */
