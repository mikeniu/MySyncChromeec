/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LCOV_EXCL_START - non-shipping code */

#include "common_pdc_fwup.h"
#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>

int pdc_common_fwup_parse_start_cli_args(const struct shell *sh, size_t argc,
					 char **argv, struct i2c_dt_spec *i2c)
{
	int rv;
	const struct device *dev;
	char *e;

	/* A PDC chip can be specified in one of two ways:
	 *
	 *  1) By USB-C port number, in which case the updater will look up the
	 *     associated PDC's I2C bus info through the PDC API.
	 *  2) By raw I2C bus name and address. The updater will attempt to
	 *     communicate with a PDC at the provided target address on the
	 *     specified bus. This is useful if the currently-flashed EC
	 *     firmware has an inaccurate PDC config in the devicetree.
	 */

	if (i2c == NULL || argv == NULL) {
		return -EINVAL;
	}

	if (argc == 2) {
		uint8_t port;
		struct pdc_hw_config_t hw_config;

		/* User specified a USB-C port number */
		port = strtoul(argv[1], &e, 0);
		if (*e || port >= pdc_power_mgmt_get_usb_pd_port_count()) {
			shell_error(sh, "PDC_FWUP: Invalid port");
			return -EINVAL;
		}

		dev = pdc_power_mgmt_get_port_pdc_driver(port);
		if (dev == NULL) {
			shell_error(
				sh,
				"PDC_FWUP: Cannot locate PDC driver for C%u",
				port);
			return -ENOENT;
		}

		/* Get I2C info for this port. This is the only thing we need
		 * the PDC driver for. The rest of the update process bypasses
		 * the driver and makes direct I2C transactions.
		 */
		rv = pdc_get_hw_config(dev, &hw_config);
		if (rv) {
			shell_error(sh, "PDC_FWUP: Cannot get PDC I2C info: %d",
				    rv);
			return rv;
		}

		*i2c = hw_config.i2c;
		return 0;

	} else if (argc >= 3) {
		/* User specified an I2C bus and target address */

		uint16_t i2c_addr;

		/* Search for I2C bus driver */
		dev = shell_device_get_binding(argv[1]);
		if (!dev) {
			shell_error(sh, "PDC_FWUP: Cannot find I2C driver '%s'",
				    argv[1]);
			return -ENODEV;
		}

		if (!DEVICE_API_IS(i2c, dev)) {
			shell_error(
				sh,
				"PDC_FWUP: Device '%s' is not an I2C driver.",
				argv[1]);
			return -EINVAL;
		}

		i2c_addr = strtoul(argv[2], &e, 0);
		if (*e) {
			shell_error(sh, "PDC_FWUP: Invalid I2C addr or flags");
			return -EINVAL;
		}

		*i2c = (struct i2c_dt_spec){
			.bus = dev,
			.addr = i2c_addr,
		};
		return 0;
	}

	/* Invalid number of args */
	return -ERANGE;
}

/* LCOV_EXCL_STOP */
