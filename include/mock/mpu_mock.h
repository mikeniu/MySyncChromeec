/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_MPU_MOCK_H
#define __MOCK_MPU_MOCK_H

#include <stdint.h>

struct mock_mpu_state {
	uint32_t type;
	uint32_t ctrl;
	uint32_t base[16];
	uint32_t rasr[16];
};

extern struct mock_mpu_state mock_mpu;

void mock_mpu_reset(void);

#endif /* __MOCK_MPU_MOCK_H */
