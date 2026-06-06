/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_REALTEK_RTS5453P_PUBLIC_H
#define __EMUL_REALTEK_RTS5453P_PUBLIC_H

#include "emul/emul_common_i2c.h"
#include "include/usb_pd.h"

#include <zephyr/drivers/emul.h>

#define RTS5453P_MAX_EPR_PDO_OFFSET 4

#define RTS5453P_FIXED_PDO_COMMON_FLAGS                                       \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_UNCONSTRAINED | PDO_FIXED_COMM_CAP | \
	 PDO_FIXED_DATA_SWAP)

#define RTS5453P_FIXED_SRC_FLAGS                               \
	(RTS5453P_FIXED_PDO_COMMON_FLAGS | PDO_FIXED_SUSPEND | \
	 PDO_FIXED_PEAK_CURR(PDO_PEAK_OVERCURR_110))
#define RTS5453P_FIXED_SNK_FLAGS (RTS5453P_FIXED_PDO_COMMON_FLAGS)

#define RTS5453P_FIXED1_SRC PDO_FIXED(12000, 5000, RTS5453P_FIXED_SRC_FLAGS)
#define RTS5453P_FIXED2_SRC PDO_FIXED(20000, 3000, RTS5453P_FIXED_SRC_FLAGS)

#define RTS5453P_FIXED_SNK PDO_FIXED(5000, 3000, RTS5453P_FIXED_SNK_FLAGS)
#define RTS5453P_BATT_SNK PDO_BATT(5000, 20000, 45000)
#define RTS5453P_VAR_SNK PDO_VAR(5000, 20000, 3000)

/**
 * @brief Get pointer to i2c_common_emul_data for RTS5453P emulator
 *
 * Used to configure I2C failure injection for testing error paths.
 *
 * @param emul Pointer to RTS5453P emulator
 * @return Pointer to i2c_common_emul_data structure
 */
struct i2c_common_emul_data *
rts5453p_emul_get_i2c_common_data(const struct emul *emul);

#endif
