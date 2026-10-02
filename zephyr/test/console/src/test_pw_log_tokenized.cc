/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

#include <pw_log/levels.h>
#include <pw_log_tokenized/metadata.h>

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
extern "C" int format_dropped_logs_msg(char *dest, size_t dest_size,
				       uint32_t drops_isr, uint32_t drops_mutex,
				       uint32_t drops_overflow);
#endif
extern "C" void pw_log_tokenized_HandleLog(uint32_t metadata,
					   const uint8_t log_buffer[],
					   size_t size_bytes);

#define THREAD_STACK_SIZE 2048
static K_THREAD_STACK_DEFINE(fatal_thread_stack, THREAD_STACK_SIZE);
static struct k_thread fatal_thread_data;

static volatile bool fatal_hook_called;
static volatile unsigned int fatal_reason;

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	fatal_hook_called = true;
	fatal_reason = reason;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);
	fatal_hook_called = false;
	fatal_reason = 0;
	console_channel_enable("system");
}

ZTEST_SUITE(pw_log_tokenized, NULL, NULL, test_before, NULL, NULL);

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
ZTEST(pw_log_tokenized, test_format_dropped_logs_msg)
{
	char buf[128];
	int len = format_dropped_logs_msg(buf, sizeof(buf), 1, 2, 3);

	zassert_true(len > 0);
	zassert_equal(buf[0], '$');
}
#endif

ZTEST(pw_log_tokenized, test_handle_log_normal)
{
	uint32_t metadata =
		pw::log_tokenized::Metadata::Set<PW_LOG_LEVEL_INFO, 0, 0, 0>()
			.value();
	const uint8_t test_token[4] = { 0x12, 0x34, 0x56, 0x78 };

	pw_log_tokenized_HandleLog(metadata, test_token, sizeof(test_token));
	zassert_false(fatal_hook_called);
}

struct fatal_thread_args {
	uint32_t metadata;
	const uint8_t *token;
	size_t size;
};

static void fatal_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	auto *args = static_cast<struct fatal_thread_args *>(p1);

	ztest_set_fault_valid(true);
	pw_log_tokenized_HandleLog(args->metadata, args->token, args->size);
	/* Should never be reached if pw_log_tokenized_HandleLog panicked */
	ztest_test_fail();
}

static void run_fatal_log_test(uint32_t metadata, const uint8_t *token,
			       size_t size)
{
	fatal_hook_called = false;
	struct fatal_thread_args args = {
		.metadata = metadata,
		.token = token,
		.size = size,
	};

	k_tid_t tid = k_thread_create(&fatal_thread_data, fatal_thread_stack,
				      THREAD_STACK_SIZE, fatal_thread_entry,
				      &args, NULL, NULL, K_PRIO_PREEMPT(5), 0,
				      K_NO_WAIT);

	k_thread_join(tid, K_FOREVER);
	zassert_true(fatal_hook_called, "Fatal error hook was not called");
	zassert_equal(fatal_reason, K_ERR_KERNEL_PANIC);
}

ZTEST(pw_log_tokenized, test_handle_log_fatal)
{
	uint32_t metadata =
		pw::log_tokenized::Metadata::Set<PW_LOG_LEVEL_FATAL, 0, 0, 0>()
			.value();
	const uint8_t test_token[4] = { 0x12, 0x34, 0x56, 0x78 };

	run_fatal_log_test(metadata, test_token, sizeof(test_token));
}

ZTEST(pw_log_tokenized, test_handle_log_fatal_disabled_channel)
{
	/* Set channel to SYSTEM (1) offset by 1 -> flag 2 */
	uint32_t metadata =
		pw::log_tokenized::Metadata::Set<PW_LOG_LEVEL_FATAL, 0,
						 CC_SYSTEM + 1, 0>()
			.value();
	const uint8_t test_token[4] = { 0x12, 0x34, 0x56, 0x78 };

	console_channel_disable("system");
	run_fatal_log_test(metadata, test_token, sizeof(test_token));
}

ZTEST(pw_log_tokenized, test_handle_log_non_fatal_disabled_channel)
{
	/* Set channel to SYSTEM (1) offset by 1 -> flag 2 */
	uint32_t metadata =
		pw::log_tokenized::Metadata::Set<PW_LOG_LEVEL_INFO, 0,
						 CC_SYSTEM + 1, 0>()
			.value();
	const uint8_t test_token[4] = { 0x12, 0x34, 0x56, 0x78 };

	console_channel_disable("system");
	pw_log_tokenized_HandleLog(metadata, test_token, sizeof(test_token));
	zassert_false(fatal_hook_called);
}

ZTEST(pw_log_tokenized, test_handle_log_non_fatal_enabled_channel)
{
	/* Set channel to SYSTEM (1) offset by 1 -> flag 2 */
	uint32_t metadata =
		pw::log_tokenized::Metadata::Set<PW_LOG_LEVEL_INFO, 0,
						 CC_SYSTEM + 1, 0>()
			.value();
	const uint8_t test_token[4] = { 0x12, 0x34, 0x56, 0x78 };

	console_channel_enable("system");
	pw_log_tokenized_HandleLog(metadata, test_token, sizeof(test_token));
	zassert_false(fatal_hook_called);
}

ZTEST(pw_log_tokenized, test_handle_log_fatal_empty_payload)
{
	uint32_t metadata =
		pw::log_tokenized::Metadata::Set<PW_LOG_LEVEL_FATAL, 0, 0, 0>()
			.value();

	/* Empty buffer generates base64_string of size <= 1 */
	run_fatal_log_test(metadata, nullptr, 0);
}

ZTEST(pw_log_tokenized, test_handle_log_non_fatal_empty_payload)
{
	uint32_t metadata =
		pw::log_tokenized::Metadata::Set<PW_LOG_LEVEL_INFO, 0, 0, 0>()
			.value();

	/* Empty buffer generates base64_string of size <= 1 */
	pw_log_tokenized_HandleLog(metadata, nullptr, 0);
	zassert_false(fatal_hook_called);
}
