/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ft_ft9001_cros_flash

#include "flash.h"
#include "spi_flash_reg.h"
#include "write_protect.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/ft90_flash_api_ex.h>
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

struct cros_flash_ft_data {
	int all_protected;
	int addr_prot_start;
	int addr_prot_length;
};

struct cros_flash_ft_config {
	const struct device *flash_dev;
};

#define DRV_DATA(dev) ((struct cros_flash_ft_data *)(dev)->data)
#define DRV_CONFIG(dev) ((const struct cros_flash_ft_config *)(dev)->config)

#define FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

static const struct cros_flash_ft_config cros_flash_config = {
	.flash_dev = DEVICE_DT_GET(FLASH_DEV),
};

static struct cros_flash_ft_data cros_flash_data;

static int cros_flash_ft90_set_status_reg(const struct device *dev,
					  uint8_t *reg)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);
	uint8_t send_sr = reg[0];

	int ret;

	/* Write status regs */
	ret = flash_ex_op(cfg->flash_dev, FT_FLASH_FT90_EX_OP_WR_SR,
			  (uintptr_t)NULL, &send_sr);

	return ret;
}

static int cros_flash_ft90_write_protection_set(const struct device *dev,
						bool enable)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);

	/* Write protection can be cleared only by core domain reset */
	if (!enable) {
		LOG_ERR("WP can be disabled only via core domain reset");
		return -ENOTSUP;
	}

	return flash_ex_op(cfg->flash_dev, FT_FLASH_EX_OP_SET_WP,
			   (uintptr_t)NULL, &enable);
}

static int is_int_flash_protected(const struct device *dev)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);
	uint8_t is_wp;
	int ret;

	ret = flash_ex_op(cfg->flash_dev, FT_FLASH_EX_OP_GET_WP,
			  (uintptr_t)&is_wp, NULL);
	if (ret != 0) {
		return ret;
	}

	return (is_wp != 0 ? 1 : 0);
}

static void flash_get_status(const struct device *dev, uint8_t *sr)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);
	crec_flash_lock_mapped_storage(1);

	/* Read status register1 */
	flash_ex_op(cfg->flash_dev, FT_FLASH_FT90_EX_OP_RD_SR, (uintptr_t)sr,
		    NULL);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);
}

/*
 * Check if Status Register Protect 0 (SRP0) bit in the Status 1 Register
 * is set.
 */
static bool flash_check_status_reg_srp(const struct device *dev)
{
	uint8_t sr;

	flash_get_status(dev, &sr);
	return (sr & SPI_FLASH_SR1_SRP0);
}

static int flash_set_status(const struct device *dev, uint8_t sr)
{
	int rv;
	uint8_t regs[2] = { sr };

	if (is_int_flash_protected(dev) && flash_check_status_reg_srp(dev)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);
	rv = cros_flash_ft90_set_status_reg(dev, regs);
	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
}

static void flash_protect_int_flash(const struct device *dev, int enable)
{
	/*
	 * Please notice the type of WP_IF bit is R/W1S. Once it's set,
	 * only rebooting EC can clear it.
	 */
	if (enable) {
		cros_flash_ft90_write_protection_set(dev, enable);
	}
}

static int flash_set_status_for_prot(const struct device *dev, int reg)
{
	struct cros_flash_ft_data *data = DRV_DATA(dev);
	int rv;

	if (write_protect_is_asserted()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now before setting them.
	 */
	flash_protect_int_flash(dev, write_protect_is_asserted());

	rv = flash_set_status(dev, reg);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	spi_flash_reg_to_protect(reg, 0, &data->addr_prot_start,
				 &data->addr_prot_length);
	return EC_SUCCESS;
}

static int flash_check_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes)
{
	unsigned int start;
	unsigned int len;
	uint8_t sr;
	int rv = EC_SUCCESS;

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now.
	 */
	flash_protect_int_flash(dev, write_protect_is_asserted());

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES) {
		return EC_ERROR_INVAL;
	}

	/* Compute current protect range */
	flash_get_status(dev, &sr);

	rv = spi_flash_reg_to_protect(sr, 0, &start, &len);

	if (rv) {
		return rv;
	}

	/* Check if ranges overlap */
	if (max(start, offset) < min(start + len, offset + bytes)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

/* set new protect range by writing status regs */
static int flash_write_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes, int hw_protect)
{
	uint8_t sr1, sr2;
	int rv;

	/* Invalid values */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES) {
		return EC_ERROR_INVAL;
	}

	/* Compute desired protect range */
	flash_get_status(dev, &sr1);

	rv = spi_flash_protect_to_reg(offset, bytes, &sr1, &sr2);
	if (rv) {
		return rv;
	}

	return flash_set_status_for_prot(dev, sr1);
}

