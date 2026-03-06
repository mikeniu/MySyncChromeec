/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PAL_SENSOR_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PAL_SENSOR_H_

#include <zephyr/toolchain.h>

/**
 * @brief rts5817 MAA debug log message.
 *
 * @param[in] format A printf-style format string for the message.
 * @param[in] ... Variable number of arguments to be formatted according
 * to the format string.
 *
 */
void __unused rts5817_debug_log(const char *format, ...);

/**
 * @brief Writes and reads SPI data.
 *
 * Writes data to SPI interface and reads data from SPI interface, with chip
 * select control. The caller is blocked until the operation is complete. By use
 * of the chip select control parameter a single SPI transaction can be split in
 * several calls.
 *
 * @param data data buffer to store the write data and receive the read data(Full Duplex)
 * @param len  transfer size.
 *
 * @return 0 If successful in master mode.
 * @return negative number on failure.
 */
int __unused rts5817_spi_xfer(uint8_t *data, uint32_t len);

/**
 * @brief Get the time of the system.
 *
 * @return time [ms]
 */
uint64_t __unused rts5817_get_time(void);

/**
 * @brief Busy wait.
 *
 * @param us  Time to wait [us].
 */
void __unused rts5817_busy_wait(uint32_t us);

/**
 * @brief Set sensor reset pin state.
 *
 * @param active true  => reset sensor, i.e. low GPIO state
 *              false => normal operation, i.e. high GPIO state
 */
void __unused rts5817_set_reset_pin(bool active);

/**
 * @brief Allocate memory
 *
 * @param size Allocation size
 * @return Address on successful allocation, NULL otherwise
 */
void __unused *rts5817_malloc(uint32_t size);

/**
 * @brief Free previously allocated memory
 *
 * @param data Pointer to buffer that should be freed
 */
void __unused rts5817_free(void *data);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PAL_SENSOR_H_ */
