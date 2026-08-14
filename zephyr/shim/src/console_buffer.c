/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "power.h"

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <subsys/usbd_service.h>

#ifdef CONFIG_PLATFORM_EC_SOC_IT8XXX2_CONSOLE_BUF_H2RAM_SHARED
extern uint8_t h2ram_pool[];
#define console_buf h2ram_pool
/*
 * CONFIG_ESPI_PERIPHERAL_HOST_CMD_PARAM_PORT_NUM and
 * CONFIG_ESPI_PERIPHERAL_ACPI_SHM_REGION_PORT_NUM represent the offset from the
 * base of h2ram_pool where each eSPI region begins.
 * Since console_buf is placed at the beginning of h2ram_pool, ensure its size
 * does not exceed these offsets so that it does not overlap with the
 * host-command parameter region or ACPI shared memory region.
 */
BUILD_ASSERT(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE <=
	     CONFIG_ESPI_PERIPHERAL_HOST_CMD_PARAM_PORT_NUM);
BUILD_ASSERT(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE <=
	     CONFIG_ESPI_PERIPHERAL_ACPI_SHM_REGION_PORT_NUM);
#else
static char console_buf[CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE];
#endif
static uint32_t previous_snapshot_idx;
static uint32_t current_snapshot_idx;
static uint32_t read_next_idx;
static uint32_t head_idx;
static uint32_t tail_idx;
#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
static atomic_t dropped_logs_isr;
static atomic_t dropped_logs_mutex;
static atomic_t dropped_logs_overflow;
#endif
#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT
/* Guarded with console_write_lock */
static bool new_console_log;

void console_log_notify_host(char new_char)
{
	/* Inform about every new line, not to send event every char. */
	if (new_char == '\n') {
		/* Avoid waking the host from a low-power state for console
		 * logs.
		 */
		bool wake_host = true;
#ifdef CONFIG_AP_POWER_CONTROL
		if (power_get_state() != POWER_S0) {
			wake_host = false;
		}
#endif /* CONFIG_AP_POWER_CONTROL */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_USB
		if (usb_is_suspended()) {
			wake_host = false;
		}
#endif /* CONFIG_EC_HOST_CMD_BACKEND_USB */
		if (!new_console_log && wake_host) {
			/* Set the host event once since last snapshot not to
			 * spam events e.g. in case host doesn't read the logs.
			 */
			new_console_log = true;
			host_set_single_event(EC_HOST_EVENT_CONSOLE_LOGS);
		}
	}
}
#endif

static inline uint32_t next_idx(uint32_t cur_idx)
{
	return (cur_idx + 1) % CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE;
}

static uint32_t next_log(uint32_t start_idx)
{
	uint32_t idx = start_idx;

	while (idx != tail_idx) {
#ifndef CONFIG_PLATFORM_EC_LOG_TOKENIZED
		char prev = console_buf[idx];
#endif

		idx = next_idx(idx);

		/* For plain text, we need to check the end of the log for
		 * newline. For tokenized logs, we need to check for the start
		 * of the log with the special prefix character. In both cases,
		 * the idx points to the start of the next log line.
		 */
#ifdef CONFIG_PLATFORM_EC_LOG_TOKENIZED
		if (console_buf[idx] == PW_TOKENIZER_NESTED_PREFIX_STR[0])
			break;
#else
		if (prev == '\n')
			break;
#endif
	}

	return idx;
}

K_MUTEX_DEFINE(console_write_lock);

size_t console_buf_notify_chars(const char *s, size_t len)
{
	/*
	 * This is just notifying of console characters for debugging
	 * output, so if we are unable to lock the mutex immediately,
	 * then just drop the string. Mutexes cannot be locked from an
	 * isr, so also drop the string in this case too.
	 */
	if (k_is_in_isr()) {
#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
		atomic_inc(&dropped_logs_isr);
#endif
		return 0;
	}
	if (k_mutex_lock(&console_write_lock, K_NO_WAIT)) {
#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
		atomic_inc(&dropped_logs_mutex);
#endif
		return 0;
	}
	/* We got the mutex. */
	for (size_t i = 0; i < len; i++) {
		/* Don't copy null byte into buffer */
		if (!(*s)) {
			s++;
			continue;
		}

		uint32_t new_tail = next_idx(tail_idx);

		/* Check if we are starting to overwrite our snapshot
		 * heads
		 */
		if (new_tail == head_idx)
			head_idx = next_log(head_idx);
		if (new_tail == previous_snapshot_idx) {
#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
			atomic_inc(&dropped_logs_overflow);
#endif
			previous_snapshot_idx = next_log(previous_snapshot_idx);
		}
		if (new_tail == current_snapshot_idx)
			current_snapshot_idx = next_log(current_snapshot_idx);
		if (new_tail == read_next_idx)
			read_next_idx = next_log(read_next_idx);

		console_buf[tail_idx] = *s++;

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT
		console_log_notify_host(console_buf[tail_idx]);
#endif /* CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT */

		tail_idx = new_tail;
	}
	k_mutex_unlock(&console_write_lock);
	return len;
}

