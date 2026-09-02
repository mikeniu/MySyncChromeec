/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_CORSOLA_INCLUDE_VARIANT_DB_DETECTION_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_CORSOLA_INCLUDE_VARIANT_DB_DETECTION_H_

#include <stdint.h>

enum corsola_db_type {
	CORSOLA_DB_UNINIT = -1,
	/* CORSOLA_DB_NO_DETECTION means there is no detection involved in. */
	CORSOLA_DB_NO_DETECTION,
	/* CORSOLA_DB_NONE means there is no DB in the design. */
	CORSOLA_DB_NONE,
	CORSOLA_DB_TYPEC,
	CORSOLA_DB_HDMI,
	CORSOLA_DB_COUNT,
};

/*
 * Get the connected daughterboard type.
 *
 * @return		The daughterboard type.
 */
#if defined(CONFIG_VARIANT_CORSOLA_DB_DETECTION) || defined(CONFIG_TEST)
enum corsola_db_type corsola_get_db_type(void);
#else
inline enum corsola_db_type corsola_get_db_type(void)
{
	return CORSOLA_DB_NO_DETECTION;
}
#endif

/* return the adjusted port count for board overridden usbc/charger functions.
 */
test_mockable uint8_t board_get_adjusted_usb_pd_port_count(void);

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_CORSOLA_INCLUDE_VARIANT_DB_DETECTION_H_ \
	*/
