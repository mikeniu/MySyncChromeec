/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_FAKE_INCLUDE_SYSTEM_FAKE_H_
#define PLATFORM_EC_ZEPHYR_FAKE_INCLUDE_SYSTEM_FAKE_H_

#include "ec_commands.h"

#include <setjmp.h>

/**
 * @brief Reset the fake interrupt disabled state.
 */
void system_fake_reset_interrupt_disabled(void);

/**
 * @brief Check whether interrupts have been disabled in the fake.
 *
 * @return true if interrupts are disabled, false otherwise.
 */
bool system_fake_is_interrupt_disabled(void);

/**
 * @brief Set the current image copy.
 */
void system_set_shrspi_image_copy(enum ec_image new_image_copy);

/**
 * @brief Set the fake environment
 */
void system_fake_setenv(jmp_buf *env);

#endif /* PLATFORM_EC_ZEPHYR_FAKE_INCLUDE_SYSTEM_FAKE_H_ */
