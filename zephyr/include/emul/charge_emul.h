/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_INCLUDE_EMUL_CHARGE_EMUL_H
#define __ZEPHYR_INCLUDE_EMUL_CHARGE_EMUL_H

#include "charge_state.h"

/**
 * @brief Set the emulator's LED power state.
 *
 * @param state The new LED power state.
 */
void emul_charge_set_led_pwr_state(enum led_pwr_state state);

/**
 * @brief Set the emulator's active charge port.
 *
 * @param port The new active charge port.
 */
void emul_charge_set_active_port(int port);

#endif /* __ZEPHYR_INCLUDE_EMUL_CHARGE_EMUL_H */
