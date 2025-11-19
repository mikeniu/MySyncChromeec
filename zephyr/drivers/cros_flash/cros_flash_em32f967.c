/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT elan_em32f967_cros_flash

#include "cros_flash_em32f967_wp.h"
#include "flash.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_flash.h>

LOG_MODULE_REGISTER(cros_flash);

#if !DT_NODE_EXISTS(DT_CHOSEN(zephyr_flash_controller))
#error "No suitable devicetree overlay specified for zephyr_flash_controller"
#endif

/* Device Configuration */
struct cros_flash_em32f967_config {
	const struct device *flash_dev;
};
#define DRV_CONFIG(dev) \
	((const struct cros_flash_em32f967_config *)(dev)->config)

#define FLASH_DEV DT_CHOSEN(zephyr_flash_controller)
static const struct cros_flash_em32f967_config cros_flash_config = {
	.flash_dev = DEVICE_DT_GET(FLASH_DEV),
};

/* cros ec flash api functions */
static int cros_flash_em32f967_init(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_CROS_FLASH_WP_LIBRARY_EM32F967)) {
		return 0;
	}

	/* temporary unlock RW+RB portect at RO*/
	if (!system_is_in_rw()) {
		flash_em32_write_protect_2_disable();
	}

	return 0;
}

static int cros_flash_em32f967_get_protect(const struct device *dev, int bank)
{
	if (!IS_ENABLED(CONFIG_CROS_FLASH_WP_LIBRARY_EM32F967)) {
		return 0;
	}

	bool protected = false;
	protected = flash_em32_check_bank_protected(bank);

	return protected ? 1 : 0;
}

static int cros_flash_em32f967_write(const struct device *dev, int offset,
				     int size, const char *src_data)
{
	const struct cros_flash_em32f967_config *cfg = DRV_CONFIG(dev);
	int ret = 0;

	LOG_DBG("cros_flash_em32f967_write.");

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	LOG_DBG("cros_flash_em32f967_write 0x%x 0x%x.", offset, size);
	ret = flash_write(cfg->flash_dev, offset, src_data, size);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

static int cros_flash_em32f967_erase(const struct device *dev, int offset,
				     int size)
{
	const struct cros_flash_em32f967_config *cfg = DRV_CONFIG(dev);
	int ret = 0;

	LOG_DBG("cros_flash_em32f967_erase.");

	/* address must be aligned to page size */
	if ((offset % CONFIG_FLASH_ERASE_SIZE) != 0)
		return -EINVAL;

	/* Erase size must be a non-zero multiple of page size */
	if ((size == 0) || (size % CONFIG_FLASH_ERASE_SIZE) != 0)
		return -EINVAL;

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	/* Always use page erase command */
	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE) {
		LOG_DBG("cros_flash_em32f967_erase 0x%x 0x%x.", offset,
			CONFIG_FLASH_ERASE_SIZE);
		ret = flash_erase(cfg->flash_dev, offset,
				  CONFIG_FLASH_ERASE_SIZE);
		if (ret) {
			break;
		}

		offset += CONFIG_FLASH_ERASE_SIZE;
	}

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

static int get_prot_reg(const struct device *dev, uint32_t *start,
			uint32_t *end)
{
	int bank_count = CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE;
	int first = -1;
	int last = -1;
	int i;

	for (i = 0; i < bank_count; i++) {
		if (cros_flash_em32f967_get_protect(dev, i)) {
			if (first < 0) {
				first = i;
			}
			last = i;
		} else if (first >= 0) {
			break;
		}
	}

	if (first < 0) {
		*start = 0;
		*end = 0;
		return 0;
	}

	*start = first * CONFIG_FLASH_BANK_SIZE;
	*end = (last + 1) * CONFIG_FLASH_BANK_SIZE;
	LOG_INF("protect start=0x%x end=0x%x", *start, *end);

	return 0;
}

static uint32_t cros_flash_em32f967_get_protect_flags(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_CROS_FLASH_WP_LIBRARY_EM32F967)) {
		return 0;
	}

	uint32_t flags = 0;
	uint32_t prot_start, prot_end;

	get_prot_reg(dev, &prot_start, &prot_end);

	/* Check unexpected state. */
	if (prot_start != CONFIG_WP_STORAGE_OFF) {
		/* Treat this as inconsistent error, because it can be fixed by
		 * re-applying protection flags */
		flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		goto exit_flash;
	}

	/* Check if ranges fully overlap. This logic assumes a certain flash
	 * layout: RO -> ROLLBACKS -> RW. */
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

	/* Write Protect is asserted. Add proper _NOW flags. */
	if (write_protect_is_asserted()) {
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

exit_flash:
	LOG_DBG("cros_flash_em32f967_get_protect_flags flags=0x%x.", flags);
	return flags;
}

