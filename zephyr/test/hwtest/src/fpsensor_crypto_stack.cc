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
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <span>

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

constexpr uint8_t kHostCmdProtocolVersion = 3;
constexpr k_timeout_t kHostCmdTimeout = K_SECONDS(5);
constexpr int kMaxAsyncPollAttempts = 100;
constexpr k_timeout_t kAsyncPollDelay = K_MSEC(10);
constexpr uint8_t kDummyTemplateByte = 0x5a;
constexpr uint16_t kExpectedValidTemplates = 2;

struct ec_host_cmd *hc;
const struct ec_host_cmd_backend *orig_backend;
struct k_sem hc_send;

int hc_backend_send(const struct ec_host_cmd_backend *backend)
{
	ARG_UNUSED(backend);
	k_sem_give(&hc_send);
	return 0;
}

struct ec_host_cmd_backend_api hc_api = {
	.init = nullptr,
	.send = hc_backend_send,
};

struct ec_host_cmd_backend hc_backend = {
	.api = &hc_api,
	.ctx = nullptr,
};

constexpr fp_elliptic_curve_public_key kTestPeerPubKey = {
	.x = {
		0x85, 0xad, 0x35, 0x23, 0x05, 0x1e, 0x33, 0x3f,
		0xca, 0xa7, 0xea, 0xa5, 0x88, 0x33, 0x12, 0x95,
		0xa7, 0xb5, 0x98, 0x9f, 0x32, 0xef, 0x7d, 0xe9,
		0xf8, 0x70, 0x14, 0x5e, 0x89, 0xcb, 0xde, 0x1f,
	},
	.y = {
		0xd1, 0xdc, 0x91, 0xc6, 0xe6, 0x5b, 0x1e, 0x3c,
		0x01, 0x6c, 0xe6, 0x50, 0x25, 0x5d, 0x89, 0xcf,
		0xb7, 0x8d, 0x88, 0xb9, 0x0d, 0x09, 0x41, 0xf1,
		0x09, 0x4f, 0x61, 0x55, 0x6c, 0xc4, 0x96, 0x6b,
	},
};

constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> kFakeTpmSeed = {
	0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60, 0xf8, 0x5a, 0xa0,
	0xa6, 0x2c, 0xb3, 0xf5, 0xe2, 0xeb, 0xb9, 0xd8, 0x2f, 0xb5, 0x78,
	0x5c, 0x79, 0x82, 0xce, 0x06, 0x3f, 0xcc, 0x23, 0xb9, 0xe7,
};

constexpr std::array<uint32_t, FP_CONTEXT_USERID_WORDS> kFakeUserId = {
	0x5f6d5f69, 0x61665f61, 0x755f656b, 0x5f726573,
	0x755f6469, 0x5f646573, 0x5f726f66, 0x74736574,
};

uint8_t cal_checksum(std::span<const uint8_t> buffer)
{
	const uint8_t sum =
		std::accumulate(buffer.begin(), buffer.end(), uint8_t{ 0 });
	return static_cast<uint8_t>(-sum);
}

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

void log_thread_watermark(const char *label, const struct k_thread *thread)
{
	size_t unused = 0;

	if (thread != nullptr &&
	    k_thread_stack_space_get(thread, &unused) == 0) {
		size_t size = thread->stack_info.size;
		size_t used = size - unused;

		printk("  thread %-16s stack_size=%5zu used=%5zu unused=%5zu\n",
		       label, size, used, unused);
	}
}

void log_all_thread_watermarks(const struct k_thread *hostcmd_thread,
			       const struct k_thread *fpsensor_thread)
{
	log_thread_watermark("HOSTCMD", hostcmd_thread);
	log_thread_watermark("FPSENSOR", fpsensor_thread);
	log_thread_watermark("sysworkq", k_sys_work_q.thread_id);
	log_thread_watermark("ztest", k_current_get());
}

void *setup_suite()
{
	if (IS_ENABLED(CONFIG_OTP_KEY)) {
		std::ranges::copy(default_fake_otp_key,
				  mock_otp.otp_key_buffer);
	}

	hc = const_cast<struct ec_host_cmd *>(ec_host_cmd_get_hc());
	zassert_not_null(hc);
	zassert_not_null(hc->thread);

	k_sem_init(&hc_send, 0, 1);
	orig_backend = hc->backend;
	hc->backend = &hc_backend;

	return nullptr;
}

void teardown_suite(void *data)
{
	ARG_UNUSED(data);
	if (hc != nullptr && orig_backend != nullptr) {
		hc->backend =
			const_cast<struct ec_host_cmd_backend *>(orig_backend);
	}
}

} /* namespace */

