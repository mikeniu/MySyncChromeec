/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling, including displaying a message on the panic reporting
 * device, which is currently the UART.
 */

#ifndef PLATFORM_EC_INCLUDE_PANIC_H_
#define PLATFORM_EC_INCLUDE_PANIC_H_

#include "common.h"
#include "panic_defs.h"
#include "software_panic.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_RO_PANIC_DATA_SIZE
BUILD_ASSERT(sizeof(struct panic_data) == CONFIG_RO_PANIC_DATA_SIZE);
#endif

/* Use PANIC_DATA_PTR to refer to the persistent storage location */
#define PANIC_DATA_PTR ((struct panic_data *)CONFIG_PANIC_DATA_BASE)

/**
 * Write a string to the panic reporting device
 *
 * This function will not return until the string has left the UART
 * data register. Any previously queued UART traffic is displayed first.
 *
 * @param ch	Character to write
 */
void panic_puts(const char *s);

/**
 * Very basic printf() for use in panic situations
 *
 * See panic_vprintf() for full details
 *
 * @param format	printf-style format string
 * @param ...		Arguments to process
 */
__attribute__((__format__(__printf__, 1, 2))) void
panic_printf(const char *format, ...);

/*
 * Print saved panic information
 *
 * @param pdata pointer to saved panic data
 */
void panic_data_print(const struct panic_data *pdata);

/*
 * Print saved panic information on console channel to observe panic
 * information
 *
 * @param pdata pointer to saved panic data
 */
void panic_data_ccprint(const struct panic_data *pdata);

/**
 * Report an assertion failure and reset
 *
 * @param msg		Assertion expression or other message
 * @param func		Function name where assertion happened
 * @param fname		File name where assertion happened
 * @param linenum	Line number where assertion happened
 */

/**
 * Display a default message and reset
 */
#if !(defined(CONFIG_ZTEST))
__noreturn
#endif
	void panic_reboot(void);

struct arch_esf;
struct k_thread;

/**
 * Write panic data for a hardware exception (ESF).
 *
 * @param esf Pointer to the architecture exception stack frame, or NULL.
 */
void panic_data_write_esf(const struct arch_esf *esf);

/**
 * Write panic data for an assertion failure.
 *
 * @param path File path where assertion failed, or NULL if stripped.
 * @param line Line number where assertion failed.
 */
void panic_data_write_assert(const char *path, unsigned int line);

/**
 * Write panic data for a watchdog warning event.
 *
 * @param pc Program counter where execution was interrupted.
 * @param thread Thread pointer of the interrupted thread.
 */
void panic_data_write_watchdog_warning(uintptr_t pc,
				       const struct k_thread *thread);

/**
 * Write panic data for a Zephyr fatal error without an ESF.
 *
 * @param reason Zephyr fatal error reason code (e.g. K_ERR_KERNEL_PANIC).
 * @param thread Thread pointer of the faulting thread.
 */
void panic_data_write_fatal(unsigned int reason, const struct k_thread *thread);

/**
 * Write a generic software panic reason, info, and exception.
 *
 * @param reason PANIC_SW_* constant.
 * @param info Reason-specific 32-bit auxiliary info.
 * @param exception Exception or truncated thread ID byte.
 */
void panic_data_write_sw(uint32_t reason, uint32_t info, uint8_t exception);

/**
 * Reset/prepare a panic_data structure for writing.
 *
 * Sets struct size/version, architecture, and default image flags
 * (PANIC_DATA_FLAG_RW_IMAGE or PANIC_DATA_FLAG_RO_IMAGE).
 * Note: magic is NOT set here; call panic_data_finalize() when writing
 * completes.
 *
 * @param pdata Pointer to panic_data to reset, or NULL to reset and return
 *              the system panic data buffer from get_panic_data_write().
 * @return Pointer to the prepared panic_data structure.
 */
struct panic_data *panic_data_reset(struct panic_data *pdata);

/**
 * Finalize panic data by setting the valid magic number and flushing to RAM.
 */
void panic_data_finalize(struct panic_data *pdata);

/**
 * Get the panic reason field from a panic_data structure for the current
 * architecture.
 */
