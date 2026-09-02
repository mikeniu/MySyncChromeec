/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_SMBUS_ARA_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_SMBUS_ARA_H_

#include <zephyr/drivers/emul.h>

/**
 * @brief Queue ARA address to respond on next request (which will get
 * consumed). Addresses will respond from lowest port to highest when read.
 *
 * @param emul Pointer to SMBus ARA emulator
 * @param address Device address
 *
 * @return -1 if parameters are invalid.
 */
int emul_smbus_ara_queue_address(const struct emul *emul, int port,
				 uint8_t address);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_SMBUS_ARA_H_ */
