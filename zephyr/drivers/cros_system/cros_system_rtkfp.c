/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_version.h"
#include "drivers/cros_system.h"
#include "stdint.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

/* Driver data */
struct cros_system_rtkfp_data {
	int reset; /* reset cause */
};

static const char *cros_system_rtkfp_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "rtk";
}

static const char *cros_system_rtkfp_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	return NULL;
}

static const char *cros_system_rtkfp_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	return NULL;
}

static int cros_system_rtkfp_get_reset_cause(const struct device *dev)
{
	return 0;
}

static int cros_system_rtkfp_init(const struct device *dev)
{
	return 0;
}

static int cros_system_rtkfp_soc_reset(const struct device *dev)
{
	return 0;
}

static int cros_system_rtkfp_hibernate(const struct device *dev, uint32_t seconds,
				     uint32_t microseconds)
{
	return 0;
}

static const struct cros_system_driver_api cros_system_driver_rtkfp_api = {
	.get_reset_cause = cros_system_rtkfp_get_reset_cause,
	.soc_reset = cros_system_rtkfp_soc_reset,
	.hibernate = cros_system_rtkfp_hibernate,
	.chip_vendor = cros_system_rtkfp_get_chip_vendor,
	.chip_name = cros_system_rtkfp_get_chip_name,
	.chip_revision = cros_system_rtkfp_get_chip_revision,
};
#if CONFIG_CROS_SYSTEM_REALTEK_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

static struct cros_system_rtkfp_data cros_system_rtkfp_data_0;

DEVICE_DEFINE(cros_system_rtkfp_0, "CROS_SYSTEM", cros_system_rtkfp_init, NULL,
	      &cros_system_rtkfp_data_0, NULL, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_REALTEKFP_INIT_PRIORITY,
	      &cros_system_driver_rtkfp_api);
