/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "mock/mpu_mock.h"
#include "mpu.h"
#include "mpu_private.h"
#include "util.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_mpu_state mock_mpu = MOCK_MPU_DEFAULT;

void mock_mpu_reset(void)
{
	mock_mpu = MOCK_MPU_DEFAULT;
}

int mpu_update_region(uint8_t region, uint32_t addr, uint8_t size_bit,
		      uint16_t attr, uint8_t enable, uint8_t srd)
{
	if (!is_aligned(addr, BIT(size_bit)))
		return -EC_ERROR_INVAL;

	if (region >= mpu_num_regions() || region >= MOCK_MPU_MAX_REGIONS)
		return -EC_ERROR_INVAL;

	if (size_bit < MPU_SIZE_BITS_MIN)
		return -EC_ERROR_INVAL;

	mock_mpu.rasr[region] &= ~1;
	if (enable) {
		mock_mpu.base[region] = addr;
		mock_mpu.rasr[region] = ((uint32_t)attr << 16) | (srd << 8) |
					((size_bit - 1) << 1) | 1;
	}

	return EC_SUCCESS;
}

void mpu_enable(void)
{
	mock_mpu.ctrl |= MPU_CTRL_PRIVDEFEN | MPU_CTRL_HFNMIENA |
			 MPU_CTRL_ENABLE;
}

void mpu_disable(void)
{
	mock_mpu.ctrl &=
		~(MPU_CTRL_PRIVDEFEN | MPU_CTRL_HFNMIENA | MPU_CTRL_ENABLE);
}

uint32_t mpu_get_type(void)
{
	return mock_mpu.type;
}
