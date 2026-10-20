/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Design description.
 *
 * This driver is implemented for Realtek RTS5817 based chips, using
 * only software write protection. The hardware WP pin is not used for
 * flash chip protection.
 *
 * The GPIO WP signal (write_protect_is_asserted) is still used as a
 * system-level write protect indicator. When GPIO WP is asserted and
 * software protection is enabled, modifying the protection range is
 * restricted (only extending allowed).
 *
 * Software protection uses BP bits in the status register to define
 * the protected region (AT_BOOT flags). The software protection lock
 * (FLASH_RTS5817_EX_OP_SOFT_PROTECT) is used to determine _NOW flags.
 *
 * SRP0/SRP1 bits are not used since they are tied to hardware WP pin.
 * They are cleared at init to ensure no HW WP dependency.
 */

#define DT_DRV_COMPAT realtek_rtkfp_cros_flash

#include "flash.h"
#include "spi_flash_reg.h"
#include "write_protect.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/rts5817_flash_api_extensions.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_flash.h>
#include <soc.h>

LOG_MODULE_REGISTER(cros_flash, LOG_LEVEL_ERR);

#if !DT_NODE_EXISTS(DT_CHOSEN(zephyr_flash_controller))
#error "No suitable devicetree overlay specified for zephyr_flash_controller"
#endif

struct cros_flash_rtkfp_data {
	int sw_protected;
};

struct cros_flash_rtkfp_config {
	const struct device *flash_dev;
};

#define DRV_DATA(dev) ((struct cros_flash_rtkfp_data *)(dev)->data)
#define DRV_CONFIG(dev) ((const struct cros_flash_rtkfp_config *)(dev)->config)

#define FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

static const struct cros_flash_rtkfp_config cros_flash_config = {
	.flash_dev = DEVICE_DT_GET(FLASH_DEV),
};

static struct cros_flash_rtkfp_data cros_flash_data;

static int flash_get_status(const struct device *dev, uint8_t *sr1,
			    uint8_t *sr2)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	uint8_t sr[2];
	int rv;

	crec_flash_lock_mapped_storage(1);
	rv = flash_ex_op(cfg->flash_dev, FLASH_RTS5817_EX_OP_RD_SR,
			 (uintptr_t)NULL, sr);
	crec_flash_lock_mapped_storage(0);

	if (rv == 0) {
		*sr1 = sr[0];
		*sr2 = sr[1];
	}

	return rv;
}

static int flash_set_status(const struct device *dev, uint8_t sr1, uint8_t sr2)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	uint8_t regs[2] = { sr1, sr2 };
	int rv;

	crec_flash_lock_mapped_storage(1);
	rv = flash_ex_op(cfg->flash_dev, FLASH_RTS5817_EX_OP_WR_SR,
			 (uintptr_t)regs, NULL);
	crec_flash_lock_mapped_storage(0);

	return rv;
}

static int flash_check_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes)
{
	unsigned int start;
	unsigned int len;
	uint8_t sr1, sr2;
	int rv = EC_SUCCESS;

	/* Invalid value */
	if (bytes > CONFIG_FLASH_SIZE_BYTES ||
	    offset > CONFIG_FLASH_SIZE_BYTES - bytes) {
		return EC_ERROR_INVAL;
	}

	/* Compute current protect range */
	rv = flash_get_status(dev, &sr1, &sr2);
	if (rv) {
		return rv;
	}

	rv = spi_flash_reg_to_protect(sr1, sr2, &start, &len);
	if (rv) {
		/* Fail closed on undecodable status */
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Check if ranges overlap */
	if (MAX(start, offset) < MIN(start + len, offset + bytes)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

static int cros_flash_rtkfp_write(const struct device *dev, int offset,
				  int size, const char *data)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	int ret;

	/* check protection */
	ret = flash_check_prot_reg(dev, offset, size);
	if (ret) {
		return ret;
	}

	/* Invalid data pointer */
	if (data == NULL) {
		return EC_ERROR_INVAL;
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	LOG_DBG("fwrite %x, %x", offset, size);

	ret = flash_write(cfg->flash_dev, offset, data, size);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

static int cros_flash_rtkfp_erase(const struct device *dev, int offset,
				  int size)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	int ret;

	ret = flash_check_prot_reg(dev, offset, size);
	if (ret) {
		return ret;
	}

	if (size <= 0) {
		return EC_ERROR_INVAL;
	}

	crec_flash_lock_mapped_storage(1);

	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE) {
		ret = flash_erase(cfg->flash_dev, offset,
				  CONFIG_FLASH_ERASE_SIZE);
		if (ret) {
			break;
		}

		offset += CONFIG_FLASH_ERASE_SIZE;
	}
	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	if (ret) {
		LOG_ERR("Erase Failed, code %d.", ret);
	} else {
		LOG_DBG("Erase success.");
	}
	return ret;
}

static int cros_flash_rtkfp_sw_protect(const struct device *dev, bool enable)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	uint8_t val = enable ? 1 : 0;
	int rv;

	crec_flash_lock_mapped_storage(1);
	rv = flash_ex_op(cfg->flash_dev, FLASH_RTS5817_EX_OP_SOFT_PROTECT,
			 (uintptr_t)&val, NULL);
	crec_flash_lock_mapped_storage(0);
	return rv;
}

static int cros_flash_rtkfp_init(const struct device *dev)
{
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);
	uint8_t sr1, sr2;
	int ret;

	/*
	 * Make sure to clear SRP bits - only software protection.
	 * Read current status, clear SRP0/SRP1, and write back.
	 */
	ret = flash_get_status(dev, &sr1, &sr2);
	if (ret) {
		return ret;
	}

	if ((sr1 & SPI_FLASH_SR1_SRP0) || (sr2 & SPI_FLASH_SR2_SRP1)) {
		sr1 &= ~SPI_FLASH_SR1_SRP0;
		sr2 &= ~SPI_FLASH_SR2_SRP1;

		ret = flash_set_status(dev, sr1, sr2);
		if (ret) {
			return ret;
		}
	}

	if (write_protect_is_asserted()) {
		/* Emulate behaviour of #WP pin with SW protect. */
		ret = cros_flash_rtkfp_sw_protect(dev, true);
		if (ret) {
			return ret;
		}
		data->sw_protected = 1;
	}

	return EC_SUCCESS;
}

/* cros ec flash driver registration */
static const struct cros_flash_driver_api cros_flash_rtkfp_driver_api = {
	.init = cros_flash_rtkfp_init,
	.physical_write = cros_flash_rtkfp_write,
	.physical_erase = cros_flash_rtkfp_erase,
	.physical_get_status = flash_get_status,
};

BUILD_ASSERT(CONFIG_FLASH_INIT_PRIORITY < CONFIG_CROS_FLASH_INIT_PRIORITY);

static int flash_rtkfp_init(const struct device *dev)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);

	data->sw_protected = 0;

	if (!device_is_ready(cfg->flash_dev)) {
		LOG_ERR("device %s not ready", cfg->flash_dev->name);
		return -ENODEV;
	}

	return EC_SUCCESS;
}

DEVICE_DT_INST_DEFINE(0, flash_rtkfp_init, NULL, &cros_flash_data,
		      &cros_flash_config, POST_KERNEL,
		      CONFIG_CROS_FLASH_INIT_PRIORITY,
		      &cros_flash_rtkfp_driver_api);