extern "C" uint16_t host_command_process(struct host_cmd_handler_args *args)
{
	struct ec_host_cmd_request_header header = {
		.prtcl_ver = kHostCmdProtocolVersion,
		.checksum = 0,
		.cmd_id = args->command,
		.cmd_ver = static_cast<uint8_t>(args->version),
		.reserved = 0,
		.data_len = args->input_buf_size,
	};
	const size_t total_req_size = sizeof(header) + args->input_buf_size;

	zassert_true(total_req_size <= hc->rx_ctx.len_max,
		     "Request too large: %zu > %zu", total_req_size,
		     hc->rx_ctx.len_max);

	std::memcpy(hc->rx_ctx.buf, &header, sizeof(header));
	if (args->input_buf_size > 0 && args->input_buf != nullptr) {
		std::memcpy(hc->rx_ctx.buf + sizeof(header), args->input_buf,
			    args->input_buf_size);
	}

	auto *req_hdr = reinterpret_cast<struct ec_host_cmd_request_header *>(
		hc->rx_ctx.buf);
	req_hdr->checksum = cal_checksum({ hc->rx_ctx.buf, total_req_size });
	hc->rx_ctx.len = total_req_size;

	k_sem_reset(&hc_send);
	ec_host_cmd_rx_notify();

	int rv = k_sem_take(&hc_send, kHostCmdTimeout);
	zassert_ok(rv, "Timed out waiting for host command 0x%04x",
		   args->command);

	const auto *resp_hdr =
		reinterpret_cast<const struct ec_host_cmd_response_header *>(
			hc->tx.buf);
	if (resp_hdr->result == EC_HOST_CMD_SUCCESS &&
	    args->output_buf != nullptr && args->output_buf_max > 0) {
		zassert_equal(
			resp_hdr->data_len, args->output_buf_max,
			"Unexpected response size for cmd 0x%04x: %u != %u",
			args->command, resp_hdr->data_len,
			args->output_buf_max);
		args->output_buf_size = resp_hdr->data_len;
		std::memcpy(args->output_buf,
			    static_cast<const uint8_t *>(hc->tx.buf) +
				    sizeof(*resp_hdr),
			    args->output_buf_size);
	}

	return resp_hdr->result;
}

ZTEST_SUITE(fpsensor_crypto_stack, nullptr, setup_suite, nullptr, nullptr,
	    teardown_suite);

