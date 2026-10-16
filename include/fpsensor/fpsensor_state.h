/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_

#include "atomic.h"
#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_utils.h"
#include "fpsensor_driver.h"
#include "fpsensor_image_frame_params.h"
#include "fpsensor_matcher.h"
#include "fpsensor_state_driver.h"
#include "link_defs.h"
#include "openssl/ec.h"

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

/* if no special memory regions are defined, fallback on regular SRAM */
#ifndef FP_FRAME_SECTION
#define FP_FRAME_SECTION
#endif
#ifndef FP_TEMPLATE_SECTION
#define FP_TEMPLATE_SECTION
#endif

#define FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE                         \
	(FP_ALGORITHM_TEMPLATE_SIZE + FP_POSITIVE_MATCH_SALT_BYTES + \
	 sizeof(struct ec_fp_template_encryption_metadata))

using FpTemplate = std::array<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE>;
using FpPositiveMatchSalt = std::array<uint8_t, FP_POSITIVE_MATCH_SALT_BYTES>;

/**
 * Validated fingerprint template index in [0, FP_MAX_FINGER_COUNT).
 */
class FpTemplateId {
    public:
	static constexpr std::expected<FpTemplateId, ec_status>
	create(std::integral auto idx) noexcept
	{
		if (std::cmp_less(idx, 0) ||
		    std::cmp_greater_equal(idx, FP_MAX_FINGER_COUNT)) {
			return std::unexpected(EC_RES_INVALID_PARAM);
		}
		return FpTemplateId(static_cast<uint16_t>(idx));
	}

	static constexpr std::expected<FpTemplateId, ec_status>
	create_enrolled(std::integral auto idx, uint16_t templ_valid) noexcept
	{
		return create(idx).and_then(
			[templ_valid](FpTemplateId id)
				-> std::expected<FpTemplateId, ec_status> {
				if (id.raw() >= templ_valid) {
					return std::unexpected(
						EC_RES_UNAVAILABLE);
				}
				return id;
			});
	}

	[[nodiscard]] constexpr uint16_t raw() const noexcept
	{
		return id_;
	}

	constexpr bool
	operator==(const FpTemplateId &) const noexcept = default;

    private:
	explicit constexpr FpTemplateId(uint16_t id) noexcept : id_(id)
	{
	}

	uint16_t id_ = 0;
};

void cleanse(std::span<uint8_t> bytes) noexcept;

struct enc_buffer {
	struct ec_fp_template_encryption_metadata metadata{};
	FpTemplate fp_template{};
	FpPositiveMatchSalt positive_match_salt{};

	[[nodiscard]] auto as_bytes(this auto &self) noexcept
	{
		using Byte = std::conditional_t<
			std::is_const_v<std::remove_reference_t<decltype(self)> >,
			const uint8_t, uint8_t>;
		return std::span<Byte, FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE>(
			reinterpret_cast<Byte *>(&self), sizeof(self));
	}

	[[nodiscard]] auto payload(this auto &self) noexcept
	{
		return self.as_bytes()
			.template subspan<sizeof(metadata),
					  FP_ALGORITHM_TEMPLATE_SIZE +
						  FP_POSITIVE_MATCH_SALT_BYTES>();
	}

	[[nodiscard]] bool is_zeroed() const noexcept
	{
		return std::ranges::none_of(as_bytes(), std::identity{});
	}

	[[nodiscard]] constexpr bool has_valid_metadata() const noexcept
	{
		return metadata.struct_version == FP_TEMPLATE_FORMAT_VERSION;
	}

	[[nodiscard]] bool has_nontrivial_salt() const noexcept
	{
		return !bytes_are_trivial(positive_match_salt);
	}

