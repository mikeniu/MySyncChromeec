/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_INTELRVP_INCLUDE_INTEL_RVP_BOARD_ID_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_INTELRVP_INCLUDE_INTEL_RVP_BOARD_ID_H_

#include <zephyr/drivers/gpio.h>

#define DT_DRV_COMPAT intel_rvp_board_id

#define RVP_ID_HAS_BOM_GPIOS DT_NODE_HAS_PROP(DT_DRV_INST(0), bom_gpios)

#define RVP_ID_HAS_FAB_GPIOS DT_NODE_HAS_PROP(DT_DRV_INST(0), fab_gpios)

#if RVP_ID_HAS_BOM_GPIOS
extern const struct gpio_dt_spec bom_id_config[];
#endif

#if RVP_ID_HAS_FAB_GPIOS
extern const struct gpio_dt_spec fab_id_config[];
#endif

extern const struct gpio_dt_spec board_id_config[];

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_INTELRVP_INCLUDE_INTEL_RVP_BOARD_ID_H_ */
