/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TEST_SYSTEM_COMMON_CMSIS_CORE_H_
#define TEST_SYSTEM_COMMON_CMSIS_CORE_H_

#include <stdint.h>

#define MPU_RASR_ENABLE_Msk 1UL

struct mpu_regs {
	uint32_t TYPE;
	uint32_t CTRL;
	uint32_t RNR;
	uint32_t RBAR;
	uint32_t RASR;
};

struct arm_mpu_region {
	uint32_t base;
};

struct arm_mpu_config {
	uint32_t num_regions;
	const struct arm_mpu_region *mpu_regions;
};

extern struct mpu_regs mock_mpu;
extern const struct arm_mpu_config mpu_config;

#define MPU (&mock_mpu)

#endif /* TEST_SYSTEM_COMMON_CMSIS_CORE_H_ */
