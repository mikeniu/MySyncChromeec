/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "event_log.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Event log FIFO */
#define UNIT_SIZE sizeof(struct event_log_entry)
#define UNIT_COUNT (CONFIG_EVENT_LOG_SIZE / UNIT_SIZE)
#define UNIT_COUNT_MASK (UNIT_COUNT - 1)
static struct event_log_entry log_events[UNIT_COUNT];
BUILD_ASSERT(POWER_OF_TWO(UNIT_COUNT));

/*
 * The FIFO pointers are defined as following :
 * "log_head" is the next available event to dequeue.
 * "log_tail" is marking the end of the FIFO content (after last committed
 *  event)
 * The pointers are not wrapped until they are used, so we don't need an extra
 * entry to disambiguate between full and empty FIFO.
 *
 * For concurrency, several tasks might try to enqueue events in parallel with
 * log_add_event() while another task is dequeuing events with
 * log_dequeue_event(), so FIFO accesses and updates are serialized in critical
 * sections.
 */
static size_t log_head;
static size_t log_tail;

/* Size of one FIFO entry */
#define ENTRY_SIZE(payload_sz) (1 + DIV_ROUND_UP((payload_sz), UNIT_SIZE))

void log_add_event(uint8_t type, uint8_t size, uint16_t data, void *payload,
		   uint32_t timestamp)
{
	struct event_log_entry *r;
	size_t payload_size = EVENT_LOG_SIZE(size);
	size_t total_size = ENTRY_SIZE(payload_size);
	size_t current_tail, first;
	uint32_t lock_key;

	lock_key = irq_lock();
	current_tail = log_tail;

	/* Out of space : discard the oldest entry */
	while ((UNIT_COUNT - (current_tail - log_head)) < total_size) {
		struct event_log_entry *oldest;

		oldest = log_events + (log_head & UNIT_COUNT_MASK);
		log_head += ENTRY_SIZE(EVENT_LOG_SIZE(oldest->size));
	}

	r = log_events + (current_tail & UNIT_COUNT_MASK);

	r->timestamp = timestamp;
	r->type = type;
	r->size = size;
	r->data = data;
	/* copy the payload into the FIFO */
	first = min(total_size - 1,
		    (UNIT_COUNT - (current_tail & UNIT_COUNT_MASK)) - 1);
	if (first)
		memcpy(r->payload, payload, first * UNIT_SIZE);
	if (first < total_size - 1)
		memcpy(log_events, ((uint8_t *)payload) + first * UNIT_SIZE,
		       (total_size - 1 - first) * UNIT_SIZE);

	log_tail = current_tail + total_size;
	irq_unlock(lock_key);
}

int log_dequeue_event(struct event_log_entry *r)
{
	uint32_t now = get_time().val >> EVENT_LOG_TIMESTAMP_SHIFT;
	unsigned int total_size, first;
	struct event_log_entry *entry;
	size_t current_head;
	uint32_t lock_key;

	lock_key = irq_lock();
	current_head = log_head;

	/* The log FIFO is empty */
	if (log_tail == current_head) {
		irq_unlock(lock_key);
		memset(r, 0, UNIT_SIZE);
		r->type = EVENT_LOG_NO_ENTRY;
		return UNIT_SIZE;
	}

	entry = log_events + (current_head & UNIT_COUNT_MASK);
	total_size = ENTRY_SIZE(EVENT_LOG_SIZE(entry->size));
	first = min(total_size, UNIT_COUNT - (current_head & UNIT_COUNT_MASK));
	memcpy(r, entry, first * UNIT_SIZE);
	if (first < total_size)
		memcpy(r + first, log_events, (total_size - first) * UNIT_SIZE);

	log_head += total_size;
	irq_unlock(lock_key);

	/* fixup the timestamp : number of milliseconds in the past */
	r->timestamp = now - r->timestamp;

	return total_size * UNIT_SIZE;
}

#ifdef CONFIG_CMD_DLOG
/*
 * Display TPM event logs.
 */
static int command_dlog(int argc, const char **argv)
{
	size_t log_cur;
	const uint8_t *const log_events_end =
		(uint8_t *)&log_events[UNIT_COUNT];

	if (argc > 1) {
		if (!strcasecmp(argv[1], "clear")) {
			interrupt_disable();
			log_head = log_tail = 0;
			interrupt_enable();

			return EC_SUCCESS;
		}
		/* Too many parameters */
		return EC_ERROR_PARAM1;
	}

	ccprintf(" TIMESTAMP | TYPE |  DATA | SIZE | PAYLOAD\n");
	log_cur = log_head;
	while (log_cur != log_tail) {
		struct event_log_entry *r;
		uint8_t *payload;
		uint32_t payload_bytes;

		r = &log_events[log_cur & UNIT_COUNT_MASK];
		payload_bytes = EVENT_LOG_SIZE(r->size);
		log_cur += ENTRY_SIZE(payload_bytes);

		ccprintf("%10d   %4d  0x%04X   %4d   ", r->timestamp, r->type,
			 r->data, payload_bytes);

		/* display payload if exists */
		payload = r->payload;
		while (payload_bytes--) {
			if (payload >= log_events_end)
				payload = (uint8_t *)&log_events[0];

			ccprintf("%02X", *payload);
			payload++;
		}
		ccprintf("\n");
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dlog, command_dlog, "[clear]",
			"Display/clear TPM event logs");
#endif
