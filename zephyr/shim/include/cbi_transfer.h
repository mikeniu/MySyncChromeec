/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CBI_TRANSFER_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CBI_TRANSFER_H_

/**
 * @brief Transfer CBI from EEPROM to CBI section on EC flash
 *
 * The function has to be called before performing any read
 * or write operation on CBI.
 */
void cros_cbi_transfer_eeprom_to_flash(void);
#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CBI_TRANSFER_H_ */
