/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_state.h"
#include "host_command.h"
#include "mock/otpi_mock.h"
#include "otp_key.h"

#include <zephyr/kernel.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

/*
 * In production builds (CONFIG_ZTEST=n), host commands run on the main thread
 * (CONFIG_MAIN_STACK_SIZE). In hardware test builds (CONFIG_ZTEST=y),
 * CONFIG_EC_HOST_CMD_DEDICATED_THREAD is selected and host commands run on the
 * dedicated ec_host_cmd thread (hc->thread) with stack size
 * CONFIG_EC_HOST_CMD_HANDLER_STACK_SIZE.
 *
 * Ensure the test exercises the exact production HOSTCMD stack size.
 */
BUILD_ASSERT(CONFIG_EC_HOST_CMD_HANDLER_STACK_SIZE == CONFIG_MAIN_STACK_SIZE,
	     "HC handler stack size must match production MAIN stack size");

extern "C" const k_tid_t FPSENSOR;

namespace
{

/*
 * Minimum required unused stack headroom (in bytes) after executing the
 * heaviest fingerprint cryptographic paths on both HOSTCMD and FPSENSOR tasks.
 */
constexpr size_t kMinRequiredStackHeadroomBytes = 512;

/*
 * Minimum expected stack usage (in bytes) to confirm the cryptographic code
 * paths actually executed on the target threads.
 */
constexpr size_t kMinExpectedStackUsedBytes = 1024;

constexpr int kMaxAsyncPollAttempts = 100;
constexpr k_timeout_t kAsyncPollDelay = K_MSEC(10);
constexpr uint8_t kDummyTemplateByte = 0x5a;
constexpr uint16_t kExpectedValidTemplates = 2;

template <typename Fn> int poll_host_cmd(Fn &&cmd_fn)
{
	int status = EC_RES_BUSY;

	for (int i = 0; i < kMaxAsyncPollAttempts; ++i) {
		status = cmd_fn();
		if (status != EC_RES_BUSY) {
			break;
		}
		k_sleep(kAsyncPollDelay);
	}
	return status;
}

void check_thread_watermark(const char *label, const struct k_thread *thread,
			    size_t expected_size)
{
	zassert_not_null(thread, "Missing %s thread", label);

	size_t unused = 0;
	zassert_ok(k_thread_stack_space_get(thread, &unused));

	const size_t size = thread->stack_info.size;
	const size_t used = size - unused;

	printk("%-8s stack: size=%zu used=%zu unused=%zu (min required=%zu)\n",
	       label, size, used, unused, kMinRequiredStackHeadroomBytes);

	zassert_equal(size, expected_size, "Unexpected %s stack size: %zu",
		      label, size);
	zassert_true(used >= kMinExpectedStackUsedBytes,
		     "%s stack usage unexpectedly low: %zu B", label, used);
	zassert_true(unused >= kMinRequiredStackHeadroomBytes,
		     "%s stack headroom too low: %zu B unused of %zu B "
		     "(used %zu B, need >= %zu B unused)",
		     label, unused, size, used, kMinRequiredStackHeadroomBytes);
}

void *setup_suite()
{
	if (IS_ENABLED(CONFIG_OTP_KEY)) {
		std::ranges::copy(default_fake_otp_key,
				  mock_otp.otp_key_buffer);
	}
	return nullptr;
}

} /* namespace */

ZTEST_SUITE(fpsensor_crypto_stack, nullptr, setup_suite, nullptr, nullptr,
	    nullptr);

