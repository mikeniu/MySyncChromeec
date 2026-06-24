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

#define FLASH_PROTECTION_START 0

#define RO_END (CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE)
#define RB_END (CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE)
#define RW_END (CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE)

/*
 * Software-only protection lock used to emulate *_NOW semantics
 * independently from the flash protection range.
 *
 * This lock is not backed by any flash-internal or controller-enforced
 * mechanism. It does not survive reset, sysjump, and must not be
 * interpreted as a hardware security boundary.
 *
 * Its scope and guarantees are limited strictly to this driver.
 */
static bool software_protection_lock = false;

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
	if (!IS_ENABLED(CONFIG_HAS_EM32F967_PRIVATE_FLASH_WP)) {
		return 0;
	}

	/* temporary unlock RW+RB protect at RO*/
	if (!system_is_in_rw()) {
		flash_em32_write_protect_2_disable();
	}

	if (write_protect_is_asserted()) {
		software_protection_lock = true;
	}

	return 0;
}

/**
 * @brief Aggregate and validate flash protection state.
 *
 * This function collects the hardware protection ranges from the
 * EM32F967 private library, validates that they form a consistent,
 * contiguous protection model, and exposes the result as a single
 * protection range.
 *
 * This is the single source of truth for protection state within the
 * cros_flash driver.
 *
 * It performs no policy decisions (e.g. write/erase permission or
 * runtime lock semantics).
 *
 * @param state Output structure describing the aggregated protection state.
 *
 * @return EC_SUCCESS on success,
 *         EC_ERROR_HW_INTERNAL if the hardware protection configuration
 *         is inconsistent or invalid.
 */
static int get_prot_reg(const struct device *dev,
			struct flash_prot_state *state)
{
	struct flash_protect_range ranges[2];
	int nr;

	if (!state)
		return EC_ERROR_INVAL;

	/* Default: no protection */
	state->enabled = false;
	state->start = 0;
	state->end = 0;

	/*
	 * Read hardware protection ranges from the private library.
	 * The library only reports raw hardware state and does not
	 * validate contiguity or policy semantics.
	 */
	nr = flash_em32_get_protection_ranges(ranges);
	if (nr < 0) {
		LOG_ERR("Failed to read flash protection ranges, nr=%d", nr);
		return EC_ERROR_HW_INTERNAL;
	}

	/* No protection configured */
	if (nr == 0)
		return EC_SUCCESS;

	if (nr == 1) {
		/* Single contiguous protection range */
		state->start = ranges[0].start;
		state->end = ranges[0].end;
		state->enabled = true;

		LOG_DBG("protect start=0x%x end=0x%x", state->start,
			state->end);
		return EC_SUCCESS;
	}

	/*
	 * Two ranges configured: must be contiguous.
	 * Any gap indicates an inconsistent protection model.
	 */
	if (ranges[0].end != ranges[1].start) {
		LOG_ERR("Flash write protection is not continuous");
		return EC_ERROR_HW_INTERNAL;
	}

	state->start = ranges[0].start;
	state->end = ranges[1].end;
	state->enabled = true;

	LOG_DBG("protect start=0x%x end=0x%x", state->start, state->end);

	return EC_SUCCESS;
}

/*
 * Check whether the requested write/erase operation overlaps with the current
 * flash protection regions.
 *
 * This helper enforces flash write-protection for data-path operations.
 * It relies on get_prot_reg() to read and validate the hardware protection
 * state, and rejects write/erase requests that target protected ranges or
 * encounter inconsistent hardware protection configuration.
 *
 * Writing to protected flash regions is hardware-dependent and may result in
 * non-deterministic behavior (e.g. silent ignore, partial writes, or
 * inconsistent failures). To keep EC behavior deterministic and
 * policy-controlled, such accesses are explicitly rejected here.
 *
 * Note: This is a driver-level policy check and does not represent a
 * flash-internal or controller-enforced runtime lock.
 */
static int check_prot_reg(const struct device *dev, unsigned int offset,
			  unsigned int bytes)
{
	struct flash_prot_state state;
	uint32_t req_start = offset;
	uint32_t req_end = offset + bytes;
	int ret;

	/*
	 * Note: The request is considered protected if it partially overlaps
	 * with any protected region.
	 */
	ret = get_prot_reg(dev, &state);
	if (ret != EC_SUCCESS)
		return ret;

	if (state.enabled && (req_start < state.end && state.start < req_end))
		return EC_ERROR_ACCESS_DENIED;

	return EC_SUCCESS;
}

