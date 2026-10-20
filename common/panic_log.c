/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "link_defs.h"
#include "panic.h"
#include "panic_log.h"
#include "util.h"

#include <zephyr/irq.h>
#include <zephyr/sys/ring_buffer.h>

/* Panic log defaults to frozen */
test_export_static bool panic_log_frozen = true;
test_export_static bool panic_log_initialized = false;

test_export_static struct panic_log_storage panic_log __uncached __aligned(4)
	__noinit_end_of_ram(panic_log);

/* Basic access functions */
test_export_static uint32_t panic_log_len(void)
{
	return ring_buf_size_get(&panic_log.rb);
}

test_export_static uint32_t panic_log_capacity(void)
{
	return ring_buf_capacity_get(&panic_log.rb);
}

test_export_static uint32_t panic_log_version(void)
{
	return panic_log.version;
}

test_export_static bool panic_log_is_valid(void)
{
	return (panic_log.start_magic == PANIC_LOG_START_MAGIC &&
		panic_log.end_magic == PANIC_LOG_END_MAGIC &&
		panic_log.version == PANIC_LOG_VERSION &&
		panic_log.rb.size == CONFIG_PANIC_LOG_SIZE &&
		panic_log.rb.buffer == panic_log.buffer);
}

test_export_static char panic_log_read(uint32_t offset)
{
	uint8_t *data;

	ring_buf_get_ptr(&panic_log.rb, &data, offset);
	return *data;
}

/* Returns the panic log freeze state before applying the requested freeze state
 */
test_export_static bool panic_log_freeze(bool freeze)
{
	bool orig_frozen = panic_log_frozen;
	panic_log_frozen = freeze;
	return orig_frozen;
}

/* Resets the panic log and defaults to frozen */
test_export_static void panic_log_reset(void)
{
	panic_log_frozen = true;
	panic_log.start_magic = PANIC_LOG_START_MAGIC;
	panic_log.end_magic = PANIC_LOG_END_MAGIC;
	panic_log.version = PANIC_LOG_VERSION;
	ring_buf_init(&panic_log.rb, CONFIG_PANIC_LOG_SIZE, panic_log.buffer);
}

test_export_static void panic_log_init(void)
{
	bool freeze;
	/* Initialize may only run once */
	if (panic_log_initialized)
		return;
	panic_log_initialized = true;

	if (!panic_log_is_valid()) {
		if (IS_ENABLED(CONFIG_PANIC_LOG_DEBUG))
			ccprintf(
				"Panic log is invalid, resetting and unfreezing panic log\n");
		panic_log_reset();
		freeze = false;
	} else if (panic_data_is_new()) {
		if (IS_ENABLED(CONFIG_PANIC_LOG_DEBUG))
			ccprintf(
				"New panic detected, keeping panic log frozen\n");
		freeze = true;
	} else {
		if (IS_ENABLED(CONFIG_PANIC_LOG_DEBUG))
			ccprintf("No new panic, unfreezing panic log\n");
		freeze = false;
	}
	panic_log_freeze(freeze);
}
DECLARE_HOOK(HOOK_INIT_EARLY, panic_log_init, HOOK_PRIO_DEFAULT - 1);

static void panic_log_write_bytes(const uint8_t *data, size_t size)
{
	unsigned int key = irq_lock();

	if (!panic_log_frozen) {
		if (size > CONFIG_PANIC_LOG_SIZE) {
			data += size - CONFIG_PANIC_LOG_SIZE;
			size = CONFIG_PANIC_LOG_SIZE;
		}

		uint32_t space = ring_buf_space_get(&panic_log.rb);

		if (size > space)
			ring_buf_consume(&panic_log.rb, size - space);

		ring_buf_put(&panic_log.rb, data, size);
	}

	irq_unlock(key);
}

void panic_log_write_char(const char c)
{
	panic_log_write_bytes((const uint8_t *)&c, 1);
}

void panic_log_write_str(const char *str, const size_t size)
{
	panic_log_write_bytes((const uint8_t *)str, size);
}

/* Returns the current state of panic log, before applying any requested changes
 */
