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

/**
 * Hardware strap detection and name formatting for fingerprint sensor and
 * transport configuration.
 */
class FpDetectedHardware {
    public:
	[[nodiscard]] static fp_sensor_type sensor_type() noexcept
	{
		return fpsensor_detect_get_type();
	}

	[[nodiscard]] static constexpr const char *
	transport_type_name(fp_transport_type type) noexcept
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

	[[nodiscard]] static constexpr const char *
	sensor_type_name(fp_sensor_type type) noexcept
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

	[[nodiscard]] static const char *sensor_type_name() noexcept
	{
		return sensor_type_name(sensor_type());
	}

	[[nodiscard]] static constexpr const char *
	spi_select_name(fp_sensor_spi_select type) noexcept
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
};

[[nodiscard]] constexpr const char *
fp_transport_type_to_str(fp_transport_type type) noexcept
{
	return FpDetectedHardware::transport_type_name(type);
}

[[nodiscard]] constexpr const char *
fp_sensor_type_to_str(fp_sensor_type type) noexcept
{
	return FpDetectedHardware::sensor_type_name(type);
}

[[nodiscard]] constexpr const char *
fp_sensor_spi_select_to_str(fp_sensor_spi_select type) noexcept
{
	return FpDetectedHardware::spi_select_name(type);
}
#else
static inline const char *fp_transport_type_to_str(enum fp_transport_type type)
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

static inline const char *fp_sensor_type_to_str(enum fp_sensor_type type)
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

static inline const char *
fp_sensor_spi_select_to_str(enum fp_sensor_spi_select type)
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
