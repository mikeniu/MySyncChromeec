/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint template index and positive match secret state */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_TEMPLATE_STORE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_TEMPLATE_STORE_H_

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_utils.h"

#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>

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
		const auto id = create(idx);
		if (!id.has_value()) {
			return std::unexpected(id.error());
		}
		if (id->raw() >= templ_valid) {
			return std::unexpected(EC_RES_UNAVAILABLE);
		}
		return *id;
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

struct FpTemplateStore {
	/* b/478160541 */
	static constexpr std::chrono::seconds kSecretValidityWindow{ 15 };

	static void disable_secret(positive_match_secret_state &state) noexcept
	{
		state.template_matched = FP_NO_SUCH_TEMPLATE;
		state.readable = false;
		state.deadline.val = 0;
	}

	[[nodiscard]] static std::expected<void, ec_error_list>
	enable_secret(uint16_t fgr, positive_match_secret_state &state,
		      FpTimestamp now = FpClock::now()) noexcept
	{
		if (state.readable) {
			disable_secret(state);
			return std::unexpected(EC_ERROR_UNKNOWN);
		}
		state.template_matched = fgr;
		state.readable = true;
		state.deadline.val =
			static_cast<uint64_t>((now + kSecretValidityWindow)
						      .time_since_epoch()
						      .count());
		return {};
	}

	[[nodiscard]] static std::expected<FpTemplateId, ec_status>
	consume_secret(positive_match_secret_state &state,
		       std::optional<FpTemplateId> expected_fgr = std::nullopt,
		       FpTimestamp now = FpClock::now()) noexcept
	{
		const positive_match_secret_state copy = state;
		disable_secret(state);

		const auto matched =
			FpTemplateId::create(copy.template_matched);
		if (!expected_fgr.has_value() &&
		    (!copy.readable || !matched.has_value())) {
			return std::unexpected(EC_RES_ACCESS_DENIED);
		}
		if (now >=
		    FpTimestamp{ FpClock::duration{ copy.deadline.val } }) {
			return std::unexpected(EC_RES_TIMEOUT);
		}
		if (!copy.readable || !matched.has_value() ||
		    (expected_fgr.has_value() && *matched != *expected_fgr)) {
			return std::unexpected(EC_RES_ACCESS_DENIED);
		}
		return *matched;
	}
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_TEMPLATE_STORE_H_ */
