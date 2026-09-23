/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_CONFIG_STD_INTERNAL_FLASH_H_
#define PLATFORM_EC_INCLUDE_CONFIG_STD_INTERNAL_FLASH_H_

/*
 * Standard memory-mapped flash layout:
 * - RO image starts at the beginning of flash.
 * - PSTATE immediately follows the RO image.
 * - RW image starts at the second half of flash.
 * - Protected region consists of the first half of flash (RO image + PSTATE).
 * - Unprotected region consists of second half of flash (RW image).
 *
 *                            PSTATE
 *                              |
 *                              v
 * |<-----Protected Region------>|<------Unprotected Region----->|
 * |<--------RO image--------->| |<----------RW image----------->|
 * 0                            N/2                              N
 *
 * This layout is used by several supported chips. Chips which do not use
 * this layout MUST NOT include this header file, and must instead define
 * the configs below in a chip-level header file (config_flash_layout.h).
 *
 * See the following page for additional image geometry discussion:
 *
 * https://www.chromium.org/chromium-os/ec-development/ec-image-geometry-spec
 *
 * TODO(crosbug.com/p/23796): Finish implementing the spec.
 */

/*
 * Size of one firmware image in flash - half for RO, half for RW.
 * This is NOT a globally defined config, and is only used in this file
 * for convenience.
 */
#define _IMAGE_SIZE (CONFIG_FLASH_SIZE_BYTES / 2)

/*
 * The EC uses the one bank of flash to emulate a SPI-like write protect
 * register with persistent state.
 */
#define CONFIG_FLASH_PSTATE
#define CONFIG_FW_PSTATE_SIZE CONFIG_FLASH_BANK_SIZE
#define CONFIG_FW_PSTATE_OFF (_IMAGE_SIZE - CONFIG_FW_PSTATE_SIZE)

#define CONFIG_RO_MEM_OFF 0
#define CONFIG_RO_STORAGE_OFF 0
#define CONFIG_RO_SIZE (_IMAGE_SIZE - CONFIG_FW_PSTATE_SIZE)
#define CONFIG_RW_MEM_OFF (CONFIG_RO_MEM_OFF + _IMAGE_SIZE)
#define CONFIG_RW_STORAGE_OFF 0
#define CONFIG_RW_SIZE _IMAGE_SIZE

#define CONFIG_EC_PROTECTED_STORAGE_OFF 0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_OFF CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE \
	(CONFIG_FLASH_SIZE_BYTES - CONFIG_EC_WRITABLE_STORAGE_OFF)

#define CONFIG_WP_STORAGE_OFF CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE CONFIG_EC_PROTECTED_STORAGE_SIZE

#endif /* PLATFORM_EC_INCLUDE_CONFIG_STD_INTERNAL_FLASH_H_ */