	[[nodiscard]] constexpr std::expected<void, ec_error_list>
	validate_offset(uint32_t offset, uint32_t size) const noexcept
	{
		return validate_fp_buffer_offset(sizeof(*this), offset, size);
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	write_chunk(uint32_t offset, std::span<const uint8_t> chunk) noexcept
	{
		return checked_subspan(as_bytes(), offset, chunk.size())
			.transform([chunk](std::span<uint8_t> dest) {
				std::ranges::copy(chunk, dest.begin());
			});
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	read_chunk(uint32_t offset, std::span<uint8_t> dest) const noexcept
	{
		return checked_subspan(as_bytes(), offset, dest.size())
			.transform([dest](std::span<const uint8_t> src) {
				std::ranges::copy(src, dest.begin());
			});
	}

	void clear() noexcept;
};
static_assert(sizeof(enc_buffer) == FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE);

#define FP_NO_SUCH_TEMPLATE (UINT16_MAX)

/* --- Global variables defined in fpsensor_state.cc --- */

/* Fingers templates for the current user */
extern std::array<FpTemplate, FP_MAX_FINGER_COUNT> fp_template;
/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
extern struct enc_buffer fp_enc_buffer;

struct positive_match_secret_state {
	/* b/478160541 */
	static constexpr std::chrono::seconds kValidityWindow{ 15 };

	/* Index of the most recently matched template, if readable. */
	std::optional<FpTemplateId> template_matched = std::nullopt;
	/* Deadline to read positive match secret. */
	FpTimestamp deadline{};

	/**
	 * Allow reading positive match secret for |fgr| in the next 15 seconds.
	 *
	 * @param fgr the index of template to enable positive match secret.
	 * @return {} if the request is valid, error code otherwise.
	 */
	std::expected<void, ec_error_list> enable(FpTemplateId fgr);

	/**
	 * Disallow positive match secret for any finger to be read.
	 */
	void disable() noexcept
	{
		*this = {};
	}

	/**
	 * Atomically take, clear, and validate the positive match secret state
	 * so it can only be used once.
	 */
	[[nodiscard]] std::expected<FpTemplateId, ec_status>
	consume(std::optional<FpTemplateId> expected_fgr = std::nullopt,
		FpTimestamp now = FpClock::now())
	{
		const positive_match_secret_state state =
			std::exchange(*this, {});
		if (!expected_fgr.has_value() && !state.has_any_match()) {
			return std::unexpected(EC_RES_ACCESS_DENIED);
		}
		if (state.is_expired(now)) {
			return std::unexpected(EC_RES_TIMEOUT);
		}
		if (expected_fgr.has_value() &&
		    !state.is_matched(*expected_fgr)) {
			return std::unexpected(EC_RES_ACCESS_DENIED);
		}
		return *state.template_matched;
	}

	[[nodiscard]] constexpr bool is_matched(FpTemplateId fgr) const noexcept
	{
		return template_matched == fgr;
	}

	[[nodiscard]] constexpr bool has_any_match() const noexcept
	{
		return template_matched.has_value();
	}

	[[nodiscard]] constexpr bool
	is_expired(FpTimestamp now = FpClock::now()) const noexcept
	{
		return now >= deadline;
	}
};

/**
 * Domain store for fingerprint templates, salts, encryption buffer, and match
 * secret state.
 */
struct FpTemplateStore {
	static constexpr std::chrono::seconds kEncryptionCooldown{ 1 };

	/** Fingers templates for the current user. */
	std::span<FpTemplate, FP_MAX_FINGER_COUNT> templates = fp_template;
	/** Encryption/decryption buffer. */
	enc_buffer *enc_buf = &fp_enc_buffer;
	/** Index of the last enrolled but not retrieved template. */
	std::optional<FpTemplateId> template_newly_enrolled = std::nullopt;
	/** Number of used templates. */
	uint16_t templ_valid = 0;
	/** Bitmap of the templates with local modifications. */
	std::bitset<FP_MAX_FINGER_COUNT> templ_dirty{};
	struct positive_match_secret_state positive_match_secret_state{};
	/** Salt used in derivation of positive match secret. */
	std::array<FpPositiveMatchSalt, FP_MAX_FINGER_COUNT>
		fp_positive_match_salt{};
	/**
	 * ID of encrypted template in 'enc_buf'.
	 * The template is available if 'crypto.encrypted_template_ready' is
	 * true.
	 */
	std::optional<FpTemplateId> template_encrypted_id = std::nullopt;
	/** Template decryption result. */
	atomic_t template_decryption_result = 0;
	/** Rate-limiting deadline for template encryption (b/114160734). */
	FpTimestamp encryption_deadline{};

	constexpr void mark_dirty(FpTemplateId id) noexcept
	{
		templ_dirty.set(id.raw());
	}

	constexpr void mark_dirty_mask(uint32_t mask) noexcept
	{
		templ_dirty |= std::bitset<FP_MAX_FINGER_COUNT>(mask);
	}

	constexpr void clear_dirty(FpTemplateId id) noexcept
	{
		templ_dirty.reset(id.raw());
	}

	void set_decryption_result(ec_status res) noexcept
	{
		atomic_set(&template_decryption_result, res);
	}

	[[nodiscard]] ec_status get_decryption_result() const noexcept
	{
		return static_cast<ec_status>(
			atomic_get(&template_decryption_result));
	}

	[[nodiscard]] constexpr std::expected<FpTemplateId, ec_status>
	next_free_id() const noexcept
	{
		return FpTemplateId::create(templ_valid)
			.transform_error(
				[](ec_status) { return EC_RES_OVERFLOW; });
	}

	[[nodiscard]] constexpr fp_template_info
	to_template_info() const noexcept
	{
		static_assert(FP_MAX_FINGER_COUNT <= 32);
		return {
			.template_size = FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE,
			.template_max = FP_MAX_FINGER_COUNT,
			.template_valid = templ_valid,
			.template_dirty =
				static_cast<uint32_t>(templ_dirty.to_ulong()),
			.template_version = FP_TEMPLATE_FORMAT_VERSION,
		};
	}

	/**
	 * Check whether template encryption is currently rate-limited (at most
	 * 1 encrypted message per second, see b/114160734), and if not, advance
	 * the encryption deadline by 1 second.
	 */
	[[nodiscard]] constexpr std::expected<void, ec_host_cmd_status>
	try_acquire_encryption_slot(FpTimestamp now = FpClock::now()) noexcept
	{
		if (now < encryption_deadline) {
			return std::unexpected(EC_HOST_CMD_BUSY);
		}
		encryption_deadline = now + kEncryptionCooldown;
		return {};
	}

	void clear_finger(FpTemplateId id) noexcept;
	void reset() noexcept;
	void finish_enrollment(FpTemplateId id, bool auth_enabled);
	[[nodiscard]] std::expected<void, ec_error_list>
	encrypt(FpTemplateId fgr,
		std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);
	[[nodiscard]] std::expected<void, ec_status>
	commit(std::span<const uint8_t> context,
	       std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);
	[[nodiscard]] std::expected<
		CleanseWrapper<
			std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> >,
		ec_status>
	read_match_secret(
		int8_t fgr,
		std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);
};

struct FpAuthChallenge {
	std::array<uint8_t, FP_CHALLENGE_SIZE> value{};
	FpTimestamp deadline{};

	[[nodiscard]] constexpr bool
	is_expired(FpTimestamp now = FpClock::now()) const noexcept
	{
		return now >= deadline;
	}
};

/**
 * Domain context for cryptographic keys, user context, encryption status, and
 * authenticated session state.
 */
struct FpCryptoContext {
	static constexpr std::chrono::seconds kChallengeExpiry{ 5 };

	/** Whether the TPM seed has been set. */
	bool seed_set = false;
	/** Whether an authenticated session is established. */
	bool session_established = false;
	/** Whether an encrypted template is available in |fp_enc_buffer|. */
	bool encrypted_template_ready = false;
	/**
	 * Part of the IKM used to derive encryption keys received from the
	 * TPM.
	 */
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed{};
	/** Current user ID. */
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id{};
	/** The session nonce for session key, if generated. */
	std::optional<
		CleanseWrapper<std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> > >
		session_nonce = std::nullopt;
	/** Current session key. */
	std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> session_key{};
	/** The FPMCU pairing key. */
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key{};
	/** Ephemeral FPMCU ECDH private key during pairing key establishment.
	 */
	bssl::UniquePtr<EC_KEY> ecdh_key{};
	/** Active single-use authentication challenge, if any. */
	std::optional<FpAuthChallenge> active_challenge = std::nullopt;

	[[nodiscard]] constexpr bool user_id_set() const noexcept
	{
		return std::ranges::any_of(user_id, std::identity{});
	}

	constexpr void
	set_user_id(const uint32_t (&words)[FP_CONTEXT_USERID_WORDS]) noexcept
	{
		user_id = std::bit_cast<
			std::array<uint8_t, FP_CONTEXT_USERID_BYTES> >(words);
	}

	[[nodiscard]] constexpr bool tpm_seed_set() const noexcept
	{
		return seed_set;
	}

	constexpr void set_tpm_seed(
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> seed) noexcept
	{
		std::ranges::copy(seed, tpm_seed.begin());
		seed_set = true;
	}

	void clear_tpm_seed() noexcept
	{
		cleanse(tpm_seed);
		seed_set = false;
	}

	[[nodiscard]] constexpr bool auth_enabled() const noexcept
	{
		return session_established;
	}

	constexpr void set_auth_enabled(bool enabled) noexcept
	{
		session_established = enabled;
	}

	[[nodiscard]] constexpr bool session_nonce_set() const noexcept
	{
		return session_nonce.has_value();
	}

	void clear_session_nonce() noexcept
	{
		session_nonce.reset();
	}

	[[nodiscard]] constexpr uint32_t encryption_status() const noexcept
	{
		return (seed_set ? FP_ENC_STATUS_SEED_SET : 0) |
		       (session_established ?
				FP_CONTEXT_STATUS_SESSION_ESTABLISHED :
				0) |
		       (session_nonce.has_value() ?
				FP_CONTEXT_SESSION_NONCE_SET :
				0) |
		       (user_id_set() ? FP_CONTEXT_USER_ID_SET : 0) |
		       (active_challenge.has_value() ? FP_AUTH_CHALLENGE_SET :
						       0) |
		       (encrypted_template_ready ? FP_ENCRYPTED_TEMPLATE_READY :
						   0);
	}

	/**
	 * The context for signing/verifying messages is the Android user id,
	 * which is the first 4-byte integer of user_id.
	 */
	[[nodiscard]] constexpr std::span<const uint8_t, sizeof(uint32_t)>
	auth_user_id_context() const noexcept
	{
		return std::span(user_id).first<sizeof(uint32_t)>();
	}

	void reset_user_context() noexcept;
	void reset_session() noexcept;
	[[nodiscard]] std::expected<void, ec_status>
	set_tpm_seed(const ec_params_fp_seed &params);
	void generate_nonce(std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> out);
	[[nodiscard]] std::expected<void, ec_status>
	generate_challenge(std::span<uint8_t, FP_CHALLENGE_SIZE> out);
	[[nodiscard]] std::expected<FpAuthChallenge, ec_error_list>
	consume_challenge(FpTimestamp now = FpClock::now()) noexcept;
	[[nodiscard]] std::expected<
		CleanseWrapper<
			std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> >,
		ec_error_list>
	derive_session_key(std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN>
				   peer_nonce) const;
	[[nodiscard]] std::expected<void, ec_error_list>
	validate_request(std::span<const uint8_t> context,
			 std::string_view operation,
			 std::span<const uint8_t, FP_MAC_LENGTH> mac);
	[[nodiscard]] std::expected<std::array<uint8_t, FP_MAC_LENGTH>,
				    ec_error_list>
	sign_message(std::span<const uint8_t> context,
		     std::string_view operation,
		     std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge)
		const;
	[[nodiscard]] std::expected<fp_elliptic_curve_public_key, ec_status>
	establish_pairing_key_keygen();
	[[nodiscard]] std::expected<ec_fp_encrypted_pairing_key, ec_status>
	establish_pairing_key_wrap(
		const fp_elliptic_curve_public_key &peers_pubkey);
	[[nodiscard]] std::expected<void, ec_status> load_pairing_key(
		const ec_fp_encrypted_pairing_key &encrypted_pairing_key);
	[[nodiscard]] std::expected<void, ec_status> establish_ascp_pairing_key(
		std::span<const uint8_t, FP_ASCP_KEY_SIZE> pk_g);
};

/**
 * Domain storage for captured raw fingerprint frames and frame parameter cache.
 */
struct FpFrameStorage {
	/** Raw frame buffer. */
	std::span<uint8_t, FP_SENSOR_IMAGE_SIZE> image_buffer = fp_buffer;
	/**
	 * Type of the image captured from the fingerprint sensor in the
	 * most recent successful capture.
	 */
	std::optional<FpCaptureType> current_capture_type = std::nullopt;
	/** Map of the capture type to frame parameters. */
	FpImageFrameParamsCache frame_params_cache{};

	void init_cache()
	{
		frame_params_cache.populate_cache(image_buffer.size());
	}

	constexpr void begin_capture() noexcept
	{
		current_capture_type.reset();
	}

	constexpr void finish_capture(FpCaptureType type) noexcept
	{
		current_capture_type = type;
	}

	void clear() noexcept
	{
		cleanse(image_buffer);
		current_capture_type.reset();
	}

	[[nodiscard]] constexpr
		std::span<uint8_t, FP_SENSOR_IMAGE_SIZE - FP_SENSOR_IMAGE_OFFSET>
		image_data() const noexcept
	{
		return image_buffer.subspan<FP_SENSOR_IMAGE_OFFSET>();
	}

	/**
	 * Return the active captured-frame slice of |image_buffer| for the most
	 * recent capture, or EC_RES_INVALID_PARAM if no valid frame is
	 * available.
	 */
	[[nodiscard]] std::expected<std::span<const uint8_t>, ec_status>
	captured_frame() const
	{
		if (!current_capture_type.has_value()) {
			return std::unexpected(EC_RES_INVALID_PARAM);
		}
		const uint32_t image_offset =
			current_capture_type->skip_image_offset() ?
				FP_SENSOR_IMAGE_OFFSET :
				0;
		const std::span<const uint8_t> buf = image_buffer;
		return frame_params_cache.get_frame_size(*current_capture_type)
			.and_then([buf, image_offset](uint32_t frame_size) {
				return checked_subspan(buf, image_offset,
						       frame_size);
			})
			.transform_error([](ec_error_list) {
				return EC_RES_INVALID_PARAM;
			});
	}
};

struct fpsensor_context {
	FpTemplateStore templates{};
	FpCryptoContext crypto{};
	FpFrameStorage frame{};
	atomic_t fp_events = 0;
	uint32_t sensor_mode = 0;

	[[nodiscard]] constexpr FpSensorMode mode() const noexcept
	{
		return FpSensorMode(sensor_mode);
	}

	[[nodiscard]] constexpr bool has_any_mode(uint32_t mask) const noexcept
	{
		return (sensor_mode & mask) != 0;
	}

	[[nodiscard]] constexpr bool is_crypto_in_progress() const noexcept
	{
		return has_any_mode(FP_MODES_CRYPTO_IN_PROGRESS);
	}

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

	void add_events(uint32_t evt) noexcept
	{
		atomic_or(&fp_events, evt);
	}

	[[nodiscard]] uint32_t take_events() noexcept
	{
		return atomic_clear(&fp_events);
	}

	void reset_context() noexcept;
	void clear_context() noexcept;
	void reset_and_clear_context();
	void encrypt_requested_template();
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	get_encrypted_template_v0(uint16_t idx, uint32_t offset,
				  std::span<uint8_t> output);
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	start_template_encryption(uint16_t idx);
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	get_encrypted_template_chunk(uint32_t offset,
				     std::span<uint8_t> output) const;
	[[nodiscard]] std::expected<void, ec_status>
	load_template_chunk(uint32_t offset, std::span<const uint8_t> chunk);
	[[nodiscard]] std::expected<void, ec_status> commit_template();
	[[nodiscard]] std::expected<void, ec_status>
	start_template_decryption();
	[[nodiscard]] ec_status get_template_decryption_result() const noexcept;
	void decrypt_and_commit_template();
	[[nodiscard]] std::expected<void, ec_status>
	confirm_template(std::span<const uint8_t, FP_MAC_LENGTH> mac);
	[[nodiscard]] std::expected<void, ec_status>
	set_context(const ec_params_fp_context_v1 &p);
	[[nodiscard]] std::expected<void, ec_status>
	establish_session(const ec_params_fp_establish_session &p);
	[[nodiscard]] std::expected<std::array<uint8_t, FP_MAC_LENGTH>,
				    ec_status>
	sign_match(std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge);
	std::expected<CleanseWrapper<std::array<
			      uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> >,
		      ec_status>
	read_match_secret(int8_t fgr);
};

extern struct fpsensor_context global_context;

/* Simulation for unit tests. */
extern "C" __test_only void fp_task_simulate(void);

/*
 * Get the next FP event.
 *
 * @param out the pointer to the output event.
 */
extern "C" int fp_get_next_event(uint8_t *out);

/**
 * Change the sensor mode.
 *
 * @param mode  new mode to change to
 * @param mac   the MAC, required for authenticated modes
 * @param ctx   the fingerprint sensor context to operate on
 * @return resulting FpSensorMode on success, or ec_status error code on
 *         failure.
 */
std::expected<FpSensorMode, ec_status>
fp_set_sensor_mode(uint32_t mode,
		   std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac =
			   std::nullopt,
		   fpsensor_context &ctx = global_context);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_ */
