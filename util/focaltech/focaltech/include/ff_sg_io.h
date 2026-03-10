/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FT_SG_IO_H
#define FT_SG_IO_H

#include <stdint.h>

#include <scsi/sg.h>

// USB Mass Storage Bulk-Only Transport magic
#define USB_MS_CBW_SIGNATURE 0x43425355
#define USB_MS_CSW_SIGNATURE 0x53425355
#define USB_MS_CBW_LENGTH 31
#define USB_MS_CSW_LENGTH 13

#define DIRECTION_NONE 0
#define DIRECTION_IN 1
#define DIRECTION_OUT 2

// error code
#define SG_IO_SUCCESS 0
#define SG_IO_CHECK_CONDITION 1
#define SG_IO_DRIVER_ERROR 2
#define SG_IO_TIMEOUT 3
#define SG_IO_BAD_TARGET 4

/* FocalTech Vendor-Specific SCSI Command Opcodes */
#define FT_CMD_READ_INFO 0xdb
#define FT_CMD_ACTION 0xdc

/* Sub-commands for FT_CMD_ACTION (typically placed at byte 13) */
#define FT_SUBCMD_READ_DATA 0x01
#define FT_SUBCMD_WRITE_BIN 0x03
#define FT_SUBCMD_CODE_BIN 0x04
#define FT_SUBCMD_ERASE_PAGE 0x08
#define FT_SUBCMD_VERIFY 0xbb
#define FT_SUBCMD_EXEC_DISBOOT 0xcc
#define FT_SUBCMD_SOFT_RESET 0xdd
#define FT_SUBCMD_FLASH_WP_SET 0xF7
#define FT_SUBCMD_FLASH_WP_GET 0xF8

/* Common target lengths */
#define FT_CMD_LENGTH 16

#define FT_PARA_UNSET 0
#define FT_PARA_READ_SRAM_DIFF 1
#define FT_PARA_READ_SRAM_SAME 2

#define FT_BOOT_VERSION_MAX 36

#define FT_WRITE_SRAM_ADDR (0x20002000)
#define FT_READ_SRAM_ADDR (0x20003000)

#define FT_EC_BIN_SIZE_MAX (2 * 1024 * 1024)

typedef enum ft_hash_mode_e {
	HASH_MODE_SM3,
	HASH_MODE_SHA0,
	HASH_MODE_SHA1,
	HASH_MODE_SHA224,
	HASH_MODE_SHA256,
	HASH_MODE_SHA384,
	HASH_MODE_SHA512,
} ft_hash_mode_t;

typedef enum ft_soft_reset_e { SOFT_POR, SOFT_RST } ft_soft_reset_t;

typedef enum ft_flash_protect_area_e {
	FLASH_WP_AREA_UNKNOWN,
	FLASH_WP_AREA_ALL,
	FLASH_WP_AREA_BOOT, // addr=0x10000000~0x10080000
	FLASH_WP_AREA_RO, // addr=0x10000000~0x10100000
	FLASH_WP_AREA_RW, // addr=0x10000000~0x10180000
	FLASH_WP_AREA_OUT_RANGE
} ft_flash_protect_area_t;

// BOT (Bulk-Only Transport)
struct bulk_cb_wrap {
	uint32_t signature;
	uint32_t tag;
	uint32_t data_transfer_length;
	uint8_t flags;
	uint8_t lun;
	uint8_t cmd_len;
	uint8_t cmd[16];
} __attribute__((packed));

struct bulk_cs_wrap {
	uint32_t signature;
	uint32_t tag;
	uint32_t data_residue;
	uint8_t status;
} __attribute__((packed));

int ft_sg_init(void);
int ft_sg_open(uint16_t vendor_id, uint16_t product_id);
int ft_sg_io(struct sg_io_hdr *io_hdr);
void ft_sg_close(void);

#endif
