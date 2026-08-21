/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_ELAN_SERIES_CONFIG_H_
#define PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_ELAN_SERIES_CONFIG_H_

/*
 * Select the correct configuration file based on the enabled ELAN series
 * fingerprint sensor.
 */

#if defined(CONFIG_FINGERPRINT_SENSOR_ELAN80SG)
#include "fingerprint_elan80sg_config.h"
#elif defined(CONFIG_FINGERPRINT_SENSOR_ELANI80SA)
#include "fingerprint_elani80sa_config.h"
#elif defined(CONFIG_FINGERPRINT_SENSOR_ELANHV515RC)
#include "fingerprint_elanhv515rc_config.h"
#else
#error "No valid configuration for fingerprint sensor."
#endif

#endif /* PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_ELAN_SERIES_CONFIG_H_ \
	*/