ZTEST(fpsensor_crypto_stack, test_hostcmd_and_fpsensor_crypto_stack_watermarks)
{
	const struct ec_host_cmd *hc = ec_host_cmd_get_hc();
	zassert_not_null(hc);

	/*
	 * 1. Exercise HOSTCMD task stack with ECDH P-256 key generation,
	 *    ECDH shared-secret derivation, HKDF-SHA256, AES-GCM key wrapping
	 *    / unwrapping, and BoringSSL CTR-DRBG RAND_bytes() calls.
	 */
	struct ec_response_fp_establish_pairing_key_keygen keygen_resp{};
	zassert_ok(
		ec_cmd_fp_establish_pairing_key_keygen(nullptr, &keygen_resp));

	struct ec_params_fp_establish_pairing_key_wrap wrap_params = {
		.peers_pubkey = keygen_resp.pubkey,
	};
	struct ec_response_fp_establish_pairing_key_wrap wrap_resp{};
	zassert_ok(ec_cmd_fp_establish_pairing_key_wrap(nullptr, &wrap_params,
							&wrap_resp));

	ec_params_fp_load_pairing_key load_params = {
		.encrypted_pairing_key = wrap_resp.encrypted_pairing_key,
	};
	zassert_ok(ec_cmd_fp_load_pairing_key(nullptr, &load_params));

	struct ec_response_fp_generate_nonce nonce_resp{};
	zassert_ok(ec_cmd_fp_generate_nonce(nullptr, &nonce_resp));

	/*
	 * Exercise EC_CMD_FP_ESTABLISH_SESSION (HKDF session key derivation +
	 * AES-256-GCM open on HOSTCMD thread). Dummy ciphertext is rejected by
	 * the GCM tag check after full cryptographic execution.
	 */
	struct ec_params_fp_establish_session session_params{};
	zassert_equal(ec_cmd_fp_establish_session(nullptr, &session_params),
		      EC_RES_ERROR);

	/*
	 * 2. Set non-trivial TPM seed and user ID via HOSTCMD, then exercise
	 *    the FPSENSOR task stack with asynchronous template encryption
	 *    (EC_CMD_FP_FRAME v1: FP_FRAME_ENCRYPT_TEMPLATE) and template
	 *    decryption (EC_CMD_FP_TEMPLATE v1: FP_TEMPLATE_DECRYPT).
	 */
	struct ec_params_fp_seed seed_params = {
		.struct_version = FP_TEMPLATE_FORMAT_VERSION,
		.seed = { 1 },
	};
	zassert_ok(ec_cmd_fp_seed(nullptr, &seed_params));

	struct ec_params_fp_context_v1 ctx_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 1 },
	};
	zassert_ok(ec_cmd_fp_context_v1(nullptr, &ctx_params));

	/*
	 * Populate template slot 0 as a newly-enrolled template so that
	 * encrypt_template(0) on the FPSENSOR task thread executes TRNG
	 * positive-match salt generation, HKDF key derivation, and in-place
	 * AES-128-GCM encryption in fp_enc_buffer.
	 */
	std::ranges::fill(fp_template[0], kDummyTemplateByte);
	global_context.templ_valid = 1;
	global_context.template_newly_enrolled = 0;

	struct ec_params_fp_frame_v1 frame_params = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
	};
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &frame_params, nullptr));

	struct ec_fp_template_encryption_metadata enc_metadata{};
	frame_params.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE;
	frame_params.size = sizeof(enc_metadata);
	zassert_ok(poll_host_cmd([&]() {
		return ec_cmd_fp_frame_v1(
			nullptr, &frame_params,
			reinterpret_cast<uint8_t *>(&enc_metadata));
	}));
	zassert_equal(enc_metadata.struct_version, FP_TEMPLATE_FORMAT_VERSION);

	/*
	 * fp_enc_buffer now holds the encrypted template and salt from slot 0.
	 * Trigger asynchronous FP_TEMPLATE_DECRYPT on the FPSENSOR task thread
	 * to decrypt it into slot 1.
	 */
	struct ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	zassert_ok(ec_cmd_fp_template_v1(nullptr, &decrypt_params,
					 sizeof(decrypt_params)));

	zassert_ok(poll_host_cmd([]() {
		struct ec_params_fp_template_v1 result_params = {
			.cmd = FP_TEMPLATE_GET_RESULT,
		};
		return ec_cmd_fp_template_v1(nullptr, &result_params,
					     sizeof(result_params));
	}));
	zassert_equal(global_context.templ_valid, kExpectedValidTemplates);
	zassert_mem_equal(fp_template[0], fp_template[1],
			  sizeof(fp_template[0]));

	/*
	 * 3. Verify stack watermarks on both HOSTCMD and FPSENSOR threads.
	 */
	check_thread_watermark("HOSTCMD", hc->thread, CONFIG_MAIN_STACK_SIZE);
	check_thread_watermark("FPSENSOR", FPSENSOR,
			       CONFIG_TASK_FPSENSOR_STACK_SIZE);
}
