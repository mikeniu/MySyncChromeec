/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_CBI_FLASH_H_
#define ZEPHYR_SHIM_INCLUDE_CBI_FLASH_H_

#include "cros_board_info.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CBI_FLASH_NODE DT_NODELABEL(cbi_flash)
#define CBI_FLASH_OFFSET DT_PROP(CBI_FLASH_NODE, offset)
#define CBI_FLASH_PRESERVE DT_PROP(CBI_FLASH_NODE, preserve)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SHIM_INCLUDE_CBI_FLASH_H_ */
