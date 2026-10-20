/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_utils.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "system.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <optional>
#include <span>
#ifdef CONFIG_PLATFORM_EC_WP_EXTERNAL
#include <wp_external.h>
#endif

/*
 * Only the C-ABI boundary in this file (and console debug commands in
 * fpsensor_debug.cc) is permitted to reference global_context. All C++ domain
 * translation units poison global_context at compile time.
 */
FpSensorContext global_context;

int fp_get_next_event(uint8_t *out)
{
	const uint32_t event_out = global_context.events.take().raw;

	std::ranges::copy(
		std::bit_cast<std::array<uint8_t, sizeof(event_out)> >(
			event_out),
		out);

	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_FINGERPRINT, fp_get_next_event);

namespace
{

ec_host_cmd_status fp_command_info(ec_host_cmd_handler_args *args)
{
	const std::span<uint8_t> output(
		static_cast<uint8_t *>(args->output_buf), args->output_buf_max);
	return FpSensorInfoBuffer::populate_response(
		       output, global_context.templates.to_template_info())
		.transform(
			[args](size_t size) { args->output_buf_size = size; })
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_FP_INFO, fp_command_info, EC_VER_MASK(3),
			      ec_response_fp_info_v3);

/* TODO(b/471160577): Remove FP_FRAME v0 after migration is completed. */
ec_host_cmd_status fp_command_frame_v0(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_frame *>(args->input_buf);
	const auto loc = FpFrameV0Offset::unpack(params->offset);
	const uint32_t size = params->size;

	if (size > args->output_buf_max) {
		return EC_HOST_CMD_INVALID_PARAM;
	}

	const std::span<uint8_t> output(
		static_cast<uint8_t *>(args->output_buf), size);

	return global_context.get_frame_v0(loc, output)
		.transform([&] { args->output_buf_size = output.size(); })
		.error_or(EC_HOST_CMD_SUCCESS);
}

/*
 * We are modifying variables in the global_context from this function running
 * in the HOSTCMD task and encrypt_requested_template() running in the FPSENSOR
 * task, without protecting these data.
 *
 * This works because:
 * - We use single-core MCUs
 * - HOSTCMD task has higher priority than FPSENSOR task.
 * - There could be only one host command running at the time.
 * - Host commands cannot be interrupted by other tasks.
 * - We are checking only one bit in global_context.sensor_mode,
 *   so we are not prone to partial (non-atomic) writes/reads.
 *
 * TODO(b/479824917): Unfortunately, lack of any synchronization is a general
 * problem with FPSENSOR architecture. All fingerprint related work should be
 * done by FPSENSOR task, HOSTCMD role should be limited to validating input
 * (if possible), posting work to FPSENSOR task and waiting for result, if
 * needed.
 */
ec_host_cmd_status fp_command_frame_v1(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_frame_v1 *>(args->input_buf);
	if (args->input_buf_size < sizeof(*params)) {
		return EC_HOST_CMD_REQUEST_TRUNCATED;
	}

	const uint32_t size = params->size;

	if (size > args->output_buf_max) {
		return EC_HOST_CMD_INVALID_PARAM;
	}

	const std::span<uint8_t> output(
		static_cast<uint8_t *>(args->output_buf), size);

	return global_context
		.handle_frame_v1(*params, output, args->output_buf_size)
		.error_or(EC_HOST_CMD_SUCCESS);
}

ec_host_cmd_status fp_command_frame(ec_host_cmd_handler_args *args)
{
	if (args->version == 1) {
		return fp_command_frame_v1(args);
	}

	return fp_command_frame_v0(args);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_FRAME, fp_command_frame,
			     EC_VER_MASK(0) | EC_VER_MASK(1),
			     SMALLEST_TYPE(ec_params_fp_frame,
					   ec_params_fp_frame_v1));

ec_host_cmd_status fp_command_stats(ec_host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_stats *>(args->output_buf);

	*r = global_context.stats.to_response();
	args->output_buf_size = sizeof(*r);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_FP_STATS, fp_command_stats, EC_VER_MASK(0),
			      ec_response_fp_stats);

ec_host_cmd_status fp_command_template_v0(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_template *>(args->input_buf);
	const auto [size, xfer_complete] =
		FpTemplateV0Size::unpack(params->size);
	const uint32_t offset = params->offset;

	if (args->input_buf_size !=
	    size + offsetof(ec_params_fp_template, data)) {
		return EC_HOST_CMD_INVALID_PARAM;
	}

	return global_context
		.load_template_chunk(offset, std::span(params->data, size),
				     xfer_complete)
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}

ec_host_cmd_status fp_command_template_v1(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_template_v1 *>(args->input_buf);
	if (args->input_buf_size < sizeof(*params)) {
		return EC_HOST_CMD_REQUEST_TRUNCATED;
	}

	return global_context.handle_template_v1(*params, args->input_buf_size)
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}

ec_host_cmd_status fp_command_template(ec_host_cmd_handler_args *args)
{
	if (args->version == 1) {
		return fp_command_template_v1(args);
	}

	return fp_command_template_v0(args);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_TEMPLATE, fp_command_template,
			     EC_VER_MASK(0) | EC_VER_MASK(1),
			     SMALLEST_TYPE(ec_params_fp_template,
					   ec_params_fp_template_v1));

ec_host_cmd_status fp_command_confirm_template(ec_host_cmd_handler_args *args)
{
	const auto *params = static_cast<const ec_params_fp_confirm_template *>(
		args->input_buf);

	return global_context.confirm_template(params->mac)
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_CONFIRM_TEMPLATE,
			     fp_command_confirm_template, EC_VER_MASK(0),
			     ec_params_fp_confirm_template);

ec_host_cmd_status fp_command_tpm_seed(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_seed *>(args->input_buf);

	return global_context.crypto.tpm_seed.set_from_host(*params)
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_SEED, fp_command_tpm_seed,
			     EC_VER_MASK(0), ec_params_fp_seed);

ec_host_cmd_status fp_command_encryption_status(ec_host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_encryption_status *>(
		args->output_buf);

	r->valid_flags = FP_ENC_STATUS_SEED_SET;
	r->status = global_context.crypto.encryption_status();
	args->output_buf_size = sizeof(*r);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_FP_ENC_STATUS,
			      fp_command_encryption_status, EC_VER_MASK(0),
			      ec_response_fp_encryption_status);

ec_host_cmd_status fp_command_mode(ec_host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_mode *>(args->input_buf);
	auto *r = static_cast<ec_response_fp_mode *>(args->output_buf);
	std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac =
		std::nullopt;

	if (args->version == 1) {
		const auto *p_1 = static_cast<const ec_params_fp_mode_v1 *>(
			args->input_buf);
		if (args->input_buf_size < sizeof(*p_1)) {
			return EC_HOST_CMD_REQUEST_TRUNCATED;
		}
		mac = std::span{ p_1->mac };
	}

	return global_context.set_sensor_mode(p->mode, mac)
		.transform([args, r](FpSensorMode mode) {
			r->mode = mode.raw();
			args->output_buf_size = sizeof(*r);
		})
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER(EC_CMD_FP_MODE, fp_command_mode,
		    EC_VER_MASK(0) | EC_VER_MASK(1),
		    SMALLEST_TYPE(ec_params_fp_mode, ec_params_fp_mode_v1),
		    ec_response_fp_mode);

ec_host_cmd_status fp_command_context(ec_host_cmd_handler_args *args)
{
	const auto *p =
		static_cast<const ec_params_fp_context_v1 *>(args->input_buf);

	return global_context.set_context(*p)
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_CONTEXT, fp_command_context,
			     EC_VER_MASK(1), ec_params_fp_context_v1);

ec_host_cmd_status fp_command_read_match_secret(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_read_match_secret *>(
			args->input_buf);
	auto *response = static_cast<ec_response_fp_read_match_secret *>(
		args->output_buf);

	return global_context.read_match_secret(params->fgr)
		.transform([args,
			    response](const FpPositiveMatchSecret &secret) {
			std::ranges::copy(secret.bytes(),
					  response->positive_match_secret);
			args->output_buf_size = sizeof(*response);
		})
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER(EC_CMD_FP_READ_MATCH_SECRET, fp_command_read_match_secret,
		    EC_VER_MASK(0), ec_params_fp_read_match_secret,
		    ec_response_fp_read_match_secret);

ec_host_cmd_status fp_command_sign_match(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_sign_match *>(args->input_buf);
	auto *response =
		static_cast<ec_response_fp_sign_match *>(args->output_buf);

	return global_context.sign_match(params->challenge)
		.transform([args, response](const auto &signature) {
			std::ranges::copy(signature, response->signature);
			args->output_buf_size = sizeof(*response);
		})
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER(EC_CMD_FP_SIGN_MATCH, fp_command_sign_match, EC_VER_MASK(0),
		    ec_params_fp_sign_match, ec_response_fp_sign_match);

ec_host_cmd_status fp_command_generate_nonce(ec_host_cmd_handler_args *args)
{
	auto *r =
		static_cast<ec_response_fp_generate_nonce *>(args->output_buf);

	global_context.crypto.generate_nonce(r->nonce);

	args->output_buf_size = sizeof(*r);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_FP_GENERATE_NONCE,
			      fp_command_generate_nonce, EC_VER_MASK(0),
			      ec_response_fp_generate_nonce);

ec_host_cmd_status fp_command_establish_session(ec_host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_establish_session *>(
		args->input_buf);

	return global_context.establish_session(*p)
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_ESTABLISH_SESSION,
			     fp_command_establish_session, EC_VER_MASK(0),
			     ec_params_fp_establish_session);

ec_host_cmd_status fp_cmd_generate_challenge(ec_host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_challenge *>(
		args->output_buf);

	return global_context.crypto.generate_challenge(r->challenge)
		.transform([&] { args->output_buf_size = sizeof(*r); })
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_FP_GENERATE_CHALLENGE,
			      fp_cmd_generate_challenge, EC_VER_MASK(0),
			      ec_response_fp_generate_challenge);

#ifdef CONFIG_PLATFORM_EC_WP_EXTERNAL
ec_host_cmd_status fp_command_unlock_dev_options(ec_host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_unlock_dev_options *>(
		args->input_buf);

	/* Validate with an empty context (no user_id) and unlock the system. */
	return global_context.crypto
		.validate_request({}, "enable_dev_options", p->hmac)
		.transform([] { disable_write_protect_external(); })
		.transform_error(
			[](ec_error_list) { return EC_HOST_CMD_ACCESS_DENIED; })
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_UNLOCK_DEV_OPTIONS,
			     fp_command_unlock_dev_options, EC_VER_MASK(0),
			     ec_params_fp_unlock_dev_options);
#endif

#if !defined(CONFIG_ZEPHYR) || \
	defined(CONFIG_PLATFORM_EC_FINGERPRINT_AUTH_METHOD_HANDSHAKE)
ec_host_cmd_status
fp_command_establish_pairing_key_keygen(ec_host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_establish_pairing_key_keygen *>(
		args->output_buf);

	return global_context.crypto.establish_pairing_key_keygen()
		.transform([args, r](const auto &pubkey) {
			r->pubkey = pubkey;
			args->output_buf_size = sizeof(*r);
		})
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN,
			      fp_command_establish_pairing_key_keygen,
			      EC_VER_MASK(0),
			      ec_response_fp_establish_pairing_key_keygen);

ec_host_cmd_status
fp_command_establish_pairing_key_wrap(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_establish_pairing_key_wrap *>(
			args->input_buf);
	auto *r = static_cast<ec_response_fp_establish_pairing_key_wrap *>(
		args->output_buf);

	return global_context.crypto
		.establish_pairing_key_wrap(params->peers_pubkey)
		.transform([args, r](const auto &encrypted_pairing_key) {
			r->encrypted_pairing_key = encrypted_pairing_key;
			args->output_buf_size = sizeof(*r);
		})
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP,
		    fp_command_establish_pairing_key_wrap, EC_VER_MASK(0),
		    ec_params_fp_establish_pairing_key_wrap,
		    ec_response_fp_establish_pairing_key_wrap);

ec_host_cmd_status fp_command_load_pairing_key(ec_host_cmd_handler_args *args)
{
	const auto *params = static_cast<const ec_params_fp_load_pairing_key *>(
		args->input_buf);

	return global_context.crypto
		.load_pairing_key(params->encrypted_pairing_key)
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_LOAD_PAIRING_KEY,
			     fp_command_load_pairing_key, EC_VER_MASK(0),
			     ec_params_fp_load_pairing_key);
#endif

#ifdef CONFIG_PLATFORM_EC_FINGERPRINT_AUTH_METHOD_ASCP
ec_host_cmd_status fp_command_ascp_establish(ec_host_cmd_handler_args *args)
{
	const auto *params = static_cast<const ec_params_fp_ascp_establish *>(
		args->input_buf);

	return global_context.crypto.establish_ascp_pairing_key(params->pk_g)
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_ASCP_ESTABLISH,
			     fp_command_ascp_establish, EC_VER_MASK(0),
			     ec_params_fp_ascp_establish);
#endif

} /* namespace */

#if !defined(CONFIG_ZEPHYR) && defined(TEST_BUILD)
int fp_tpm_seed_is_set()
{
	return global_context.crypto.tpm_seed.is_set();
}

void fp_reset_context()
{
	global_context.reset_context();
}

void fp_reset_and_clear_context()
{
	global_context.reset_and_clear_context();
}

enum ec_status
fp_set_sensor_mode(uint32_t mode, uint32_t *mode_output,
		   std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac)
{
	if (mode_output == nullptr) {
		return EC_RES_INVALID_PARAM;
	}
	return global_context.set_sensor_mode(mode, mac)
		.transform([mode_output](FpSensorMode new_mode) {
			*mode_output = new_mode.raw();
		})
		.error_or(EC_RES_SUCCESS);
}

void reset_session()
{
	global_context.crypto.reset_session();
}

enum ec_error_list
validate_request(std::span<const uint8_t> context,
		 std::span<const uint8_t> operation,
		 std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac)
{
	const std::string_view op_view(
		reinterpret_cast<const char *>(operation.data()),
		operation.size());
	return global_context.crypto.validate_request(context, op_view, mac)
		.error_or(EC_SUCCESS);
}

enum ec_error_list
sign_message(std::span<const uint8_t> context,
	     std::span<const uint8_t> operation,
	     std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge,
	     std::span<uint8_t, SHA256_DIGEST_LENGTH> output)
{
	const std::string_view op_view(
		reinterpret_cast<const char *>(operation.data()),
		operation.size());
	return global_context.crypto
		.sign_message(context, op_view, peer_challenge)
		.transform([output](const auto &sig) {
			std::ranges::copy(sig, output.begin());
		})
		.error_or(EC_SUCCESS);
}
#endif
