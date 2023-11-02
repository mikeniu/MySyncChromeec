/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __cplusplus
#error This header requires C++ to use.
#endif

#ifndef __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_MATCHER_H_
#define __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_MATCHER_H_

#include <stdint.h>

#if defined(HAVE_PRIVATE) && !defined(EMU_BUILD)

#if defined(CONFIG_FP_SENSOR_ELAN80) || defined(CONFIG_FP_SENSOR_ELAN515)
#include "elan/elan_matcher.h"
#define FP_ALGORITHM_TEMPLATE_SIZE (FP_ALGORITHM_TEMPLATE_SIZE_ELAN)
#define FP_MAX_FINGER_COUNT (FP_MAX_FINGER_COUNT_ELAN)
#endif /* CONFIG_FP_SENSOR_ELAN80 || CONFIG_FP_SENSOR_ELAN515 */

#if defined(CONFIG_FP_SENSOR_FPC1025) || defined(CONFIG_FP_SENSOR_FPC1035) || \
	defined(CONFIG_FP_SENSOR_FPC1145)
#include "fpc/fpc_matcher.h"
#define FP_ALGORITHM_TEMPLATE_SIZE (FP_ALGORITHM_TEMPLATE_SIZE_FPC)
#define FP_MAX_FINGER_COUNT (FP_MAX_FINGER_COUNT_FPC)
#endif /* FP_SENSOR_FPC1025 || FP_SENSOR_FPC1035 || FP_SENSOR_FPC1145 */

#endif /* HAVE_PRIVATE && !EMU_BUILD */

/* These values are used for public or host (emulator) tests. */
#if !defined(HAVE_PRIVATE) || defined(EMU_BUILD)
#define FP_ALGORITHM_TEMPLATE_SIZE 0
#define FP_MAX_FINGER_COUNT 5
#endif /* !HAVE_PRIVATE || EMU_BUILD */

#if defined(HAVE_PRIVATE) && defined(TEST_BUILD)
/*
 * For unittest in a private build, enable driver-related code in
 * common/fpsensor/ so that they can be tested (with fpsensor_mock).
 */
#define HAVE_FP_PRIVATE_DRIVER
#endif

#if !defined(FP_SENSOR_IMAGE_OFFSET) && defined(TEST_BUILD)
#define FP_SENSOR_IMAGE_OFFSET (0)
#endif

#ifdef CONFIG_LIB_DRUID

#undef noreturn
// #include "mcu/primitives/templates.h"
// #warning APPLYING TEMPLATE SIZE OVERRIDE
#undef FP_ALGORITHM_TEMPLATE_SIZE
// #define FP_ALGORITHM_TEMPLATE_SIZE sizeof(fingerprintauth::FingerTemplate)
// inline constexpr int kNumberPolarShapeletFilters = 22;
// #define FP_ALGORITHM_TEMPLATE_SIZE 28820
// inline constexpr int kNumberPolarShapeletFilters = 10;
#define FP_ALGORITHM_TEMPLATE_SIZE 14420
// (gdb) p sizeof(fingerprintauth::FingerTemplate)
#undef FP_MAX_FINGER_COUNT
#define FP_MAX_FINGER_COUNT 2
// typedef fingerprintauth::FingerTemplate fp_template_t;

#else

// typedef uint8_t fp_template_t[FP_ALGORITHM_TEMPLATE_SIZE];

#endif /* CONFIG_LIB_DRUID */

typedef uint8_t fp_template_t[FP_ALGORITHM_TEMPLATE_SIZE];

#endif /* __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_MATCHER_H_ */