static int cros_flash_em32f967_protect_at_boot(const struct device *dev,
					       uint32_t new_flags)
{
	if (!IS_ENABLED(CONFIG_CROS_FLASH_WP_LIBRARY_EM32F967)) {
		return 0;
	}

	LOG_INF("cros_flash_em32f967_protect_at_boot.");

	uint32_t new_prot_end;
	uint32_t curr_prot_start;
	uint32_t curr_prot_end;
	int ret = EC_SUCCESS;

	/* There is no independent protection of each section. Protection of WP
	 * is within protection ranges of Rollbacks */
	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		new_prot_end = CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE;
	} else if (new_flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
		new_prot_end = CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE;
	} else if (new_flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
		new_prot_end = CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE;
	} else {
		/* Disable protection of WP section, but do not disable
		 * protection of the flash header. */
		new_prot_end = CONFIG_WP_STORAGE_OFF;
	}

	get_prot_reg(dev, &curr_prot_start, &curr_prot_end);

	/* Check unexpected state. */
	if (curr_prot_start != CONFIG_WP_STORAGE_OFF) {
		/* Treat this as inconsistent error, because it can be fixed by
		 * re-applying protection flags */
		LOG_ERR("start is not CONFIG_WP_STORAGE_OFF");
		ret = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		goto exit_flash;
	}

	/* Just exit if the protection range matches new values. */
	if ((curr_prot_start == CONFIG_WP_STORAGE_OFF) &&
	    (curr_prot_end == new_prot_end)) {
		goto exit_flash;
	}

	if (write_protect_is_asserted()) {
		/* In case of  WP asserted, allow changing protection range
		 * only if it is extending the range. */
		if (new_prot_end < curr_prot_end) {
			LOG_INF("Avoid reduce when write protect is asserted");
			ret = EC_ERROR_ACCESS_DENIED;
			goto exit_flash;
		}
	}

	uint32_t offset = 0x0;
	int bank_count = 0x0;

	if ((new_flags & (EC_FLASH_PROTECT_RO_AT_BOOT |
			  EC_FLASH_PROTECT_ALL_AT_BOOT)) == 0) {
		/* Clear protection bits in status register */
		LOG_INF("cros_flash_em32f967_protect_at_boot clear all.");
		flash_em32_write_protect_1_disable();
		flash_em32_write_protect_2_disable();
	}

	if (new_flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
		LOG_INF("cros_flash_em32f967_protect_at_boot protect RO.");
		flash_em32_write_protect_2_disable();
		flash_em32_write_protect_1(WP_BANK_OFFSET, WP_BANK_COUNT);
	}

	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		LOG_INF("cros_flash_em32f967_protect_at_boot protect all.");
		offset = CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE;
		bank_count =
			(CONFIG_FLASH_SIZE_BYTES - CONFIG_WP_STORAGE_SIZE) /
			CONFIG_FLASH_BANK_SIZE;
		LOG_INF("cros_flash_em32f967_protect_at_boot offset=0x%x bank_count=0x%x.",
			offset, bank_count);
		flash_em32_write_protect_2(offset, bank_count);
	}

exit_flash:
	return ret;
}

static int cros_flash_em32f967_protect_now(const struct device *dev, bool all)
{
	if (!IS_ENABLED(CONFIG_CROS_FLASH_WP_LIBRARY_EM32F967)) {
		return 0;
	}

	LOG_DBG("cros_flash_em32f967_protect_now all=%s.",
		all ? "true" : "false");
	if (!all) {
		flash_em32_write_protect_2_disable();
	}
	return 0;
}

/* cros ec flash driver registration */
static DEVICE_API(cros_flash, cros_flash_em32f967_driver_api) = {
	.init = cros_flash_em32f967_init,
	.physical_write = cros_flash_em32f967_write,
	.physical_erase = cros_flash_em32f967_erase,
	.physical_get_protect = cros_flash_em32f967_get_protect,
	.physical_get_protect_flags = cros_flash_em32f967_get_protect_flags,
	.physical_protect_at_boot = cros_flash_em32f967_protect_at_boot,
	.physical_protect_now = cros_flash_em32f967_protect_now,
};

static int flash_em32f967_init(const struct device *dev)
{
	const struct cros_flash_em32f967_config *cfg = DRV_CONFIG(dev);

	LOG_DBG("flash_em32f967_init.");

	if (!device_is_ready(cfg->flash_dev)) {
		LOG_ERR("Flash device %s not ready", cfg->flash_dev->name);
		return -ENODEV;
	}

	return EC_SUCCESS;
}

BUILD_ASSERT(CONFIG_FLASH_INIT_PRIORITY < CONFIG_CROS_FLASH_INIT_PRIORITY);

DEVICE_DT_INST_DEFINE(0, flash_em32f967_init, NULL, NULL, &cros_flash_config,
		      POST_KERNEL, CONFIG_CROS_FLASH_INIT_PRIORITY,
		      &cros_flash_em32f967_driver_api);
