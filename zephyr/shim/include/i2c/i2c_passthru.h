/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHROME_I2C_I2C_PASSTHRU_H
#define ZEPHYR_CHROME_I2C_I2C_PASSTHRU_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED_DTS
enum i2c_passthru_allow_type {
	ALLOW_IF_SYSTEM_UNLOCKED,
	ALLOW_IF_UNPROTECTED_BY_AP,
	ALLOW_IF_FW_CONFIG_MATCH_AND_UNPROTECTED_BY_AP,
	BATTERY_ALLOW_PASSTHRU,
};

struct i2c_passthru_t {
	int port;
	enum i2c_passthru_allow_type allow_type;
#ifdef CONFIG_PLATFORM_EC_CBI
	int key; // enum cbi_fw_config_field_id
	uint32_t value;
#endif
};

struct ec_params_i2c_passthru;

/**
 * @brief Check if I2C passthru is allowed on a port for given parameters.
 *
 * This function calls the port-specific allow_passthru callback defined in DTS.
 *
 * @param port The EC port number.
 * @param params I2C passthru parameters.
 * @param protect_by_ap AP protection status.
 * @return 1 if passthru is allowed, 0 if not allowed or callback not defined.
 */
int i2c_allow_passthru(int port, const struct ec_params_i2c_passthru *params,
		       bool protect_by_ap);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_CHROME_I2C_I2C_PASSTHRU_H */