enum ec_status uart_console_read_buffer_init(void)
{
	if (k_mutex_lock(&console_write_lock, K_MSEC(100)))
		/* Failed to acquire console buffer mutex */
		return EC_RES_TIMEOUT;

	/* For read next, start reading at the beginning of the buffer */
	read_next_idx = head_idx;
	/*
	 * For read recent, start reading at the beginning of the previous
	 * snapshot
	 */
	previous_snapshot_idx = current_snapshot_idx;
	/*
	 * Limit read command to characters available at the moment of creating
	 * snapshot
	 */
	current_snapshot_idx = tail_idx;

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT
	new_console_log = false;
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_CONSOLE_LOGS));
#endif /* CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT */

	k_mutex_unlock(&console_write_lock);

	return EC_RES_SUCCESS;
}

static void copy_console_buf_range(uint32_t *head, uint32_t stop_idx,
				   char *dest, uint16_t *write_count,
				   uint16_t dest_size)
{
	while (*head != stop_idx && *write_count < dest_size - 1) {
		dest[(*write_count)++] = console_buf[*head];
		*head = next_idx(*head);
	}
}

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
__overridable int format_dropped_logs_msg(char *dest, size_t dest_size,
					  uint32_t drops_isr,
					  uint32_t drops_mutex,
					  uint32_t drops_overflow)
{
	uint32_t total_drops = drops_isr + drops_mutex + drops_overflow;

	return snprintf(dest, dest_size,
			"Dropped %u logs (ISR: %u, Mutex: %u, Overflow: %u)\n",
			total_drops, drops_isr, drops_mutex, drops_overflow);
}

static inline bool is_log_entry_boundary(char c)
{
	if (c == '\n' || c == '\0')
		return true;
#ifdef CONFIG_PLATFORM_EC_LOG_TOKENIZED
	if (c == PW_TOKENIZER_NESTED_PREFIX_STR[0])
		return true;
#endif
	return false;
}

static uint16_t handle_dropped_logs(char *dest, size_t dest_size)
{
	uint32_t drops_isr = atomic_get(&dropped_logs_isr);
	uint32_t drops_mutex = atomic_get(&dropped_logs_mutex);
	uint32_t drops_overflow = atomic_get(&dropped_logs_overflow);

	if ((drops_isr | drops_mutex | drops_overflow) == 0)
		return 0;

	if (dest_size == 0)
		return 0;

	char msg[128];
	int len = format_dropped_logs_msg(msg, sizeof(msg), drops_isr,
					  drops_mutex, drops_overflow);
	if (len <= 0 || (size_t)len >= dest_size)
		return 0;

	atomic_sub(&dropped_logs_isr, drops_isr);
	atomic_sub(&dropped_logs_mutex, drops_mutex);
	atomic_sub(&dropped_logs_overflow, drops_overflow);

	memcpy(dest, msg, len);
	return len;
}

static void process_dropped_logs(uint32_t *head, uint32_t snapshot_idx,
				 char *dest, uint16_t *write_count,
				 uint16_t dest_size)
{
	bool has_dropped = (atomic_get(&dropped_logs_isr) |
			    atomic_get(&dropped_logs_mutex) |
			    atomic_get(&dropped_logs_overflow)) != 0;

	if (has_dropped && *head != snapshot_idx) {
		/* If *head is mid-token/line, flush remainder of current log
		 * entry first */
		if (!is_log_entry_boundary(console_buf[*head])) {
			uint32_t end_idx = next_log(*head);

			copy_console_buf_range(head, end_idx, dest, write_count,
					       dest_size);
		}
	}

	*write_count += handle_dropped_logs(dest + *write_count,
					    dest_size - *write_count);
}
#endif /* CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS */

int uart_console_read_buffer(uint8_t type, char *dest, uint16_t dest_size,
			     uint16_t *write_count_out)
{
	uint32_t *head;
	uint16_t write_count = 0;

	switch (type) {
	case CONSOLE_READ_NEXT:
		/*
		 * Start where we left or from the beginning of the buffer after
		 * snapshot
		 */
		head = &read_next_idx;
		break;
	case CONSOLE_READ_RECENT:
		/* Start from end of previous snapshot */
		head = &previous_snapshot_idx;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	/* We need to make sure we have room for at least the null byte */
	if (dest_size == 0)
		return EC_RES_INVALID_PARAM;

	if (k_mutex_lock(&console_write_lock, K_MSEC(100)))
		/* Failed to acquire console buffer mutex */
		return EC_RES_TIMEOUT;

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
	process_dropped_logs(head, current_snapshot_idx, dest, &write_count,
			     dest_size);
#endif

	if (*head == current_snapshot_idx && write_count == 0) {
		/* No new data, return empty response */
		k_mutex_unlock(&console_write_lock);
		*write_count_out = 0;
		return EC_RES_SUCCESS;
	}

	copy_console_buf_range(head, current_snapshot_idx, dest, &write_count,
			       dest_size);

	dest[write_count] = '\0';
	write_count++;

	*write_count_out = write_count;
	k_mutex_unlock(&console_write_lock);

	return EC_RES_SUCCESS;
}

/* ECOS uart buffer, putc is blocking instead. */
int uart_buffer_full(void)
{
	return false;
}
