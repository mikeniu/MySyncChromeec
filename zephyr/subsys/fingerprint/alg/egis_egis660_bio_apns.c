/**
 * Copyright (c) 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license.
 *
 * APNS storage implementation for EC Zephyr firmware.
 * Provides non-weak overrides for the storage functions in libfp.a
 */

#include "common.h"
#include "flash.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(egis_apns_storage, LOG_LEVEL_INF);

#define APNS_STORAGE_ADDR DT_REG_ADDR(DT_NODELABEL(apns_data))
#define APNS_STORAGE_SIZE DT_REG_SIZE(DT_NODELABEL(apns_data))

/**
 * @brief Storage information.
 *
 * The alignment requirements are specified in number of bytes.
 */
static const struct {
    /** Required address and size alignment for read operations. */
    uint32_t read_align;
    /** Required address and size alignment for write operations. */
    uint32_t write_align;
    /** Required address and size alignment for erase operations. */
    uint32_t erase_align;
    /** True if flash can be read using memcpy. */
    bool is_memory_mapped;
} apns_storage_info = {
    .read_align = 1,
    .write_align = CONFIG_FLASH_WRITE_IDEAL_SIZE,
    .erase_align = CONFIG_FLASH_ERASE_SIZE,
    .is_memory_mapped = true,
};

static inline uint32_t get_apns_base_offset(void)
{
    return APNS_STORAGE_ADDR;
}

const void *egis_apns_storage_get_info(void)
{
    return &apns_storage_info;
}

int egis_apns_data_erase(uint32_t offset, size_t size)
{
    int rc;

    if (offset + size > APNS_STORAGE_SIZE) {
        LOG_ERR("egis_apns_data_erase: invalid offset=%u, size=%zu", offset, size);
        return EC_ERROR_INVAL;
    }

    /* Align to erase boundary (4KB) */
    uint32_t erase_start = offset & ~0xFFF;
    uint32_t erase_end = (offset + size + 0xFFF) & ~0xFFF;
    uint32_t aligned_size = erase_end - erase_start;

    LOG_DBG("egis_apns_data_erase: offset=%u, size=%zu, aligned=%u",
            offset, size, aligned_size);

    rc = crec_flash_physical_erase(get_apns_base_offset() + erase_start, aligned_size);
    if (rc != EC_SUCCESS) {
        LOG_ERR("egis_apns_data_erase: flash erase failed, rc=%d", rc);
    }

    return rc;
}

int egis_apns_data_read(uint32_t offset, size_t size, void *data)
{
    int rc;
    uint32_t abs_offset;

    if (offset + size > APNS_STORAGE_SIZE) {
        LOG_ERR("egis_apns_data_read: invalid offset=%u, size=%zu", offset, size);
        return EC_ERROR_INVAL;
    }

    if (data == NULL) {
        return EC_ERROR_INVAL;
    }

    abs_offset = get_apns_base_offset() + offset;

    LOG_DBG("egis_apns_data_read: offset=%u, size=%zu", offset, size);

    rc = crec_flash_physical_read(abs_offset, size, (char *)data);
    if (rc != EC_SUCCESS) {
        LOG_ERR("egis_apns_data_read: flash read failed, rc=%d", rc);
    }

    return rc;
}

int egis_apns_data_write(uint32_t offset, const void *data, size_t size)
{
    int rc;
    uint32_t abs_offset;

    if (offset + size > APNS_STORAGE_SIZE) {
        LOG_ERR("egis_apns_data_write: invalid offset=%u, size=%zu", offset, size);
        return EC_ERROR_INVAL;
    }

    if (data == NULL) {
        return EC_ERROR_INVAL;
    }

    abs_offset = get_apns_base_offset() + offset;

    LOG_DBG("egis_apns_data_write: offset=%u, size=%zu", offset, size);

    rc = crec_flash_physical_write(abs_offset, size, (const char *)data);
    if (rc != EC_SUCCESS) {
        LOG_ERR("egis_apns_data_write: flash write failed, rc=%d", rc);
    }

    return rc;
}
