/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cros/reg_audit.h"
#include "hooks.h"
#include "system.h"
#include "watchdog.h"

#include <string.h>

#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <strings.h>

#if defined(CONFIG_CPU_CORTEX_M)
#include <cmsis_core.h>
#endif

/* Maximum number of scalar register elements stored per snapshot */
#ifndef CONFIG_PLATFORM_EC_REG_AUDIT_MAX_REGS
#define CONFIG_PLATFORM_EC_REG_AUDIT_MAX_REGS 4096
#endif

#define MAX_MODULES 128

struct reg_audit_snapshot {
	bool valid;
	bool jumped;
	uint32_t reset_flags;
	uint32_t total_regs;
	/* Bitmask of which modules had their clocks enabled at snapshot time */
	uint32_t mod_clocked[DIV_ROUND_UP(MAX_MODULES, 32)];
	uint32_t values[CONFIG_PLATFORM_EC_REG_AUDIT_MAX_REGS];
};

static struct reg_audit_snapshot snap_pre_init;
static struct reg_audit_snapshot snap_post_init;

/**
 * Perform a fault-safe register read of 1, 2, or 4 bytes at addr.
 * On ARM Cortex-M3/M4/M33, uses BFHFNMIGN + FAULTMASK=1 to ignore precise
 * data BusFaults and detect them via SCB->CFSR.
 */
static int reg_audit_safe_read(uintptr_t addr, uint8_t size, uint32_t *out_val)
{
	uint32_t val = 0;

#if defined(CONFIG_CPU_CORTEX_M) && defined(SCB_CCR_BFHFNMIGN_Msk)
	uint32_t old_faultmask = __get_FAULTMASK();
	uint32_t old_ccr = SCB->CCR;

	__set_FAULTMASK(1);
	SCB->CCR = old_ccr | SCB_CCR_BFHFNMIGN_Msk;
	/* Clear any sticky BusFault flags prior to probing */
	SCB->CFSR = SCB_CFSR_BUSFAULTSR_Msk;
	__DSB();
	__ISB();
#endif

	switch (size) {
	case 1:
		val = *(volatile const uint8_t *)addr;
		break;
	case 2:
		val = *(volatile const uint16_t *)addr;
		break;
	case 4:
		val = *(volatile const uint32_t *)addr;
		break;
	default:
		val = REG_AUDIT_VAL_SKIPPED;
		break;
	}

#if defined(CONFIG_CPU_CORTEX_M) && defined(SCB_CCR_BFHFNMIGN_Msk)
	__DSB();
	__ISB();
	bool faulted = (SCB->CFSR & SCB_CFSR_BUSFAULTSR_Msk) != 0;

	if (faulted) {
		SCB->CFSR = SCB_CFSR_BUSFAULTSR_Msk;
	}
	SCB->CCR = old_ccr;
	__DSB();
	__ISB();
	__set_FAULTMASK(old_faultmask);

	if (faulted) {
		*out_val = REG_AUDIT_VAL_FAULT;
		return -EFAULT;
	}
#endif

	*out_val = val;
	return 0;
}

static void reg_audit_capture_snapshot(struct reg_audit_snapshot *snap)
{
	size_t num_mods = 0;
	const struct reg_module_inst *mods =
		reg_audit_chip_get_modules(&num_mods);
	uint32_t idx = 0;

	memset(snap, 0, sizeof(*snap));

	for (size_t m = 0; m < num_mods && m < MAX_MODULES; m++) {
		const struct reg_module_inst *mod = &mods[m];
		const struct reg_block_type *type = mod->type;
		unsigned int key = irq_lock();
		int was_clocked = 1;
		uint32_t saved_state = 0;
		int rc = reg_audit_chip_module_enter(mod, &was_clocked,
						     &saved_state);

		if (was_clocked) {
			snap->mod_clocked[m / 32] |= BIT(m % 32);
		}

		for (uint16_t f = 0; f < type->num_fields; f++) {
			const struct reg_field_desc *fd = &type->fields[f];
			uint16_t count = fd->count ? fd->count : 1;
			uint8_t stride = fd->stride ? fd->stride : fd->size;

			for (uint16_t i = 0; i < count; i++) {
				if (idx >=
				    CONFIG_PLATFORM_EC_REG_AUDIT_MAX_REGS) {
					break;
				}
				if (rc != 0 ||
				    (fd->flags & (REG_FLAG_SIDE_EFFECT |
						  REG_FLAG_WRITE_ONLY))) {
					snap->values[idx++] =
						REG_AUDIT_VAL_SKIPPED;
				} else {
					uintptr_t addr = mod->base +
							 fd->offset +
							 (uintptr_t)i * stride;
					uint32_t val = 0;

					reg_audit_safe_read(addr, fd->size,
							    &val);
					snap->values[idx++] = val;
				}
			}
		}

		if (rc == 0) {
			reg_audit_chip_module_exit(mod, saved_state);
		}
		irq_unlock(key);
	}

	snap->total_regs = idx;
	snap->valid = true;
}

