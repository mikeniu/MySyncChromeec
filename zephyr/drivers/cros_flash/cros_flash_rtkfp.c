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

/* The flash protection for spi nor chips goes from bottom to top.
 * The EC application uses the from bottom to top scheme for
 * RO -> [RB ->] -> RW protection.
 */
#define FLASH_PROTECTION_START 0

struct cros_flash_rtkfp_data {
	int all_protected;
	int sw_protected;
	int addr_prot_start;
	int addr_prot_length;
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

static int cros_flash_rtkfp_set_status_reg(const struct device *dev, uint8_t *reg)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);

	if (cfg == NULL) {
		return -EINVAL;
	}

	return flash_ex_op(cfg->flash_dev, FLASH_RTS5817_EX_OP_WR_SR,
			   (uintptr_t)reg, NULL);
}

static void flash_get_status(const struct device *dev, uint8_t *sr1,
			     uint8_t *sr2)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	uint8_t sr[2];

	crec_flash_lock_mapped_storage(1);

	/* Read status register */
	flash_ex_op(cfg->flash_dev, FLASH_RTS5817_EX_OP_RD_SR, (uintptr_t)NULL,
		    sr);

	*sr1 = sr[0];
	*sr2 = sr[1];

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);
}

static int flash_set_status(const struct device *dev, uint8_t sr1, uint8_t sr2)
{
	int rv;
	uint8_t regs[2] = { sr1, sr2 };

	crec_flash_lock_mapped_storage(1);
	rv = cros_flash_rtkfp_set_status_reg(dev, regs);
	crec_flash_lock_mapped_storage(0);

	return rv;
}

static int flash_set_status_for_prot(const struct device *dev, int reg1,
				     int reg2)
{
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);
	uint8_t sr1, sr2;
	int rv;

	data->all_protected = 0;

	flash_get_status(dev, &sr1, &sr2);

	sr1 &= ~(SPI_FLASH_SR1_BP0 | SPI_FLASH_SR1_BP1 | SPI_FLASH_SR1_BP2 |
		 SPI_FLASH_SR1_TB | SPI_FLASH_SR1_SEC);
	sr2 &= ~SPI_FLASH_SR2_CMP;

	sr1 |= reg1;
	sr2 |= reg2;

	rv = flash_set_status(dev, sr1, sr2);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	rv = spi_flash_reg_to_protect(sr1, sr2, &data->addr_prot_start,
				      &data->addr_prot_length);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	return EC_SUCCESS;
}

static int flash_check_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes)
{
	unsigned int start;
	unsigned int len;
	uint8_t sr1, sr2;
	int rv = EC_SUCCESS;

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES) {
		return EC_ERROR_INVAL;
	}

	/* Compute current protect range */
	flash_get_status(dev, &sr1, &sr2);

	rv = spi_flash_reg_to_protect(sr1, sr2, &start, &len);

	if (rv) {
		return rv;
	}

	/* Check if ranges overlap */
	if (MAX(start, offset) < MIN(start + len, offset + bytes)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

/* set new protect range by writing status regs */
static int flash_write_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes)
{
	uint8_t sr1, sr2;
	int rv;

	/* Invalid values */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;

	/* Compute desired protect range */
	rv = spi_flash_protect_to_reg(offset, bytes, &sr1, &sr2);
	if (rv) {
		return rv;
	}

	return flash_set_status_for_prot(dev, sr1, sr2);
}

