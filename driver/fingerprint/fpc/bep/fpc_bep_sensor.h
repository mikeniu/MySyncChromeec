/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_DRIVER_FINGERPRINT_FPC_BEP_FPC_BEP_SENSOR_H_
#define PLATFORM_EC_DRIVER_FINGERPRINT_FPC_BEP_FPC_BEP_SENSOR_H_

#if defined(CONFIG_FP_SENSOR_FPC1025)
#include "fpc1025_private.h"
#elif defined(CONFIG_FP_SENSOR_FPC1035)
#include "fpc1035_private.h"
#endif

#endif /* PLATFORM_EC_DRIVER_FINGERPRINT_FPC_BEP_FPC_BEP_SENSOR_H_ */
