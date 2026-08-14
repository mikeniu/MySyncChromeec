/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/espi/espi_realtek_rts5912.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rtk_sysjump_espi, LOG_LEVEL_DBG);

#define ESPI_SYSJUMP_TAG 0x5912
#define ESPI_PRESERVE_DATA_RESTORE_HOOK_PRIORITY 2

static int restore_espi_state_after_sysjump(void)
{
	const uint8_t *jumptag_data = NULL;
	int jumptag_size = 0;
	int jumptag_version = 0;
	int rv;

	if (!system_jumped_to_this_image()) {
		/* Cold boot. The ESPI hardware is reset.*/
		LOG_DBG("RTK ESPI sysjump: Cold boot. Do nothing.");
		return 0;
	}

	jumptag_data = system_get_jump_tag(ESPI_SYSJUMP_TAG, &jumptag_version,
					   &jumptag_size);
	if (!jumptag_data || !jumptag_size) {
		LOG_WRN("RTK ESPI sysjump: Sysjump happened but no jumptag");
		return 0;
	}

	if (jumptag_version < 1) {
		LOG_ERR("RTK ESPI sysjump: Jumptag version must be >=1 (got %d)",
			jumptag_version);
		return 0;
	}

	/* Restore some preserved ESPI driver state */
	rv = espi_rts5912_set_preserved_data(jumptag_size, jumptag_data);
	if (rv) {
		LOG_ERR("RTK ESPI sysjump: Cannot restore preserve_data: %d",
			rv);
		return 0;
	}

	LOG_INF("RTK ESPI sysjump: Restored data!");
	LOG_HEXDUMP_DBG(jumptag_data, jumptag_size, "Preserve data contents");

	return 0;
}

/* ESPI is initialized in PRE_KERNEL_2. Enforce that
 * ESPI_PRESERVE_DATA_RESTORE_HOOK_PRIORITY comes before
 * CONFIG_ESPI_INIT_PRIORITY.
 */
BUILD_ASSERT(ESPI_PRESERVE_DATA_RESTORE_HOOK_PRIORITY <
	     CONFIG_ESPI_INIT_PRIORITY);

SYS_INIT(restore_espi_state_after_sysjump, PRE_KERNEL_2,
	 ESPI_PRESERVE_DATA_RESTORE_HOOK_PRIORITY);

static void preserve_espi_state_before_sysjump(void)
{
	uint8_t preserved_data[32];
	int rv;

	rv = espi_rts5912_get_preserved_data(sizeof(preserved_data),
					     preserved_data);
	if (rv < 0) {
		LOG_ERR("RTK ESPI sysjump: Cannot get preserved data: %d", rv);
		return;
	}

	LOG_INF("RTK ESPI sysjump: Captured preserve_data");
	LOG_HEXDUMP_DBG(preserved_data, rv, "Saved data");

	rv = system_add_jump_tag(ESPI_SYSJUMP_TAG, 1, rv, preserved_data);
	if (rv) {
		LOG_ERR("RTK ESPI sysjump: Cannot save jumptag: %d", rv);
		return;
	}

	LOG_INF("RTK ESPI sysjump: Saved jumptag!");
}

DECLARE_HOOK(HOOK_SYSJUMP, preserve_espi_state_before_sysjump,
	     HOOK_PRIO_DEFAULT);
