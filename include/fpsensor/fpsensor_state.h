/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor context and telemetry state */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_

#include "atomic.h"
#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_crypto_context.h"
#include "fpsensor/fpsensor_frame_storage.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_template_store.h"
#include "fpsensor/fpsensor_utils.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

struct FpSensorStats {
	using Duration = std::chrono::duration<uint32_t, std::micro>;

	Duration capture_time{};
	Duration matching_time{};
	Duration overall_time{};
	FpTimestamp overall_t0{};
	bool capture_invalid = false;
	bool matching_invalid = false;
	/*
	 * Last matched template index, persisted for telemetry
	 * (EC_CMD_FP_STATS). Unlike positive_match_secret_state, this is not
	 * cleared when the secret is read.
	 */
	std::optional<FpTemplateId> template_matched = std::nullopt;

	[[nodiscard]] static Duration
	elapsed_since(FpTimestamp start,
		      FpTimestamp now = FpClock::now()) noexcept
	{
		return std::chrono::duration_cast<Duration>(now - start);
	}

	void start_overall(FpTimestamp now = FpClock::now()) noexcept
	{
		overall_t0 = now;
	}

	void finish_overall(FpTimestamp now = FpClock::now()) noexcept
	{
		overall_time = elapsed_since(overall_t0, now);
	}

	void record_capture(FpTimestamp start, bool success) noexcept
	{
		capture_time = elapsed_since(start);
		if (!success) {
			capture_invalid = true;
		}
	}

	void record_match(FpTimestamp start,
			  std::optional<FpTemplateId> matched,
			  bool valid) noexcept
	{
		capture_invalid = false;
		matching_invalid = !valid;
		template_matched = matched;
		matching_time = elapsed_since(start);
	}

	[[nodiscard]] constexpr ec_response_fp_stats to_response() const noexcept
	{
		const uint64_t t0_us = static_cast<uint64_t>(
			overall_t0.time_since_epoch().count());
		const uint8_t timestamps_invalid =
			(capture_invalid ? FPSTATS_CAPTURE_INV : 0) |
			(matching_invalid ? FPSTATS_MATCHING_INV : 0);
		return {
        .capture_time_us = capture_time.count(),
        .matching_time_us = matching_time.count(),
        .overall_time_us = overall_time.count(),
        .overall_t0 =
            {
                .lo = static_cast<uint32_t>(t0_us),
                .hi = static_cast<uint32_t>(t0_us >> 32),
            },
        .timestamps_invalid = timestamps_invalid,
        .template_matched =
            static_cast<int8_t>(template_matched.transform(&FpTemplateId::raw)
                                    .value_or(kFpNoSuchTemplate)),
    };
	}
};

class FpMkbpEvents {
    public:
	void add(FpMkbpEvent evt) noexcept
	{
		atomic_or(&raw_, evt.raw);
	}

	[[nodiscard]] FpMkbpEvent take() noexcept
	{
		return FpMkbpEvent{ static_cast<uint32_t>(
			atomic_clear(&raw_)) };
	}

	[[nodiscard]] FpMkbpEvent peek() const noexcept
	{
		return FpMkbpEvent{ static_cast<uint32_t>(atomic_get(&raw_)) };
	}

    private:
	atomic_t raw_ = 0;
};

struct FpSensorContext {
	FpTemplateStore templates{};
	FpCryptoContext crypto{};
	FpFrameStorage frame{};
	FpSensorStats stats{};
	FpMkbpEvents events{};
	FpSensorMode sensor_mode{};

	constexpr void request_template_encryption(FpTemplateId id) noexcept
	{
		crypto.encrypted_template_ready = false;
		templates.template_encrypted_id = id;
	}

	constexpr void clear_encrypted_template() noexcept
	{
		crypto.encrypted_template_ready = false;
		templates.template_encrypted_id.reset();
	}

