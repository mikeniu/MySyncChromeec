/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_MPU_MOCK_H
#define __MOCK_MPU_MOCK_H

#include <stdint.h>

#define MOCK_MPU_MAX_REGIONS 16
#define MOCK_MPU_DEFAULT_TYPE 0x00000800 /* 8 unified regions */

struct mock_mpu_state {
	uint32_t type;
	uint32_t ctrl;
	uint32_t base[MOCK_MPU_MAX_REGIONS];
	uint32_t rasr[MOCK_MPU_MAX_REGIONS];
};

#define MOCK_MPU_DEFAULT \
	((struct mock_mpu_state){ .type = MOCK_MPU_DEFAULT_TYPE })

extern struct mock_mpu_state mock_mpu;

void mock_mpu_reset(void);

#endif /* __MOCK_MPU_MOCK_H */