static int flash_check_prot_range(const struct device *dev, unsigned int offset,
				  unsigned int bytes)
{
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES) {
		return EC_ERROR_INVAL;
	}

	if (MAX(data->addr_prot_start, offset) <
	    MIN(data->addr_prot_start + data->addr_prot_length,
		offset + bytes)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

static int cros_flash_rtkfp_write(const struct device *dev, int offset,
				  int size, const char *data)
{
	struct cros_flash_rtkfp_data *dev_data = DRV_DATA(dev);
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	int ret;

	/* check protection */
	if (dev_data->all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (flash_check_prot_range(dev, offset, size)) {
		return EC_ERROR_ACCESS_DENIED;
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
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	int ret = 0;

	if (data->all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (flash_check_prot_range(dev, offset, size)) {
		return EC_ERROR_ACCESS_DENIED;
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

static int flash_get_prot_range(const struct device *dev, uint32_t *start,
				uint32_t *end)
{
	unsigned int len;
	uint8_t sr1, sr2;
	int ret;

	flash_get_status(dev, &sr1, &sr2);
	ret = spi_flash_reg_to_protect(sr1, sr2, start, &len);
	if (ret) {
		return ret;
	}
	*end = *start + len;

	return EC_SUCCESS;
}

static int cros_flash_rtkfp_get_protect(const struct device *dev, int bank)
{
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);
	unsigned int addr_start = bank * CONFIG_FLASH_BANK_SIZE;

	/*
	 * The physical_get_protect returns protection state until reboot,
	 * so it is used to determine *_NOW flags.
	 *
	 * Make sure flash protection is enabled and software protection is
	 * enabled.
	 */
	if (!data->sw_protected && (addr_start >= CONFIG_WP_STORAGE_OFF)) {
		return 0;
	}

	if (flash_check_prot_reg(dev, addr_start, CONFIG_FLASH_BANK_SIZE) ==
	    EC_ERROR_ACCESS_DENIED) {
		return 1;
	}

	return 0;
}

static uint32_t cros_flash_rtkfp_get_protect_flags(const struct device *dev)
{
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);
	uint32_t flags = 0;
	uint8_t sr1, sr2;
	unsigned int prot_start;
	unsigned int prot_end;
	unsigned int prot_len;
	int ret;

	flash_get_status(dev, &sr1, &sr2);

	/*
	 * Make sure SRP bits are cleared - only software write protection.
	 * The HW WP pin is not used.
	 */
	if ((sr1 & SPI_FLASH_SR1_SRP0) || (sr2 & SPI_FLASH_SR2_SRP1)) {
		flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		return flags;
	}

	ret = spi_flash_reg_to_protect(sr1, sr2, &prot_start, &prot_len);
	if (ret) {
		return EC_FLASH_PROTECT_ERROR_UNKNOWN;
	}
	prot_end = prot_start + prot_len;

	/* Check unexpected state. */
	if (prot_start != FLASH_PROTECTION_START) {
		flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		return flags;
	}

	/* Check if ranges fully overlap. This logic assumes a certain flash
	 * layout: RO -> ROLLBACKS -> RW.
	 */
	if (prot_end >= (CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE)) {
		flags |= EC_FLASH_PROTECT_ALL_AT_BOOT |
			 EC_FLASH_PROTECT_RO_AT_BOOT;
#ifdef CONFIG_ROLLBACK
		flags |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
	} else if (prot_end >= (CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE)) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT |
			 EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif /* CONFIG_ROLLBACK */
	} else if (prot_end >=
		   (CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE)) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
	}

	/* Software protection is enabled. Add proper _NOW flags. */
	if (data->sw_protected) {
		if (flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
			flags |= EC_FLASH_PROTECT_ALL_NOW |
				 EC_FLASH_PROTECT_RO_NOW;
#ifdef CONFIG_ROLLBACK
			flags |= EC_FLASH_PROTECT_ROLLBACK_NOW;
		} else if (flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
			flags |= EC_FLASH_PROTECT_ROLLBACK_NOW |
				 EC_FLASH_PROTECT_RO_NOW;
#endif /* CONFIG_ROLLBACK */
		} else if (flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
			flags |= EC_FLASH_PROTECT_RO_NOW;
		}
	}

	/* Read all-protected state from our shadow copy */
	if (data->all_protected) {
		flags |= EC_FLASH_PROTECT_ALL_NOW;
	}

	return flags;
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

static int cros_flash_rtkfp_protect_at_boot(const struct device *dev,
					    uint32_t new_flags)
{
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);
	int ret;
	bool was_sw_protected;
	uint32_t new_prot_end;
	uint32_t curr_prot_start;
	uint32_t curr_prot_end;

	was_sw_protected = data->sw_protected;

	/* There is no independent protection of each section. Protection of WP
	 * is within protection ranges of Rollbacks */
	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		new_prot_end = CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE;
#ifdef CONFIG_ROLLBACK
	} else if (new_flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
		new_prot_end = CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE;
#endif /* CONFIG_ROLLBACK */
	} else if (new_flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
		new_prot_end = CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE;
	} else {
		/* Disable protection of WP section, but do not disable
		 * protection of the flash header. */
		new_prot_end = CONFIG_WP_STORAGE_OFF;
	}

	ret = flash_get_prot_range(dev, &curr_prot_start, &curr_prot_end);
	if (ret) {
		return ret;
	}

	/* Just exit if the protection range matches new values. */
	if ((curr_prot_start == FLASH_PROTECTION_START) &&
	    (curr_prot_end == new_prot_end)) {
		return EC_SUCCESS;
	}

	if (was_sw_protected) {
		/* Allow modifying status registers in case deasserted WP, but
		 * restore the SW protect in case WP is asserted again. */
		if (write_protect_is_asserted()) {
			/* In case of SW protect enabled and WP asserted,
			 * allow changing protection range only if it is
			 * extending the range. */
			if (new_prot_end < curr_prot_end) {
				return EC_ERROR_ACCESS_DENIED;
			}
		}
		ret = cros_flash_rtkfp_sw_protect(dev, false);
		if (ret) {
			return ret;
		}
		data->sw_protected = 0;
	}

	ret = flash_write_prot_reg(dev, FLASH_PROTECTION_START, new_prot_end);

	/* Always re-enable SW protect if it was enabled before. */
	if (was_sw_protected) {
		if (cros_flash_rtkfp_sw_protect(dev, true)) {
			LOG_ERR("Failed to restore SW protect");
			/* Panic due to security related issue. Flash protection
			 * state is unknown. */
			k_panic();
		}
		data->sw_protected = 1;
	}

	if (!ret) {
		LOG_DBG("set AT_BOOT");
	}
	return ret;
}

static int cros_flash_rtkfp_protect_now(const struct device *dev, bool all)
{
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);
	int ret;

	/* Set flash protection and enable SW protect. */
	if (all) {
		ret = cros_flash_rtkfp_protect_at_boot(
			dev, EC_FLASH_PROTECT_ALL_AT_BOOT);
		data->all_protected = 1;
	} else {
		ret = cros_flash_rtkfp_protect_at_boot(
			dev, EC_FLASH_PROTECT_RO_AT_BOOT);
	}

	if (!ret) {
		ret = cros_flash_rtkfp_sw_protect(dev, true);
		if (!ret) {
			data->sw_protected = 1;
		}
	}

	return ret;
}

