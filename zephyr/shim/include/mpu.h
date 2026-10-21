/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_MPU_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_MPU_H_

#include <stdbool.h>
#include <stdint.h>

/* This matches up with core/cortex-m/include/mpu.h */

/* Location of iram.text */
extern char __iram_text_start;
extern char __iram_text_end;

/** Enable MPU */
void mpu_enable(void);

/**
 * Returns the value of MPU type register
 *
 * @returns 0 for now (always)
 */
uint32_t mpu_get_type(void);

/** Protect RAM from code execution */
int mpu_protect_data_ram(void);

/** Protect code RAM from being overwritten */
int mpu_protect_code_ram(void);

/** Protect internal mapped flash memory from code execution */
int mpu_lock_ro_flash(void);
int mpu_lock_rw_flash(void);

#ifdef CONFIG_PLATFORM_EC_ROLLBACK_MPU_PROTECT
/**
 * @brief Protects or unprotects the rollback region readback.
 *
 * @param lock true to lock (protect), false to unlock (unprotect).
 *
 * @retval 0 Success.
 * @retval <0 Error occurred.
 */
int mpu_lock_rollback(bool lock);
#endif

#ifdef CONFIG_TEST_CORTEX_M_MPU
#define MPU_RASR_ENABLE_Msk 1UL

struct cortex_m_mpu_regs {
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

extern struct cortex_m_mpu_regs test_mpu_regs;
extern const struct arm_mpu_config mpu_config;
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_MPU_H_ */