static int cros_flash_em32f967_write(const struct device *dev, int offset,
				     int size, const char *src_data)
{
	const struct cros_flash_em32f967_config *cfg = DRV_CONFIG(dev);
	int ret = 0;

	if ((offset < 0) || (size < 0)) {
		return -EINVAL;
	}

	/*
	 * If flash protection library is enabled, enforce protection.
	 * Otherwise, allow write as basic flash functionality.
	 */
	if (IS_ENABLED(CONFIG_HAS_EM32F967_PRIVATE_FLASH_WP)) {
		ret = check_prot_reg(dev, offset, size);
		if (ret != EC_SUCCESS) {
			return ret;
		}
	}

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

	if ((offset < 0) || (size < 0)) {
		return -EINVAL;
	}

	/* address must be aligned to page size */
	if ((offset % CONFIG_FLASH_ERASE_SIZE) != 0)
		return -EINVAL;

	/* Erase size must be a non-zero multiple of page size */
	if ((size == 0) || (size % CONFIG_FLASH_ERASE_SIZE) != 0)
		return -EINVAL;

	/*
	 * If flash protection library is enabled, enforce protection.
	 * Otherwise, allow write as basic flash functionality.
	 */
	if (IS_ENABLED(CONFIG_HAS_EM32F967_PRIVATE_FLASH_WP)) {
		ret = check_prot_reg(dev, offset, size);
		if (ret != EC_SUCCESS) {
			return ret;
		}
	}

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

static int cros_flash_em32f967_get_protect(const struct device *dev, int bank)
{
	uint32_t bank_start, bank_end;

	/*
	 * physical_get_protect() is a boolean predicate indicating whether
	 * a given flash bank is protected by hardware. It returns non-zero
	 * if the bank falls within any active protection range.
	 *
	 * Higher-level semantics such as *_NOW are handled via
	 * driver-level policy (software_protection_lock) and
	 * protection flags
	 */
	if (!IS_ENABLED(CONFIG_HAS_EM32F967_PRIVATE_FLASH_WP))
		return 0;

	/*
	 * physical_get_protect() reports whether a given flash bank
	 * is protected until reboot (i.e. *_NOW semantics).
	 *
	 * On EM32F967, this is true only when:
	 *  - the bank is covered by the protection range, and
	 *  - the driver-level software protection lock is active.
	 */
	if (!software_protection_lock) {
		return 0;
	}

	bank_start = bank * CONFIG_FLASH_BANK_SIZE;
	bank_end = bank_start + CONFIG_FLASH_BANK_SIZE;

	if (check_prot_reg(dev, bank_start, bank_end - bank_start) ==
	    EC_ERROR_ACCESS_DENIED)
		return 1;

	return 0;
}

static uint32_t cros_flash_em32f967_get_protect_flags(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAS_EM32F967_PRIVATE_FLASH_WP)) {
		return EC_ERROR_UNIMPLEMENTED;
	}

	struct flash_prot_state state;
	uint32_t flags = 0;
	uint32_t prot_start, prot_end;
	int ret;

	ret = get_prot_reg(dev, &state);
	if (ret != EC_SUCCESS) {
		/*
		 * Hardware protection state is inconsistent or invalid.
		 * Report this via protection flags instead of failing the call.
		 */
		flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		goto exit_flash;
	}
	if (!state.enabled) {
		goto exit_flash;
	}

	prot_start = state.start;
	prot_end = state.end;

	/* Check unexpected state. */
	if (prot_start != FLASH_PROTECTION_START) {
		/* Treat this as inconsistent error, because it can be fixed by
		 * re-applying protection flags */
		flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		goto exit_flash;
	}

	/* Check if ranges fully overlap. This logic assumes a certain flash
	 * layout: RO -> ROLLBACKS -> RW. */
	if (prot_end >= RW_END) {
		flags |= EC_FLASH_PROTECT_ALL_AT_BOOT |
			 EC_FLASH_PROTECT_RO_AT_BOOT;
#ifdef CONFIG_ROLLBACK
		flags |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
	} else if (prot_end >= RB_END) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT |
			 EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif /* CONFIG_ROLLBACK */
	} else if (prot_end >= RO_END) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
	}

	/*
	 * *_NOW flags reflect a driver-level runtime policy.
	 *
	 * On EM32F967, *_NOW is derived exclusively from
	 * software_protection_lock, which is a software-only mechanism used to
	 * prevent lowering flash protection once enabled. It is not tied to any
	 * flash-internal or controller-enforced runtime lock.
	 */
	if (software_protection_lock) {
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
	if (!IS_ENABLED(CONFIG_HAS_EM32F967_PRIVATE_FLASH_WP)) {
		return EC_ERROR_UNIMPLEMENTED;
	}

	struct flash_prot_state state;
	uint32_t new_prot_end;
	uint32_t curr_prot_start;
	uint32_t curr_prot_end;
	int ret = EC_SUCCESS;

	/* There is no independent protection of each section. Protection of WP
	 * is within protection ranges of Rollbacks */
	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		new_prot_end = RW_END;
	} else if (new_flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
		new_prot_end = RB_END;
	} else if (new_flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
		new_prot_end = RO_END;
	} else {
		new_prot_end = FLASH_PROTECTION_START;
	}

	unsigned int key = irq_lock();

	ret = get_prot_reg(dev, &state);
	if (ret != EC_SUCCESS) {
		/* Hardware protection state is inconsistent or invalid */
		goto unlock_irq;
	}
	if (state.enabled) {
		curr_prot_start = state.start;
		curr_prot_end = state.end;
	} else {
		curr_prot_start = FLASH_PROTECTION_START;
		curr_prot_end = FLASH_PROTECTION_START;
	}

	/* Just exit if the protection range matches new values. */
	if ((curr_prot_start == FLASH_PROTECTION_START) &&
	    (curr_prot_end == new_prot_end)) {
		goto unlock_irq;
	}

	/*
	 * When software_protection_lock is set, flash protection is considered
	 * locked at the driver-policy level. In this state:
	 *  - extending the protection range is allowed
	 *  - reducing or removing protection is rejected
	 */
	if (software_protection_lock) {
		/* In case of WP asserted, allow changing protection range
		 * only if it is extending the range. */
		if (write_protect_is_asserted()) {
			if (new_prot_end < curr_prot_end) {
				ret = EC_ERROR_ACCESS_DENIED;
				goto unlock_irq;
			}
		}
	}

	if (new_prot_end == FLASH_PROTECTION_START) {
		/* No protection */
		flash_em32_write_protect_1_disable();
		flash_em32_write_protect_2_disable();
	} else if (new_prot_end == RO_END) {
		/* Protect RO only */
		flash_em32_write_protect_2_disable();
		flash_em32_write_protect_1_range(FLASH_PROTECTION_START,
						 RO_END);
	} else if (new_prot_end == RB_END || new_prot_end == RW_END) {
		/* Protect RO + [RB|RW] */
		flash_em32_write_protect_1_range(FLASH_PROTECTION_START,
						 RO_END);
		flash_em32_write_protect_2_range(RO_END, new_prot_end);
	} else {
		/* Defensive programming */
		ret = EC_ERROR_INVAL;
	}