ZTEST(fpsensor_crypto_stack, test_hostcmd_and_fpsensor_crypto_stack_watermarks)
{
	const struct k_thread *hostcmd_thread = hc->thread;
	const struct k_thread *fpsensor_thread = FPSENSOR;
	zassert_not_null(fpsensor_thread, "Failed to find FPSENSOR thread");

	printk("Stack watermarks before fingerprint crypto operations:\n");
	log_all_thread_watermarks(hostcmd_thread, fpsensor_thread);

	/*
	 * 1. Exercise HOSTCMD task stack with ECDH P-256 key generation,
	 *    ECDH shared-secret derivation, HKDF-SHA256, AES-GCM key wrapping
	 *    / unwrapping, and BoringSSL CTR-DRBG RAND_bytes() calls.
	 */
	struct ec_response_fp_establish_pairing_key_keygen keygen_resp{};
	zassert_ok(
		ec_cmd_fp_establish_pairing_key_keygen(nullptr, &keygen_resp));

	struct ec_params_fp_establish_pairing_key_wrap wrap_params = {
		.peers_pubkey = kTestPeerPubKey,
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
	 * 2. Set TPM seed and user ID via HOSTCMD, then exercise the FPSENSOR
	 *    task stack with asynchronous template encryption
	 *    (EC_CMD_FP_FRAME v1: FP_FRAME_ENCRYPT_TEMPLATE) and template
	 *    decryption (EC_CMD_FP_TEMPLATE v1: FP_TEMPLATE_DECRYPT).
	 */
	struct ec_params_fp_seed seed_params = {
		.struct_version = FP_TEMPLATE_FORMAT_VERSION,
		.reserved = 0,
	};
	std::ranges::copy(kFakeTpmSeed, seed_params.seed);
	zassert_ok(ec_cmd_fp_seed(nullptr, &seed_params));

	struct ec_params_fp_context_v1 ctx_params = {
		.action = FP_CONTEXT_ASYNC,
	};
	std::ranges::copy(kFakeUserId, ctx_params.userid);
	zassert_ok(ec_cmd_fp_context_v1(nullptr, &ctx_params));

	ctx_params.action = FP_CONTEXT_GET_RESULT;
	zassert_ok(poll_host_cmd(
		[&]() { return ec_cmd_fp_context_v1(nullptr, &ctx_params); }));

	/*
	 * Populate template slot 0 as a newly-enrolled template so that
	 * encrypt_template(0) on the FPSENSOR task thread executes TRNG
	 * positive-match salt generation, HKDF key derivation, and in-place
	 * AES-128-GCM encryption.
	 */
	std::ranges::fill(fp_template[0], kDummyTemplateByte);
	global_context.templ_valid = 1;
	global_context.template_newly_enrolled = 0;

	struct ec_params_fp_frame_v1 frame_params = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
		.offset = 0,
		.size = 0,
	};
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &frame_params, nullptr));

	struct ec_fp_template_encryption_metadata enc_metadata{};
	frame_params.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE;
	frame_params.offset = 0;
	frame_params.size = sizeof(enc_metadata);
	zassert_ok(poll_host_cmd([&]() {
		return ec_cmd_fp_frame_v1(
			nullptr, &frame_params,
			reinterpret_cast<uint8_t *>(&enc_metadata));
	}));
	zassert_equal(enc_metadata.struct_version, FP_TEMPLATE_FORMAT_VERSION);

	/*
	 * Now fp_enc_buffer contains the valid encrypted template and salt for
	 * slot 0. Send FP_TEMPLATE_LOAD for the metadata header and trigger
	 * asynchronous FP_TEMPLATE_DECRYPT on the FPSENSOR task thread to
	 * decrypt it into slot 1.
	 */
	uint8_t load_buf[offsetof(struct ec_params_fp_template_v1, data) +
			 sizeof(enc_metadata)]{};
	auto *tpl_params =
		reinterpret_cast<struct ec_params_fp_template_v1 *>(load_buf);
	tpl_params->offset = 0;
	tpl_params->size = sizeof(enc_metadata);
	tpl_params->cmd = FP_TEMPLATE_LOAD;
	std::memcpy(tpl_params->data, &enc_metadata, sizeof(enc_metadata));

	zassert_ok(
		ec_cmd_fp_template_v1(nullptr, tpl_params, sizeof(load_buf)));

	tpl_params->offset = 0;
	tpl_params->size = 0;
	tpl_params->cmd = FP_TEMPLATE_DECRYPT;
	constexpr int kEmptyTemplateParamsSize =
		offsetof(struct ec_params_fp_template_v1, data);
	zassert_ok(ec_cmd_fp_template_v1(nullptr, tpl_params,
					 kEmptyTemplateParamsSize));

	tpl_params->cmd = FP_TEMPLATE_GET_RESULT;
	zassert_ok(poll_host_cmd([&]() {
		return ec_cmd_fp_template_v1(nullptr, tpl_params,
					     kEmptyTemplateParamsSize);
	}));
	zassert_equal(global_context.templ_valid, kExpectedValidTemplates);
	zassert_mem_equal(fp_template[0], fp_template[1],
			  sizeof(fp_template[0]));

	printk("Stack watermarks after fingerprint crypto operations:\n");
	log_all_thread_watermarks(hostcmd_thread, fpsensor_thread);

	/*
	 * 3. Verify stack watermarks on both HOSTCMD and FPSENSOR threads.
	 */
	size_t hostcmd_unused = 0;
	zassert_ok(k_thread_stack_space_get(hostcmd_thread, &hostcmd_unused));
	size_t hostcmd_size = hostcmd_thread->stack_info.size;
	size_t hostcmd_used = hostcmd_size - hostcmd_unused;

	size_t fpsensor_unused = 0;
	zassert_ok(k_thread_stack_space_get(fpsensor_thread, &fpsensor_unused));
	size_t fpsensor_size = fpsensor_thread->stack_info.size;
	size_t fpsensor_used = fpsensor_size - fpsensor_unused;

	printk("HOSTCMD  stack: size=%zu used=%zu unused=%zu (min required=%zu)\n",
	       hostcmd_size, hostcmd_used, hostcmd_unused,
	       kMinRequiredStackHeadroomBytes);
	printk("FPSENSOR stack: size=%zu used=%zu unused=%zu (min required=%zu)\n",
	       fpsensor_size, fpsensor_used, fpsensor_unused,
	       kMinRequiredStackHeadroomBytes);

	zassert_equal(hostcmd_size, CONFIG_MAIN_STACK_SIZE);
	zassert_equal(fpsensor_size, CONFIG_TASK_FPSENSOR_STACK_SIZE);

	zassert_true(hostcmd_used >= kMinExpectedStackUsedBytes,
		     "HOSTCMD stack usage unexpectedly low: %zu B",
		     hostcmd_used);
	zassert_true(fpsensor_used >= kMinExpectedStackUsedBytes,
		     "FPSENSOR stack usage unexpectedly low: %zu B",
		     fpsensor_used);

	zassert_true(hostcmd_unused >= kMinRequiredStackHeadroomBytes,
		     "HOSTCMD stack headroom too low: %zu B unused of %zu B "
		     "(used %zu B, need >= %zu B unused)",
		     hostcmd_unused, hostcmd_size, hostcmd_used,
		     kMinRequiredStackHeadroomBytes);
	zassert_true(fpsensor_unused >= kMinRequiredStackHeadroomBytes,
		     "FPSENSOR stack headroom too low: %zu B unused of %zu B "
		     "(used %zu B, need >= %zu B unused)",
		     fpsensor_unused, fpsensor_size, fpsensor_used,
		     kMinRequiredStackHeadroomBytes);
}
