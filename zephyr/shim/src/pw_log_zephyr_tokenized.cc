/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "panic_log.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/printk.h>

#include <pw_log/levels.h>
#include <pw_log_tokenized/base64.h>
#include <pw_log_tokenized/config.h>
#include <pw_log_tokenized/handler.h>
#include <pw_log_tokenized/metadata.h>
#include <pw_tokenizer/base64.h>
#include <pw_tokenizer/tokenize.h>

#ifndef PW_FLAG_TO_EC_CHANNEL
#define PW_FLAG_TO_EC_CHANNEL(flag) ((enum console_channel)((flag) - 1))
#endif

namespace pw::log_zephyr
{
namespace
{
	// The Zephyr console may output raw text along with Base64 tokenized
	// messages, which could interfere with detokenization. Output a
	// character to mark the end of a Base64 message.
	// If delimiter changes, make sure to update the following files to
	// match
	//  -- src/third_party/hdctools/servo/ec3po/console.py
	constexpr char kEndDelimiter = '~';

	struct k_spinlock lock;

	// Static buffer guarded by spinlock to prevent stack allocation
	// (~270B).
	pw::InlineString<log_tokenized::kBase64EncodedBufferSizeBytes + 1>
		base64_string;

	[[noreturn]] void handle_fatal_log(void)
	{
		LOG_PANIC();
		k_panic();
		CODE_UNREACHABLE;
	}
} // namespace
} // namespace pw::log_zephyr

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS
extern "C" {
int format_dropped_logs_msg(char *dest, size_t dest_size, uint32_t drops_isr,
			    uint32_t drops_mutex, uint32_t drops_overflow)
{
	uint32_t total_drops = drops_isr + drops_mutex + drops_overflow;
	uint8_t token_buf[32];
	size_t token_size = sizeof(token_buf);

	PW_TOKENIZE_TO_BUFFER(
		token_buf, &token_size,
		"Dropped %u logs (ISR: %u, Mutex: %u, Overflow: %u)\n",
		total_drops, drops_isr, drops_mutex, drops_overflow);

	k_spinlock_key_t key = k_spin_lock(&pw::log_zephyr::lock);

	pw::log_zephyr::base64_string.clear();
	pw::log_zephyr::base64_string.push_back(PW_TOKENIZER_NESTED_PREFIX);
	pw::base64::Encode(pw::as_bytes(pw::span(token_buf, token_size)),
			   pw::log_zephyr::base64_string);

	int len = snprintf(dest, dest_size, "%s",
			   pw::log_zephyr::base64_string.c_str());

	k_spin_unlock(&pw::log_zephyr::lock, key);

	return len;
}
}
#endif /* CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_DROPPED_LOGS */

namespace pw::log_zephyr
{

extern "C" void pw_log_tokenized_HandleLog(uint32_t metadata,
					   const uint8_t log_buffer[],
					   size_t size_bytes)
{
	pw::log_tokenized::Metadata meta(metadata);

	/* flags == 0 --> regular Zephyr Logging
	 * flags != 0 --> EC Console Channel Logging offset by 1
	 *                Check if channel is enabled to send log to console
	 */
	if (meta.flags() > 0) {
		if (console_channel_is_disabled(
			    PW_FLAG_TO_EC_CHANNEL(meta.flags()))) {
			if (meta.level() == PW_LOG_LEVEL_FATAL) {
				handle_fatal_log();
			}
			return;
		}
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	base64_string.clear();
	base64_string.push_back(PW_TOKENIZER_NESTED_PREFIX);
	pw::base64::Encode(pw::as_bytes(pw::span(log_buffer, size_bytes)),
			   base64_string);

	if (base64_string.size() <= 1) {
		k_spin_unlock(&lock, key);
		if (meta.level() == PW_LOG_LEVEL_FATAL) {
			handle_fatal_log();
		}
		return;
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_PANIC_LOG)) {
		panic_log_write_str(base64_string.c_str(),
				    base64_string.size());
	}

	// On DUT, timberslide doesn't receive console raw text, okay to send
	// base64 message without end delimiter
	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE)) {
		console_buf_notify_chars(base64_string.c_str(),
					 base64_string.size());
	}

	if (base64_string.size() < base64_string.capacity()) {
		base64_string += kEndDelimiter;
	}

	// TODO(asemjonovs):
	// https://github.com/zephyrproject-rtos/zephyr/issues/59454 Zephyr
	// frontend should protect messages from getting corrupted from multiple
	// threads.
	if (k_is_in_isr()) {
		printk("[ISR]%s", base64_string.c_str());
	} else {
		printk("%s", base64_string.c_str());
	}
	k_spin_unlock(&lock, key);

	if (meta.level() == PW_LOG_LEVEL_FATAL) {
		handle_fatal_log();
	}
}

} // namespace pw::log_zephyr