unlock_irq:
	irq_unlock(key);
	return ret;
}

static int cros_flash_em32f967_protect_now(const struct device *dev, bool all)
{
	if (!IS_ENABLED(CONFIG_HAS_EM32F967_PRIVATE_FLASH_WP)) {
		return EC_ERROR_UNIMPLEMENTED;
	}

	uint32_t new_flags;
	int ret;

	LOG_DBG("cros_flash_em32f967_protect_now all=%s.",
		all ? "true" : "false");

	if (all) {
		new_flags = EC_FLASH_PROTECT_ALL_AT_BOOT;
	} else {
		new_flags = EC_FLASH_PROTECT_RO_AT_BOOT;
	}

	/*
	 * Enforce the requested protection range immediately.
	 * protect_at_boot() is responsible for setting the protection ranges.
	 */
	ret = cros_flash_em32f967_protect_at_boot(dev, new_flags);
	if (!ret) {
		software_protection_lock = true;
	}
	return ret;
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

	if (!device_is_ready(cfg->flash_dev)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->flash_dev);
		return -ENODEV;
	}

	return EC_SUCCESS;
}

BUILD_ASSERT(CONFIG_FLASH_INIT_PRIORITY < CONFIG_CROS_FLASH_INIT_PRIORITY);

DEVICE_DT_INST_DEFINE(0, flash_em32f967_init, NULL, NULL, &cros_flash_config,
		      POST_KERNEL, CONFIG_CROS_FLASH_INIT_PRIORITY,
		      &cros_flash_em32f967_driver_api);
