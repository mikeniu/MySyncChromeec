/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_KB8010_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_KB8010_H_

#include <zephyr/drivers/emul.h>
#include <zephyr/sys/slist.h>

/**
 * @brief Assert/deassert reset GPIO to the kb8010 retimer.
 *
 * @param emul Pointer to kb8010 emulator
 * @param assert_reset State of the reset signal
 */
void kb8010_emul_set_reset(const struct emul *emul, bool assert_reset);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_KB8010_H_ */