uint32_t panic_get_reason_reg(const struct panic_data *pdata);

/**
 * Set the panic reason field in a panic_data structure for the current
 * architecture.
 */
void panic_set_reason_reg(struct panic_data *pdata, uint32_t reason);

/**
 * Get the panic info field from a panic_data structure for the current
 * architecture.
 */
uint32_t panic_get_info_reg(const struct panic_data *pdata);

/**
 * Set the panic info field in a panic_data structure for the current
 * architecture.
 */
void panic_set_info_reg(struct panic_data *pdata, uint32_t info);

/**
 * Get the panic exception/thread field from a panic_data structure for the
 * current architecture.
 */
uint8_t panic_get_exception_reg(const struct panic_data *pdata);

/**
 * Set the panic exception/thread field in a panic_data structure for the
 * current architecture.
 */
void panic_set_exception_reg(struct panic_data *pdata, uint8_t exception);

/**
 * Check if stored panic data represents a new panic.
 *
 * A panic is considered new if valid panic data is present in RAM and has not
 * yet been read and acknowledged by the AP via host command (i.e. the
 * PANIC_DATA_FLAG_OLD_HOSTCMD flag is not set).
 */
bool panic_data_is_new(void);

/**
 * Enable/disable bus fault handler
 *
 * @param ignored	Non-zero if ignoring bus fault
 */
void ignore_bus_fault(int ignored);

/**
 * Return a pointer to the saved data from a previous panic that can be
 * safely interpreted
 *
 * @param pointer to the valid panic data, or NULL if none available (for
 * example, the last reboot was not caused by a panic).
 */
struct panic_data *panic_get_data(void);

/**
 * Return a pointer to the beginning of panic data. This function can be
 * used to obtain pointer which can be used to calculate place of other
 * structures (eg. jump_data). This function should not be used to get access
 * to panic_data structure as it might not be valid
 *
 * @param pointer to the beginning of panic_data, or NULL if there is no
 * panic_data
 */
uintptr_t get_panic_data_start(void);

#if defined(CONFIG_BOARD_NATIVE_POSIX) || defined(CONFIG_BOARD_NATIVE_SIM)
/**
 * @brief Test-only function for accessing the pdata_ptr object.
 *
 * @return struct panic_data* pdata_ptr
 */
struct panic_data *test_get_panic_data_pointer(void);
#endif

/**
 * Return a pointer to panic_data structure that can be safely written.  Please
 * note that this function can move jump data and jump tags.  It can also delete
 * panic data from previous boot, so this function should be used when we are
 * sure that we don't need it.
 *
 * NOTE: Invoking this function without subsequently setting the rest of the
 * panic data is unsafe because it leaves the panic data in an unfinished state
 * that may be inappropriately reported to the AP.
 * TODO(b/274661193): Finalize panic data with panic magic.
 *
 * @param pointer to panic_data structure that can be safely written
 */
struct panic_data *get_panic_data_write(void);

/**
 * Return a pointer to the stack of the process that caused the panic.
 * The implementation of this function will depend on the architecture.
 */
uint32_t get_panic_stack_pointer(const struct panic_data *pdata);

/**
 * Chip-specific implementation for backing up panic data to persistent
 * storage. This function is used to ensure that the panic data can survive loss
 * of VCC power rail.
 *
 * There is no generic restore function provided since every chip can decide
 * when it is safe to restore panic data during the system initialization step.
 */
void chip_panic_data_backup(void);

/**
 * Called from the panic handler to trigging nested crashes for testing.
 */
int command_crash_nested_handler(void);

#ifdef TEST_BUILD
/**
 * @brief Wrapper for accessing the command_crash() console command
 * implementation directly in unit tests. It cannot be called normally through
 * the shell interface because it upsets the shell's internal state when the
 * command doesn't return after a crash. command_crash() cannot be marked
 * test_export_static directly due to an implementation detail in
 * DECLARE_CONSOLE_COMMAND().
 *
 * @param argc Number of CLI args in `argv`
 * @param argv CLI arguments
 * @return int Return value
 */
int test_command_crash(int argc, const char **argv);
#endif /* TEST_BUILD*/

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_PANIC_H_ */
