/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_FAKE_SYSTEM_FAKE_H
#define ZEPHYR_FAKE_SYSTEM_FAKE_H

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

#endif /* ZEPHYR_FAKE_SYSTEM_FAKE_H */