static int cros_flash_rtkfp_get_status(const struct device *dev,
				       uint8_t *sr1, uint8_t *sr2)
{
	flash_get_status(dev, sr1, sr2);

	return EC_SUCCESS;
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
	flash_get_status(dev, &sr1, &sr2);
	sr1 &= ~SPI_FLASH_SR1_SRP0;
	sr2 &= ~SPI_FLASH_SR2_SRP1;

	flash_set_status(dev, sr1, sr2);

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
	.physical_get_protect = cros_flash_rtkfp_get_protect,
	.physical_get_protect_flags = cros_flash_rtkfp_get_protect_flags,
	.physical_protect_at_boot = cros_flash_rtkfp_protect_at_boot,
	.physical_protect_now = cros_flash_rtkfp_protect_now,
	.physical_get_status = cros_flash_rtkfp_get_status,
};

BUILD_ASSERT(CONFIG_FLASH_INIT_PRIORITY < CONFIG_CROS_FLASH_INIT_PRIORITY);

static int flash_rtkfp_init(const struct device *dev)
{
	const struct cros_flash_rtkfp_config *cfg = DRV_CONFIG(dev);
	struct cros_flash_rtkfp_data *data = DRV_DATA(dev);

	data->all_protected = 0;
	data->sw_protected = 0;
	data->addr_prot_start = 0;
	data->addr_prot_length = 0;

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
