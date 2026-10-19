/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor detection (transport and sensor). */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_DETECT_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_DETECT_H_

#include "fpsensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum fp_sensor_type fpsensor_detect_get_type(void);
enum fp_transport_type get_fp_transport_type(void);
enum fp_sensor_spi_select fpsensor_detect_get_spi_select(void);

#ifdef __cplusplus
}

[[nodiscard]] constexpr const char *
fp_transport_type_to_str(fp_transport_type type) noexcept
{
	switch (type) {
	case FP_TRANSPORT_TYPE_UNKNOWN:
	default:
		return "UNKNOWN";
	case FP_TRANSPORT_TYPE_SPI:
		return "SPI";
	case FP_TRANSPORT_TYPE_UART:
		return "UART";
	case FP_TRANSPORT_TYPE_USB:
		return "USB";
	}
}

[[nodiscard]] constexpr const char *
fp_sensor_type_to_str(fp_sensor_type type) noexcept
{
	switch (type) {
	case FP_SENSOR_TYPE_UNKNOWN:
	default:
		return "UNKNOWN";
	case FP_SENSOR_TYPE_FPC:
		return "FPC";
	case FP_SENSOR_TYPE_ELAN:
		return "ELAN";
	case FP_SENSOR_TYPE_EGIS:
		return "EGIS";
	case FP_SENSOR_TYPE_FOCALTECH:
		return "FOCALTECH";
	case FP_SENSOR_TYPE_REALTEK:
		return "REALTEK";
	}
}

[[nodiscard]] constexpr const char *
fp_sensor_spi_select_to_str(fp_sensor_spi_select type) noexcept
{
	switch (type) {
	case FP_SENSOR_SPI_SELECT_UNKNOWN:
	default:
		return "UNKNOWN";
	case FP_SENSOR_SPI_SELECT_DEVELOPMENT:
		return "DEVELOPMENT";
	case FP_SENSOR_SPI_SELECT_PRODUCTION:
		return "PRODUCTION";
	}
}
#endif

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_DETECT_H_ */