static int reg_audit_pre_init(void)
{
	reg_audit_capture_snapshot(&snap_pre_init);
	return 0;
}
/* Run at PRE_KERNEL_1 priority 0 before any driver or EC shim init */
SYS_INIT(reg_audit_pre_init, PRE_KERNEL_1, 0);

static void reg_audit_post_init(void)
{
	snap_pre_init.jumped = system_jumped_to_this_image();
	snap_pre_init.reset_flags = system_get_reset_flags();

	reg_audit_capture_snapshot(&snap_post_init);
	snap_post_init.jumped = system_jumped_to_this_image();
	snap_post_init.reset_flags = system_get_reset_flags();
}
DECLARE_HOOK(HOOK_INIT, reg_audit_post_init, HOOK_PRIO_LAST);

static void format_flags_str(uint8_t flags, char *buf, size_t len)
{
	size_t pos = 0;

	buf[0] = '\0';
	if (flags & REG_FLAG_VOLATILE) {
		pos += snprintk(buf + pos, len - pos, "V");
	}
	if (flags & REG_FLAG_SIDE_EFFECT) {
		pos += snprintk(buf + pos, len - pos, "S");
	}
	if (flags & REG_FLAG_WRITE_ONLY) {
		pos += snprintk(buf + pos, len - pos, "W");
	}
	if (pos == 0) {
		snprintk(buf, len, "-");
	}
}

static void format_reg_val(uint32_t val, uint8_t size, char *buf, size_t len)
{
	if (val == REG_AUDIT_VAL_SKIPPED) {
		snprintk(buf, len, "SKIP");
	} else if (val == REG_AUDIT_VAL_FAULT) {
		snprintk(buf, len, "FAULT");
	} else {
		snprintk(buf, len, "0x%0*x", size * 2, val);
	}
}

