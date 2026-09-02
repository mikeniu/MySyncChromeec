/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_RUNTIME_PORT_CONFIG_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_RUNTIME_PORT_CONFIG_H_

#include <zephyr/device.h>

/**
 * @brief If CONFIG_PDC_RUNTIME_PORT_CONFIG is enabled, boards must implement
 *        this function to choose drivers for each port. Will be called early
 *        during the EC boot sequence.
 *
 *        External callers should not access this function. Use
 *        pdc_power_mgmt_get_port_pdc_driver() instead
 *
 * @param port USB-C port number to query
 * @param dev Output pointer to device struct of chosen PDC driver, or NULL if
 *            this port shall not be activated.
 * @return 0 on success. Returning non-zero will cause the port not to activate
 */
int board_get_pdc_for_port(int port, const struct device **dev);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_RUNTIME_PORT_CONFIG_H_ */
