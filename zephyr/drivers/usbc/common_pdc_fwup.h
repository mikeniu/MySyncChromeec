/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef ZEPHYR_DRIVERS_USBC_COMMON_PDC_FWUP_H_
#define ZEPHYR_DRIVERS_USBC_COMMON_PDC_FWUP_H_

#include <stdlib.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>

/**
 * @brief Helper for parsing console-based PDC update CLI args
 *
 *        Accepts either a USB-C port number or an I2C bus name plus I2C target
 *        address and outputs an I2C DT spec struct to access the corresponding
 *        PDC chip:
 *
 *        -  Port No.:   argc=2 argv={"start", "0"}
 *        -  I2C target: argc=3 argv={"start", "I2C_PORT_PD", "0x20"}
 *
 * @param sh Pointer to shell instance, to print error messages
 * @param argc Number of args in argv
 * @param argv Array of string CLI args
 * @param i2c Output param for I2C info
 * @return 0 on success or negative error code on failure
 */
int pdc_common_fwup_parse_start_cli_args(const struct shell *sh, size_t argc,
					 char **argv, struct i2c_dt_spec *i2c);

#endif /* ZEPHYR_DRIVERS_USBC_COMMON_PDC_FWUP_H_ */
