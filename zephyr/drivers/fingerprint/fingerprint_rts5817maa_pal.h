/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PAL_SENSOR_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PAL_SENSOR_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

/* Required alignment of the buffers handed to the private library. */
#define RTS5817MAA_MEM_ALIGNMENT 32

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
 * Writes data to the SPI interface and reads data back in full-duplex mode.
 * The caller is blocked until the operation is complete.
 *
 * @param data Data buffer to store the write data and receive the read data
 *             (full duplex).
 * @param len  Transfer size in bytes.
 *
 * @return 0 on success, negative errno code on failure.
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
 * @return Address on successful allocation, panic otherwise
 */
void __unused *rts5817_malloc(uint32_t size);

/**
 * @brief Free previously allocated memory
 *
 * @param data Pointer to buffer that should be freed
 */
void __unused rts5817_free(void *data);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PAL_SENSOR_H_ */
