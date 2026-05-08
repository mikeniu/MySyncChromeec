/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __FF_COMMAND_H__
#define __FF_COMMAND_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// sub cmd table
typedef struct {
	const char *name;
	int cmd;
} ft_cmd_entry_t;

typedef enum {
	UNKNOWN_CMD,
	ENTER_BOOT_CMD,
	ENTER_ROM_CMD,
	USB_SUSPEND_CMD,
	USB_RESUME_CMD,
	GET_FLASH_WP_CMD,
	SET_FLASH_WP_CMD,
	GET_EC_VERSION_CMD,
	FORCE_UPDATE_CMD,
	UPDATE_CMD,
} ft_cmd_t;

#define FT_ENTER_BOOT_CMD_STR "back_to_bootloader"
#define FT_ENTER_ROM_CMD_STR "back_to_rom_boot"
#define FT_USB_SUSPEND_CMD_STR "enter_usb_suspend"
#define FT_USB_RESUME_CMD_STR "enter_usb_resume"
#define FT_GET_FLASH_WP_CMD_STR "flash_get_protect"
#define FT_SET_FLASH_WP_CMD_STR "flash_set_protect"
#define FT_GET_EC_VERSION_CMD_STR "get_ec_version"
#define FT_FORCE_UPDATE_CMD_STR "force_update_full_ec_bin"
#define FT_UPDATE_CMD_STR ".bin"

static ft_cmd_entry_t cmd_table[] = {
	{ FT_ENTER_BOOT_CMD_STR, ENTER_BOOT_CMD },
	{ FT_ENTER_ROM_CMD_STR, ENTER_ROM_CMD },
	{ FT_USB_SUSPEND_CMD_STR, USB_SUSPEND_CMD },
	{ FT_USB_RESUME_CMD_STR, USB_RESUME_CMD },
	{ FT_GET_FLASH_WP_CMD_STR, GET_FLASH_WP_CMD },
	{ FT_SET_FLASH_WP_CMD_STR, SET_FLASH_WP_CMD },
	{ FT_GET_EC_VERSION_CMD_STR, GET_EC_VERSION_CMD },
	{ FT_FORCE_UPDATE_CMD_STR, FORCE_UPDATE_CMD },
	{ NULL, UNKNOWN_CMD }
};

#ifdef __cplusplus
}
#endif

#endif