static enum ec_host_cmd_status
host_command_get_panic_log_info(struct ec_host_cmd_handler_args *args)
{
	struct ec_response_panic_log_info *r = args->output_buf;
	const struct ec_params_panic_log_info *p = args->input_buf;

	if (p->freeze && p->unfreeze)
		return EC_HOST_CMD_INVALID_PARAM;

	/* Freeze before while reading */
	bool orig_frozen = panic_log_freeze(true);
	r->frozen = orig_frozen;
	r->version = panic_log_version();
	r->capacity = panic_log_capacity();
	r->valid = panic_log_is_valid();
	r->length = panic_log_len();
	args->output_buf_size = sizeof(*r);

	if (p->reset)
		panic_log_reset();

	if (p->freeze)
		panic_log_freeze(true);
	else if (p->unfreeze)
		panic_log_freeze(false);
	else
		/* Restore original frozen state if no change requested */
		panic_log_freeze(orig_frozen);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_PANIC_LOG_INFO, host_command_get_panic_log_info,
		    EC_VER_MASK(0), struct ec_params_panic_log_info,
		    struct ec_response_panic_log_info);

static enum ec_host_cmd_status
host_command_read_panic_log(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_panic_log_read *p = args->input_buf;
	char *response = args->output_buf;
	uint32_t offset = p->offset;
	uint32_t length = panic_log_len();

	if (offset >= length)
		return EC_HOST_CMD_INVALID_PARAM;

	while (offset < length &&
	       args->output_buf_size < args->output_buf_max) {
		response[args->output_buf_size++] = panic_log_read(offset++);
	}

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_PANIC_LOG_READ, host_command_read_panic_log,
			     EC_VER_MASK(0), struct ec_params_panic_log_read);

#if defined(CONFIG_PANIC_LOG_DEBUG)

test_export_static void panic_log_corrupt(void)
{
	panic_log.start_magic = 0;
}

static int command_panic_log(int argc, const char **argv)
{
	if ((argc == 1) || (argc == 2 && !strcasecmp(argv[1], "info"))) {
		bool orig_frozen = panic_log_freeze(true);
		ccprintf("Valid: %d\n", panic_log_is_valid());
		ccprintf("Frozen: %d\n", orig_frozen);
		ccprintf("Length: %d\n", panic_log_len());
		ccprintf("Capacity: %d\n", panic_log_capacity());
		ccprintf("Version: %d\n", panic_log_version());
		panic_log_freeze(orig_frozen);
	} else if (argc == 2 && !strcasecmp(argv[1], "dump")) {
		bool orig_frozen = panic_log_freeze(true);
		char dump_buffer[16];
		panic_printf("=== Panic Log Start ===\n");
		for (int i = 0; i < panic_log_len();) {
			int j = 0;
			for (; j < ARRAY_SIZE(dump_buffer) - 1 &&
			       i < panic_log_len();
			     j++, i++)
				dump_buffer[j] = panic_log_read(i);
			dump_buffer[j] = '\0';
			/*
			 * Using panic_printf to dump for two reasons:
			 * 1) Avoid dumping panic log back into panic log, since
			 * panic_printf does not go to panic log. 2) Avoid being
			 * re-tokenized if CONFIG_PIGWEED_LOG_TOKENIZED_LIB is
			 * enabled
			 */
			panic_printf("%s", dump_buffer);
		}
		panic_log_freeze(orig_frozen);
		panic_printf("\n=== Panic Log End ===\n");
	} else if (argc == 2 && !strcasecmp(argv[1], "reset")) {
		bool orig_frozen = panic_log_freeze(true);
		panic_log_reset();
		panic_log_freeze(orig_frozen);
		ccprintf("Panic log reset, currently %s\n",
			 orig_frozen ? "frozen" : "unfrozen");
	} else if (argc == 2 && !strcasecmp(argv[1], "freeze")) {
		bool orig_frozen = panic_log_freeze(true);
		if (orig_frozen) {
			ccprintf("Panic log already frozen\n");
		} else {
			ccprintf("Panic log frozen\n");
		}
	} else if (argc == 2 && !strcasecmp(argv[1], "unfreeze")) {
		bool orig_frozen = panic_log_freeze(false);
		if (!orig_frozen) {
			ccprintf("Panic log already unfrozen\n");
		} else {
			ccprintf("Panic log unfrozen\n");
		}
	} else if (argc == 2 && !strcasecmp(argv[1], "corrupt")) {
		panic_log_corrupt();
		ccprintf("Panic log corrupted\n");
	} else {
		return EC_ERROR_PARAM1;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(paniclog, command_panic_log,
			"[info | dump | reset | freeze | unfreeze | corrupt]",
			"Panic log");

#endif /* CONFIG_PANIC_LOG_DEBUG */
