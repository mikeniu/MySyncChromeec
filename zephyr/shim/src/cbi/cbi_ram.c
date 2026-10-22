/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_config.h"
#include "cros_board_info.h"
#include "cros_cbi.h"

#include <string.h>

static uint8_t cbi_ram_buf[CBI_IMAGE_SIZE] __aligned(4);
static bool cbi_ram_initialized;

static void cbi_ram_ensure_init(void)
{
	struct cbi_header *head = (struct cbi_header *)cbi_ram_buf;

	if (cbi_ram_initialized) {
		return;
	}

	memset(cbi_ram_buf, 0, sizeof(cbi_ram_buf));
	memcpy(head->magic, cbi_magic, sizeof(cbi_magic));
	head->total_size = sizeof(*head);
	head->major_version = CBI_VERSION_MAJOR;
	head->minor_version = CBI_VERSION_MINOR;
	head->crc = cbi_crc8(head);
	cbi_ram_initialized = true;
}

__attribute__((weak)) void cbi_ram_store_notify(void)
{
}

static int ram_load(uint8_t offset, uint8_t *data, int len)
{
	if (len < 0 || offset + len > CBI_IMAGE_SIZE) {
		return EC_ERROR_INVAL;
	}

	cbi_ram_ensure_init();
	memcpy(data, cbi_ram_buf + offset, len);
	return EC_SUCCESS;
}

static int ram_is_write_protected(void)
{
	return 0;
}

static int ram_store(uint8_t *cbi)
{
	memcpy(cbi_ram_buf, cbi, CBI_IMAGE_SIZE);
	cbi_ram_initialized = true;

	if (IS_ENABLED(CONFIG_CROS_EC_CBI_SSFC_PARSER)) {
		cros_cbi_ssfc_init();
	}
	if (IS_ENABLED(CONFIG_CROS_EC_CBI_FW_CONFIG_PARSER)) {
		cros_cbi_fw_config_init();
	}
	if (IS_ENABLED(CONFIG_CROS_EC_CBI_UFSC_PARSER)) {
		cros_cbi_ufsc_init();
	}

	cbi_ram_store_notify();
	return EC_SUCCESS;
}

static const struct cbi_storage_driver ram_drv = {
	.store = ram_store,
	.load = ram_load,
	.is_protected = ram_is_write_protected,
};

const struct cbi_storage_config_t ram_cbi_config = {
	.storage_type = CBI_STORAGE_TYPE_RAM,
	.drv = &ram_drv,
};