	void reset_context() noexcept;
	void clear_context() noexcept;
	void reset_and_clear_context();
	void encrypt_requested_template();
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	get_encrypted_template_v0(uint16_t idx, uint32_t offset,
				  std::span<uint8_t> output);
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	get_frame_v0(FpFrameV0Offset loc, std::span<uint8_t> output);
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	start_template_encryption(uint16_t idx);
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	get_encrypted_template_chunk(uint32_t offset,
				     std::span<uint8_t> output) const;
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	handle_frame_v1(const ec_params_fp_frame_v1 &params,
			std::span<uint8_t> output, uint16_t &output_buf_size);
	[[nodiscard]] std::expected<void, ec_status>
	load_template_chunk(uint32_t offset, std::span<const uint8_t> chunk,
			    bool commit = false);
	[[nodiscard]] std::expected<void, ec_status> commit_template();
	[[nodiscard]] std::expected<void, ec_status>
	start_template_decryption();
	[[nodiscard]] ec_status get_template_decryption_result() const noexcept;
	[[nodiscard]] std::expected<void, ec_status>
	handle_template_v1(const ec_params_fp_template_v1 &params,
			   uint32_t params_size);
	void decrypt_and_commit_template();
	[[nodiscard]] std::expected<void, ec_status>
	confirm_template(std::span<const uint8_t, FP_MAC_LENGTH> mac);
	[[nodiscard]] std::expected<FpSensorMode, ec_status> set_sensor_mode(
		uint32_t mode,
		std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac =
			std::nullopt);
	[[nodiscard]] std::expected<void, ec_status>
	set_context(const ec_params_fp_context_v1 &p);
	[[nodiscard]] std::expected<void, ec_status>
	establish_session(const ec_params_fp_establish_session &p);
	[[nodiscard]] std::expected<std::array<uint8_t, FP_MAC_LENGTH>,
				    ec_status>
	sign_match(std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge);
	[[nodiscard]] std::expected<FpPositiveMatchSecret, ec_status>
	read_match_secret(int8_t fgr);
	[[nodiscard]] std::expected<void, ec_error_list>
	wait_for_mode_clear(uint32_t mask,
			    std::chrono::microseconds poll_interval,
			    int max_tries) const;
	[[nodiscard]] std::expected<void, ec_error_list>
	run_console_capture(uint32_t mode);
	[[nodiscard]] std::expected<void, ec_error_list>
	console_capture(FpCaptureType capture_type);
	[[nodiscard]] std::expected<void, ec_error_list> console_enroll();
	[[nodiscard]] std::expected<void, ec_error_list> console_match();
	[[nodiscard]] std::expected<void, ec_error_list> console_clear();
	[[nodiscard]] std::expected<void, ec_error_list> console_maintenance();

#if !defined(CONFIG_ZEPHYR) && defined(TEST_BUILD)
	struct LegacyTemplateNewlyEnrolledProxy {
		std::optional<FpTemplateId> *target;

		constexpr LegacyTemplateNewlyEnrolledProxy &
		operator=(int32_t val) noexcept
		{
			if (const auto id = FpTemplateId::create(val);
			    id.has_value()) {
				*target = *id;
			} else {
				target->reset();
			}
			return *this;
		}

		constexpr operator uint16_t() const noexcept
		{
			return target->has_value() ? (*target)->raw() :
						     kFpNoSuchTemplate;
		}
	};

	struct LegacyEncryptionStatusProxy {
		FpCryptoContext *crypto;

		constexpr operator uint32_t() const noexcept
		{
			return crypto->encryption_status();
		}

		constexpr LegacyEncryptionStatusProxy &
		operator|=(uint32_t flags) noexcept
		{
			if ((flags & FP_CONTEXT_SESSION_NONCE_SET) &&
			    !crypto->session_nonce.has_value()) {
				crypto->session_nonce.emplace();
			}
			if (flags & FP_CONTEXT_STATUS_SESSION_ESTABLISHED) {
				crypto->set_auth_enabled(true);
			}
			if (flags & FP_ENCRYPTED_TEMPLATE_READY) {
				crypto->encrypted_template_ready = true;
			}
			return *this;
		}
	};

	using LegacyPositiveMatchSaltArray =
		uint8_t[FP_MAX_FINGER_COUNT][FP_POSITIVE_MATCH_SALT_BYTES];

	uint16_t &templ_valid = templates.templ_valid;
	FpPositiveMatchSecretState &positive_match_secret_state =
		templates.positive_match_secret_state;
	LegacyPositiveMatchSaltArray &fp_positive_match_salt =
		*reinterpret_cast<LegacyPositiveMatchSaltArray *>(
			templates.fp_positive_match_salt.data());
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> &user_id =
		crypto.user_id.value;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> &tpm_seed =
		crypto.tpm_seed.value;
	LegacyTemplateNewlyEnrolledProxy template_newly_enrolled{
		&templates.template_newly_enrolled
	};
	LegacyEncryptionStatusProxy fp_encryption_status{ &crypto };
#endif
};

#if !defined(CONFIG_ZEPHYR) && defined(TEST_BUILD)
using fpsensor_context = FpSensorContext;
extern FpSensorContext global_context;

int fp_tpm_seed_is_set();
void fp_reset_context();
void fp_reset_and_clear_context();
enum ec_status
fp_set_sensor_mode(uint32_t mode, uint32_t *mode_output,
		   std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac);
#endif

/* Simulation for unit tests. */
extern "C" __test_only void fp_task_simulate();

/*
 * Get the next FP event.
 *
 * @param out the pointer to the output event.
 */
extern "C" int fp_get_next_event(uint8_t *out);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_ */