static void reg_audit_print_module(const struct reg_module_inst *mod,
				   size_t mod_idx,
				   const struct reg_audit_snapshot *snap,
				   uint32_t *snap_idx, const char *stage_tag)
{
	const struct reg_block_type *type = mod->type;
	int was_clocked = 1;
	uint32_t saved_state = 0;
	unsigned int key = 0;
	int rc = 0;

	if (snap != NULL) {
		was_clocked =
			(snap->mod_clocked[mod_idx / 32] & BIT(mod_idx % 32)) ?
				1 :
				0;
	} else {
		key = irq_lock();
		rc = reg_audit_chip_module_enter(mod, &was_clocked,
						 &saved_state);
		if (rc == 0) {
			reg_audit_chip_module_exit(mod, saved_state);
		}
		irq_unlock(key);
	}

	printk("MOD|%s|%s|%s|0x%08lx|clk=%d\n", stage_tag, mod->name,
	       type->type_name, (unsigned long)mod->base, was_clocked);

	uint32_t line_cnt = 0;

	for (uint16_t f = 0; f < type->num_fields; f++) {
		const struct reg_field_desc *fd = &type->fields[f];
		uint16_t count = fd->count ? fd->count : 1;
		uint8_t stride = fd->stride ? fd->stride : fd->size;
		char fstr[8];

		format_flags_str(fd->flags, fstr, sizeof(fstr));

		for (uint16_t i = 0; i < count; i++) {
			uintptr_t offset = fd->offset + (uintptr_t)i * stride;
			uintptr_t addr = mod->base + offset;
			uint32_t val = REG_AUDIT_VAL_SKIPPED;
			char vstr[16];

			if (snap != NULL) {
				if (*snap_idx < snap->total_regs) {
					val = snap->values[(*snap_idx)++];
				}
			} else {
				if (!(fd->flags & (REG_FLAG_SIDE_EFFECT |
						   REG_FLAG_WRITE_ONLY))) {
					key = irq_lock();
					int unused_clk = 0;

					rc = reg_audit_chip_module_enter(
						mod, &unused_clk, &saved_state);
					if (rc == 0) {
						reg_audit_safe_read(
							addr, fd->size, &val);
						reg_audit_chip_module_exit(
							mod, saved_state);
					}
					irq_unlock(key);
				}
			}

			format_reg_val(val, fd->size, vstr, sizeof(vstr));

			if (count > 1) {
				printk("REG|%s|%s|%s[%u]|0x%04x|%u|%s|%s\n",
				       stage_tag, mod->name, fd->name, i,
				       (unsigned int)offset, fd->size, vstr,
				       fstr);
			} else {
				printk("REG|%s|%s|%s|0x%04x|%u|%s|%s\n",
				       stage_tag, mod->name, fd->name,
				       (unsigned int)offset, fd->size, vstr,
				       fstr);
			}

			if (++line_cnt % 16 == 0) {
				cflush();
				watchdog_reload();
				k_msleep(2);
			}
		}
	}
	cflush();
	watchdog_reload();
}

static void reg_audit_dump(const struct reg_audit_snapshot *snap,
			   const char *stage_tag, const char *filter_mod)
{
	size_t num_mods = 0;
	const struct reg_module_inst *mods =
		reg_audit_chip_get_modules(&num_mods);
	uint32_t snap_idx = 0;

	printk("REGDUMP_BEGIN|stage=%s|img=%s|jumped=%d|reset=0x%08x|regs=%u\n",
	       stage_tag, system_get_image_copy_string(),
	       system_jumped_to_this_image(), system_get_reset_flags(),
	       snap ? snap->total_regs : 0);
	cflush();

	if (snap == NULL && filter_mod == NULL) {
		reg_audit_chip_dump_core_regs(stage_tag);
	}

	for (size_t m = 0; m < num_mods && m < MAX_MODULES; m++) {
		const struct reg_module_inst *mod = &mods[m];
		const struct reg_block_type *type = mod->type;

		if (filter_mod != NULL &&
		    strncasecmp(mod->name, filter_mod, 32) != 0 &&
		    strncasecmp(type->type_name, filter_mod, 32) != 0) {
			if (snap != NULL) {
				for (uint16_t f = 0; f < type->num_fields;
				     f++) {
					uint16_t count =
						type->fields[f].count ?
							type->fields[f].count :
							1;
					snap_idx += count;
				}
			}
			continue;
		}

		reg_audit_print_module(mod, m, snap, &snap_idx, stage_tag);
	}

	printk("REGDUMP_END|stage=%s\n", stage_tag);
	cflush();
}

