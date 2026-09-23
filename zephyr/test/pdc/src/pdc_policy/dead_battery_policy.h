/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file tests the dead battery policies on type-C ports.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_PDC_SRC_PDC_POLICY_DEAD_BATTERY_POLICY_H_
#define PLATFORM_EC_ZEPHYR_TEST_PDC_SRC_PDC_POLICY_DEAD_BATTERY_POLICY_H_

#include "chipset.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/utils.h"

#include <zephyr/device.h>

#define PDC_TEST_TIMEOUT 2000

/* TODO: b/343760437 - Once the emulator can detect the PDC threads are idle,
 * remove the sleep delay to let the policy code run.
 */
#define PDC_POLICY_DELAY_MS 500
#define PDC_NODE_PORT0 DT_NODELABEL(pdc_emul1)
#define PDC_NODE_PORT1 DT_NODELABEL(pdc_emul2)

#define USBC_NODE0 DT_NODELABEL(usbc0)
#define USBC_NODE1 DT_NODELABEL(usbc1)

#define TEST_USBC_PORT0 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT0)
#define TEST_USBC_PORT1 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT1)

#define IS_ONE_BIT_SET IS_POWER_OF_TWO

struct pdc_fixture {
	const struct device *dev;
	const struct emul *emul_pdc;
	uint32_t pdos[PDO_MAX_OBJECTS];
	uint8_t port;
};

struct dead_battery_policy_fixture {
	struct pdc_fixture pdc[CONFIG_USB_PD_PORT_MAX_COUNT];
};

void clear_partner_pdos(const struct emul *e, enum pdo_type_t type);

void pdc_driver_init(void);

int configure_dead_battery(const struct pdc_fixture *pdc);

void verify_dead_battery_config(const struct emul *e);

void set_chipset_state(enum chipset_state_mask state);

#endif /* PLATFORM_EC_ZEPHYR_TEST_PDC_SRC_PDC_POLICY_DEAD_BATTERY_POLICY_H_ */
