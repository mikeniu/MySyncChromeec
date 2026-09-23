/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_FT98XX_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_FT98XX_H_

/**
 * Stop SPI transactions
 *
 * @param target The target emulator
 */
void ft98xx_stop_spi(const struct emul *target);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_FT98XX_H_ */
