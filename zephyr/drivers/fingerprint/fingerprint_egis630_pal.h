// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_EGIS630_PAL_SENSOR_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_EGIS630_PAL_SENSOR_H_

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/toolchain.h>

#ifdef CONFIG_ZTEST
#define STATIC_VAR
#else
#define STATIC_VAR static
#endif

typedef enum {
	LOG_VERBOSE = 2,
	LOG_DEBUG = 3,
	LOG_INFO = 4,
	LOG_WARN = 5,
	LOG_ERROR = 6,
	LOG_ASSERT = 7,
} LOG_LEVEL;

#define LOG_TAG "PLAT-SPI"

#define EGIS_LOG_ENTRY() egislog_d("Start %s", __func__)
#define EGIS_LOG_EXIT(x) egislog_i("Exit %s, ret=%d", __func__, x)

#define FILE_NAME \
	(strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define egislog(level, format, ...)                                       \
	output_log(level, LOG_TAG, FILE_NAME, __func__, __LINE__, format, \
		   ##__VA_ARGS__)
#define ex_log(level, format, ...)                                      \
	output_log(level, "RBS", FILE_NAME, __func__, __LINE__, format, \
		   ##__VA_ARGS__)

#if !defined(LOGD)
#define LOGD(format, ...)                                                   \
	output_log(LOG_DEBUG, "RBS", FILE_NAME, __func__, __LINE__, format, \
		   ##__VA_ARGS__)
#endif

#if !defined(LOGE)
#define LOGE(format, ...)                                                   \
	output_log(LOG_ERROR, "RBS", FILE_NAME, __func__, __LINE__, format, \
		   ##__VA_ARGS__)
#endif

#define egislog_e(format, args...) egislog(LOG_ERROR, format, ##args)
#define egislog_d(format, args...) egislog(LOG_DEBUG, format, ##args)
#define egislog_i(format, args...) egislog(LOG_INFO, format, ##args)
#define egislog_v(format, args...) egislog(LOG_VERBOSE, format, ##args)
#define CPRINTF(format, args...) printk(format, ##args)
#define CPRINTS(format, args...) printk(format, ##args)

#define RBS_CHECK_IF_NULL(x, errorcode)               \
	if (x == NULL) {                              \
		LOGE("%s, " #x " is NULL", __func__); \
		return errorcode;                     \
	}

/**
 * @brief Sets the global debug level, controlling which log messages are
 * output.
 *
 * @param[in] level The desired debug level.
 *
 */
void __unused set_debug_level(LOG_LEVEL level);

/**
 * @brief Formats and outputs a log message based on the provided level, tag,
 * file information, and format string.
 *
 * @param[in] level The log level of the message.
 * @param[in] tag A tag or category for the message.
 * @param[in] file_name The file path where the log message originates.
 * @param[in] func The function name where the log message originates.
 * @param[in] line The line number where the log message originates.
 * @param[in] format A printf-style format string for the message.
 * @param[in] ... Variable number of arguments to be formatted according
 * to the format string.
 *
 */
void __unused output_log(LOG_LEVEL level, const char *tag,
			 const char *file_name, const char *func, int line,
			 const char *format, ...);

/**
 * @brief Issues a SPI transaction. Assumes SPI port has already been enabled.
 *
 * Transmits @p tx_len bytes from @p tx_addr, throwing away the corresponding
 * received data, then transmits @p rx_len bytes, saving the received data in @p
 * rx_buf.
 *
 * @param[in] tx_addr Pointer to the transmit buffer containing the data to be
 * sent.
 * @param[in] tx_len Length of the transmit buffer in bytes.
 * @param[out] rx_buf Pointer to the receive buffer where received data will be
 * stored.
 * @param[in] rx_len Length of the receive buffer in bytes.
 *
 * @return 0 on success.
 * @return A value from @ref ec_error_list enum.
 */
int __unused periphery_spi_write_read(uint8_t *write, uint32_t write_len,
				      uint8_t *read, uint32_t read_len);

/**
 * @brief Gets the current time in milliseconds.
 *
 * @return The current time in milliseconds.
 *
 */
unsigned long long __unused plat_get_time(void);

/**
 * @brief Calculates the time difference in milliseconds between the current
 * time and a given starting time.
 *
 * @param[in] begin The starting time in milliseconds.
 *
 * @return The time difference in milliseconds.
 *
 */
unsigned long __unused plat_get_diff_time(unsigned long long begin);

/**
 * @brief Busy-wait.
 *
 * @param[in] msecs The delay time in milliseconds.
 *
 */
void __unused plat_wait_time(unsigned long msecs);

/**
 * @brief Sleep.
 *
 * The current task will be de-scheduled for at least the specified delay (and
 * perhaps longer, if a higher-priority task is running when the delay expires).
 *
 * @param[in] timeInMs The sleep time in milliseconds.
 *
 */
void __unused plat_sleep_time(unsigned long timeInMs);

#ifdef EGIS_SPEED_DBG
#define TIME_MEASURE_START(name) \
	unsigned long long timeMeasureStart##name = plat_get_time();
#define TIME_MEASURE_STOP(name, x)                                       \
	unsigned long name = plat_get_diff_time(timeMeasureStart##name); \
	egislog_d(x SPEED_TEST_STR, name);
#define TIME_MEASURE_STOP_INFO(name, x)                                  \
	unsigned long name = plat_get_diff_time(timeMeasureStart##name); \
	egislog_i(x SPEED_TEST_STR, name);
#define TIME_MEASURE_STOP_AND_RESTART(name, x)                         \
	{                                                              \
		egislog_d(x SPEED_TEST_STR,                            \
			  plat_get_diff_time(timeMeasureStart##name)); \
		timeMeasureStart##name = plat_get_time();              \
	}
#define TIME_MEASURE_RESET(name) timeMeasureStart##name = plat_get_time();
#else
#define TIME_MEASURE_START(name)
#define TIME_MEASURE_STOP(name, x)
#define TIME_MEASURE_STOP_INFO(name, x)
#define TIME_MEASURE_STOP_AND_RESTART(name, x)
#define TIME_MEASURE_RESET(name)
#endif
/**
 * @brief Allocates a block of memory of a specified size and number of
 * elements.
 *
 * @param[in] count The number of elements to allocate.
 * @param[in] size The size of each element in bytes.
 *
 * @return Pointer to the allocated memory or NULL if the allocation failed.
 */
void *__unused sys_alloc(size_t count, size_t size);

/**
 * @brief Releases a block of shared memory.
 *
 * @param[in] data A pointer to the memory block to be released.
 *
 */
void __unused sys_free(void *data);

static inline void plat_free(void *x)
{
	sys_free(x);
}

// TODO (b/373435445): Combine PLAT_FREE and plat_free.
/**
 * @brief Deallocates memory and sets the provided pointer to NULL.
 *
 * @param[in] x A pointer to a pointer to the memory block to be freed.
 *
 */
static inline void PLAT_FREE(void **x)
{
	assert(x != NULL && *x != NULL);
	plat_free(*x);
	*x = NULL;
}

/**
 * @brief Allocates a block of memory of the specified size.
 *
 * @param[in] size The size of the memory block to allocate, in bytes.
 *
 * @return Pointer to the allocated memory or NULL if the allocation failed.
 */
static inline void *plat_alloc(size_t size)
{
	return sys_alloc(1, size);
}

/**
 * @brief Allocates memory for an array of count elements of size bytes each and
 * initializes all bytes to zero.
 *
 * @param[in] count Number of elements to allocate.
 * @param[in] size Size of each element.
 *
 * @return Pointer to allocated memory initialized to zero, or NULL if
 * allocation failed.
 */
static inline void *plat_calloc(size_t count, size_t size)
{
	void *ptr = sys_alloc(1, count * size);
	if (ptr)
		memset(ptr, 0, count * size);
	return ptr;
}

/**
 * @brief Reallocates the given memory block to a new size.
 *
 * @param[in] data Pointer to the previously allocated memory block.
 * @param[in] size New size in bytes for the memory block.
 *
 * @return Pointer to the reallocated memory block, or NULL if reallocation
 * failed.
 */
static inline void *plat_realloc(void *data, size_t size)
{
	void *new_ptr = sys_alloc(1, size);
	if (new_ptr && data) {
		memcpy(new_ptr, data, size);
		sys_free(data);
	}
	return new_ptr;
}

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_EGIS630_PAL_SENSOR_H_ */
