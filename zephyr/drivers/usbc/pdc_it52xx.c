/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ITE IT52XX Power Delivery Controller driver. */

#include "pdc_it52xx.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <drivers/pdc.h>

#define DT_DRV_COMPAT ite_it52xx_pdc

static int it52xx_pdc_reset(const struct device *dev)
{
	return -ENOSYS;
}

static int it52xx_get_info(const struct device *dev, struct pdc_info_t *info,
			   bool live)
{
	return -ENOSYS;
}

static int it52xx_get_error_status(const struct device *dev,
				   union error_status_t *es)
{
	return -ENOSYS;
}

static int it52xx_get_connector_status(const struct device *dev,
				       union connector_status_t *cs)
{
	return -ENOSYS;
}

static int it52xx_set_sink_path(const struct device *dev, bool en)
{
	return -ENOSYS;
}

static int it52xx_read_power_level(const struct device *dev)
{
	return -ENOSYS;
}

static int it52xx_get_connector_capability(const struct device *dev,
					   union connector_capability_t *caps)
{
	return -ENOSYS;
}

static int it52xx_set_uor(const struct device *dev, union uor_t uor)
{
	return -ENOSYS;
}

static int it52xx_set_pdr(const struct device *dev, union pdr_t pdr)
{
	return -ENOSYS;
}

static int it52xx_get_vdo(const struct device *dev, union get_vdo_t vdo_req,
			  uint8_t *vdo_req_list, uint32_t *vdo)
{
	return -ENOSYS;
}

static int it52xx_set_rdo(const struct device *dev, uint32_t rdo)
{
	return -ENOSYS;
}

static int it52xx_get_cable_property(const struct device *dev,
				     union cable_property_t *cp)
{
	return -ENOSYS;
}

static int it52xx_get_rdo(const struct device *dev, uint32_t *rdo)
{
	return -ENOSYS;
}

static int it52xx_connector_reset(const struct device *dev,
				  union connector_reset_t reset)
{
	return -ENOSYS;
}

static int it52xx_get_capability(const struct device *dev,
				 struct capability_t *caps)
{
	return -ENOSYS;
}

static int it52xx_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
	return -ENOSYS;
}

static int it52xx_get_ucsi_version(const struct device *dev, uint16_t *version)
{
	return -ENOSYS;
}

static int it52xx_get_hw_config(const struct device *dev,
				struct pdc_hw_config_t *config)
{
	return -ENOSYS;
}

static bool it52xx_get_hw_frs_support(const struct device *dev)
{
	return false;
}

static int it52xx_set_comms_state(const struct device *dev, bool comms_active)
{
	return -ENOSYS;
}

static int it52xx_set_handler_cb(const struct device *dev,
				 struct pdc_callback *callback)
{
	return -ENOSYS;
}

static void it52xx_start_thread(const struct device *dev)
{
}

static enum pdc_driver_init_state_t
it52xx_is_init_done(const struct device *dev)
{
	return PDC_DRIVER_INIT_STATE_WAIT;
}

static DEVICE_API(pdc, pdc_driver_api) = {
	.start_thread = it52xx_start_thread,
	.is_init_done = it52xx_is_init_done,
	.get_ucsi_version = it52xx_get_ucsi_version,
	.reset = it52xx_pdc_reset,
	.connector_reset = it52xx_connector_reset,
	.get_capability = it52xx_get_capability,
	.get_connector_capability = it52xx_get_connector_capability,
	.set_uor = it52xx_set_uor,
	.set_pdr = it52xx_set_pdr,
	.set_sink_path = it52xx_set_sink_path,
	.get_connector_status = it52xx_get_connector_status,
	.get_rdo = it52xx_get_rdo,
	.set_rdo = it52xx_set_rdo,
	.get_error_status = it52xx_get_error_status,
	.get_vbus_voltage = it52xx_get_vbus_voltage,
	.set_handler_cb = it52xx_set_handler_cb,
	.read_power_level = it52xx_read_power_level,
	.get_info = it52xx_get_info,
	.get_hw_config = it52xx_get_hw_config,
	.get_hw_frs_support = it52xx_get_hw_frs_support,
	.get_cable_property = it52xx_get_cable_property,
	.get_vdo = it52xx_get_vdo,
	.set_comms_state = it52xx_set_comms_state,
};

static int pdc_init(const struct device *dev)
{
	return 0;
}

#define IT52XX_PDC_DEFINE(inst)                                              \
	DEVICE_DT_INST_DEFINE(inst, pdc_init, NULL, NULL, NULL, POST_KERNEL, \
			      CONFIG_APPLICATION_INIT_PRIORITY,              \
			      &pdc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IT52XX_PDC_DEFINE)