static void reg_audit_diff_pre_post(const char *filter_mod)
{
	size_t num_mods = 0;
	const struct reg_module_inst *mods =
		reg_audit_chip_get_modules(&num_mods);
	uint32_t idx = 0;
	uint32_t diff_cnt = 0;

	printk("REGDIFF_BEGIN|img=%s|jumped=%d|reset=0x%08x\n",
	       system_get_image_copy_string(), system_jumped_to_this_image(),
	       system_get_reset_flags());

	for (size_t m = 0; m < num_mods && m < MAX_MODULES; m++) {
		const struct reg_module_inst *mod = &mods[m];
		const struct reg_block_type *type = mod->type;
		int clk_pre =
			(snap_pre_init.mod_clocked[m / 32] & BIT(m % 32)) ? 1 :
									    0;
		int clk_post =
			(snap_post_init.mod_clocked[m / 32] & BIT(m % 32)) ? 1 :
									     0;
		bool match_filter =
			(filter_mod == NULL ||
			 strncasecmp(mod->name, filter_mod, 32) == 0 ||
			 strncasecmp(type->type_name, filter_mod, 32) == 0);

		if (match_filter && clk_pre != clk_post) {
			printk("CLKDIFF|%s|pre=%d|post=%d\n", mod->name,
			       clk_pre, clk_post);
		}

		for (uint16_t f = 0; f < type->num_fields; f++) {
			const struct reg_field_desc *fd = &type->fields[f];
			uint16_t count = fd->count ? fd->count : 1;
			uint8_t stride = fd->stride ? fd->stride : fd->size;
			char fstr[8];

			format_flags_str(fd->flags, fstr, sizeof(fstr));

			for (uint16_t i = 0; i < count; i++) {
				if (idx >= snap_pre_init.total_regs) {
					break;
				}
				uint32_t v_pre = snap_pre_init.values[idx];
				uint32_t v_post = snap_post_init.values[idx];
				uintptr_t offset =
					fd->offset + (uintptr_t)i * stride;
				char pre_str[16];
				char post_str[16];

				idx++;
				if (!match_filter || v_pre == v_post) {
					continue;
				}

				format_reg_val(v_pre, fd->size, pre_str,
					       sizeof(pre_str));
				format_reg_val(v_post, fd->size, post_str,
					       sizeof(post_str));

				if (count > 1) {
					printk("DIFF|%s|%s[%u]|0x%04x|%u|pre=%s|post=%s|%s\n",
					       mod->name, fd->name, i,
					       (unsigned int)offset, fd->size,
					       pre_str, post_str, fstr);
				} else {
					printk("DIFF|%s|%s|0x%04x|%u|pre=%s|post=%s|%s\n",
					       mod->name, fd->name,
					       (unsigned int)offset, fd->size,
					       pre_str, post_str, fstr);
				}

				if (++diff_cnt % 16 == 0) {
					cflush();
					watchdog_reload();
					k_msleep(2);
				}
			}
		}
	}

	printk("REGDIFF_END|diffs=%u\n", diff_cnt);
	cflush();
}

static int command_regdump(int argc, const char **argv)
{
	const char *mode = (argc >= 2) ? argv[1] : "post";
	const char *filter = (argc >= 3) ? argv[2] : NULL;

	if (strncasecmp(mode, "pre", 8) == 0) {
		reg_audit_dump(&snap_pre_init, "PRE_INIT", filter);
	} else if (strncasecmp(mode, "post", 8) == 0) {
		reg_audit_dump(&snap_post_init, "POST_INIT", filter);
	} else if (strncasecmp(mode, "live", 8) == 0) {
		reg_audit_dump(NULL, "LIVE", filter);
	} else if (strncasecmp(mode, "diff", 8) == 0) {
		reg_audit_diff_pre_post(filter);
	} else if (strncasecmp(mode, "info", 8) == 0) {
		size_t num_mods = 0;
		const struct reg_module_inst *mods =
			reg_audit_chip_get_modules(&num_mods);

		printk("Image:       %s\n", system_get_image_copy_string());
		printk("Jumped:      %d\n", system_jumped_to_this_image());
		printk("Reset flags: 0x%08x\n", system_get_reset_flags());
		printk("Modules:     %zu\n", num_mods);
		printk("Total regs:  %u (max %u)\n", snap_pre_init.total_regs,
		       CONFIG_PLATFORM_EC_REG_AUDIT_MAX_REGS);
		for (size_t m = 0; m < num_mods; m++) {
			int clk_pre = (snap_pre_init.mod_clocked[m / 32] &
				       BIT(m % 32)) ?
					      1 :
					      0;
			int clk_post = (snap_post_init.mod_clocked[m / 32] &
					BIT(m % 32)) ?
					       1 :
					       0;
			printk("  [%2zu] %-12s (%-10s) @ 0x%08lx clk: pre=%d post=%d\n",
			       m, mods[m].name, mods[m].type->type_name,
			       (unsigned long)mods[m].base, clk_pre, clk_post);
			if ((m + 1) % 16 == 0) {
				cflush();
				watchdog_reload();
			}
		}
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(regdump, command_regdump,
			"[pre|post|live|diff|info] [module]",
			"Dump or compare hardware register audit snapshots");
