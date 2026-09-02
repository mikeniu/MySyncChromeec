/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dirkson declarations */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_DIRKSON_INCLUDE_BOARD_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_DIRKSON_INCLUDE_BOARD_H_

enum charge_port {
	CHARGE_PORT_TYPEC0,
	CHARGE_PORT_BARRELJACK,
};

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_COUNT };

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_DIRKSON_INCLUDE_BOARD_H_ */
