/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_RTQ3700FH_H_
#define EMUL_RTQ3700FH_H_

#include <zephyr/drivers/emul.h>

/**
 * @brief Reset the RTQ3700FH emulator registers to their default state.
 */
void rtq3700fh_emul_reset_regs(const struct emul *emul);

/**
 * @brief Set the NVM status register value for testing polling timeouts.
 *
 * @param status The 8-bit status value (e.g., 0xE0 for ready).
 */
void rtq3700fh_emul_set_nvm_status(const struct emul *emul, uint8_t status);

/**
 * @brief Set the FW version register value for testing update triggers.
 */
void rtq3700fh_emul_set_fw_version(const struct emul *emul, uint16_t version);

/**
 * @brief Set the NVM checksum register value for testing CRC match/mismatch.
 */
void rtq3700fh_emul_set_checksum(const struct emul *emul, uint32_t checksum);

/**
 * @brief Check if the IMVP chip is currently unlocked.
 */
bool rtq3700fh_emul_is_unlocked(const struct emul *emul);

#endif /* EMUL_RTQ3700FH_H_ */