static int flash_check_prot_range(const struct device *dev, unsigned int offset,
				  unsigned int bytes)
{
	struct cros_flash_ft_data *data = DRV_DATA(dev);

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES) {
		return EC_ERROR_INVAL;
	}

	if (max(data->addr_prot_start, offset) <
	    min(data->addr_prot_start + data->addr_prot_length,
		offset + bytes)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

static int cros_flash_ft_init(const struct device *dev)
{
	return EC_SUCCESS;
}

static int cros_flash_ft_write(const struct device *dev, int offset, int size,
			       const char *src_data)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);
	struct cros_flash_ft_data *data = DRV_DATA(dev);

	if (data->all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (flash_check_prot_range(dev, offset, size)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return flash_write(cfg->flash_dev, offset, src_data, size);
}

static int cros_flash_ft_erase(const struct device *dev, int offset, int size)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);
	struct cros_flash_ft_data *data = DRV_DATA(dev);

	if (data->all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (flash_check_prot_range(dev, offset, size)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return flash_erase(cfg->flash_dev, offset, size);
}

static int cros_flash_ft_get_protect(const struct device *dev, int bank)
{
	unsigned int addr = bank * CONFIG_FLASH_BANK_SIZE;
	int ret;

	ret = flash_check_prot_reg(dev, addr, CONFIG_FLASH_BANK_SIZE);

	return ret;
}

static uint32_t cros_flash_ft_get_protect_flags(const struct device *dev)
{
	uint32_t flags = 0;
	int rv;
	uint8_t sr;
	unsigned int start, len;
	struct cros_flash_ft_data *data = DRV_DATA(dev);

	/* Check if WP region is protected in status register */
	rv = flash_check_prot_reg(dev, WP_BANK_OFFSET * CONFIG_FLASH_BANK_SIZE,
				  WP_BANK_COUNT * CONFIG_FLASH_BANK_SIZE);
	if (rv == EC_ERROR_ACCESS_DENIED) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
	} else if (rv) {
		return EC_FLASH_PROTECT_ERROR_UNKNOWN;
	}

	/*
	 * If the status register protects a range, but SRP0 is not set,
	 * or Quad Enable (QE) is set,
	 * flags should indicate EC_FLASH_PROTECT_ERROR_INCONSISTENT.
	 */
	flash_get_status(dev, &sr);
	rv = spi_flash_reg_to_protect(sr, 0, &start, &len);
	if (rv) {
		return EC_FLASH_PROTECT_ERROR_UNKNOWN;
	}

	/* Read all-protected state from our shadow copy */
	if (data->all_protected) {
		flags |= EC_FLASH_PROTECT_ALL_NOW;
	}

	return flags;
}

static int cros_flash_ft_protect_at_boot(const struct device *dev,
					 uint32_t new_flags)
{
	struct cros_flash_ft_data *data = DRV_DATA(dev);
	int ret;

	if ((new_flags & (EC_FLASH_PROTECT_RO_AT_BOOT |
			  EC_FLASH_PROTECT_ALL_AT_BOOT)) == 0) {
		/* Clear protection bits in status register */
		return flash_set_status_for_prot(dev, 0);
	}

	if (new_flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
		data->all_protected = 0;
		ret = flash_write_prot_reg(
			dev, 0, CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE,
			1);
	}

	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		data->all_protected = 1;
		ret = flash_write_prot_reg(
			dev, 0, CONFIG_PLATFORM_EC_FLASH_SIZE_BYTES, 1);
	}

	return ret;
}

static int cros_flash_ft_protect_now(const struct device *dev, bool all)
{
	struct cros_flash_ft_data *data = DRV_DATA(dev);
	int ret = EC_SUCCESS;

	if (all) {
		data->all_protected = 1;
		ret = flash_write_prot_reg(
			dev, 0, CONFIG_PLATFORM_EC_FLASH_SIZE_BYTES, 1);
	} else {
		data->all_protected = 0;
		ret = flash_write_prot_reg(
			dev, 0, CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE,
			1);
	}

	return ret;
}

static int cros_flash_ft_get_status(const struct device *dev, uint8_t *sr1,
				    uint8_t *sr2)
{
	flash_get_status(dev, sr1);
	*sr2 = 0;

	return EC_SUCCESS;
}

/* cros ec flash driver registration */
static DEVICE_API(cros_flash, cros_flash_spi_nor_driver_api) = {
	.init = cros_flash_ft_init,
	.physical_write = cros_flash_ft_write,
	.physical_erase = cros_flash_ft_erase,
	.physical_get_protect = cros_flash_ft_get_protect,
	.physical_get_protect_flags = cros_flash_ft_get_protect_flags,
	.physical_protect_at_boot = cros_flash_ft_protect_at_boot,
	.physical_protect_now = cros_flash_ft_protect_now,
	.physical_get_status = cros_flash_ft_get_status,
};

BUILD_ASSERT(CONFIG_FLASH_INIT_PRIORITY <
	     CONFIG_CROS_FLASH_FOCALTECH_INIT_PRIORITY);

static int flash_ft_init(const struct device *dev)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);
	struct cros_flash_ft_data *data = DRV_DATA(dev);

	data->all_protected = 0;
	data->addr_prot_start = 0;
	data->addr_prot_length = 0;

	if (!device_is_ready(cfg->flash_dev)) {
		LOG_ERR("device %s not ready", cfg->flash_dev->name);
		return -ENODEV;
	}

	return EC_SUCCESS;
}

DEVICE_DT_INST_DEFINE(0, flash_ft_init, NULL, &cros_flash_data,
		      &cros_flash_config, POST_KERNEL,
		      CONFIG_CROS_FLASH_FOCALTECH_INIT_PRIORITY,
		      &cros_flash_spi_nor_driver_api);
