/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_
#define PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest_assert.h>

#include <algorithm>
#include <array>
#include <bit>
#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <flash.h>
#include <fpsensor/fpsensor_image_frame_params.h>
#include <fpsensor/fpsensor_modes.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>
#include <rollback.h>
#include <rollback_private.h>

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#define IMAGE_SIZE                                                 \
	MAX_FROM_LIST(LISTIFY(NUM_IMAGE_CAPTURE_TYPES,             \
			      FINGERPRINT_SENSOR_FRAME_SIZE, (, ), \
			      DT_CHOSEN(cros_fp_fingerprint_sensor)))

#define ROLLBACK0_ADDR DT_REG_ADDR(DT_NODELABEL(rollback0))
#define ROLLBACK0_SIZE DT_REG_SIZE(DT_NODELABEL(rollback0))
#define ROLLBACK1_ADDR DT_REG_ADDR(DT_NODELABEL(rollback1))
#define ROLLBACK1_SIZE DT_REG_SIZE(DT_NODELABEL(rollback1))

/* The fake TPM seed is "very_secret_32_bytes_of_tpm_seed" */
inline constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> kDefaultTpmSeed = {
	0x76, 0x65, 0x72, 0x79, 0x5f, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74,
	0x5f, 0x33, 0x32, 0x5f, 0x62, 0x79, 0x74, 0x65, 0x73, 0x5f, 0x6f,
	0x66, 0x5f, 0x74, 0x70, 0x6d, 0x5f, 0x73, 0x65, 0x65, 0x64
};

/* The fake UserID is "i_m_a_fake_user_id_used_for_test" */
inline constexpr std::array<uint32_t, FP_CONTEXT_USERID_WORDS> kDefaultUserId = {
	0x5f6d5f69, 0x61665f61, 0x755f656b, 0x5f726573,
	0x755f6469, 0x5f646573, 0x5f726f66, 0x74736574
};

template <typename Fn> inline void update_sim_state(Fn &&mutate)
{
	struct fingerprint_sensor_state state{};
	fingerprint_get_state(fp_sim, &state);
	mutate(state);
	fingerprint_set_state(fp_sim, &state);
}

inline void reset_sim_state()
{
	struct fingerprint_sensor_state state = {
		.bad_pixels = 0,
		.maintenance_ran = false,
		.detect_mode = false,
		.low_power_mode = false,
		.finger_state = FINGERPRINT_FINGER_STATE_NONE,
		.init_result = 0,
		.deinit_result = 0,
		.config_result = 0,
		.get_info_result = 0,
		.acquire_image_result = FINGERPRINT_SENSOR_SCAN_GOOD,
		.last_acquire_image_mode = -1,
	};
	fingerprint_set_state(fp_sim, &state);
}

template <typename PreEntropyFn = decltype([] {})>
inline void init_test_rollback(PreEntropyFn &&pre_entropy = {})
{
	static constexpr char kEntropyStr[] = "some_rollback_entropy";
	static constexpr auto kFakeRollbackEntropy =
		std::bit_cast<std::array<uint8_t, sizeof(kEntropyStr)> >(
			kEntropyStr);
	const struct rollback_data data = {
		.id = 0,
		.rollback_min_version = 0,
#ifdef CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE
		.secret = { 0 },
#endif
		.cookie = CROS_EC_ROLLBACK_COOKIE,
	};

	zassert_ok(crec_flash_erase(ROLLBACK0_ADDR, ROLLBACK0_SIZE));
	zassert_ok(crec_flash_write(ROLLBACK0_ADDR, sizeof(data),
				    reinterpret_cast<const char *>(&data)));

	zassert_ok(crec_flash_erase(ROLLBACK1_ADDR, ROLLBACK1_SIZE));
	zassert_ok(crec_flash_write(ROLLBACK1_ADDR, sizeof(data),
				    reinterpret_cast<const char *>(&data)));

	pre_entropy();

	rollback_add_entropy(kFakeRollbackEntropy.data(),
			     kFakeRollbackEntropy.size() - 1);
}

template <typename Header, size_t BufferSize> struct FpTemplatePacket {
	static constexpr size_t kHeaderSize = offsetof(Header, data);
	static_assert(BufferSize > kHeaderSize);
	static constexpr size_t kDataSize = BufferSize - kHeaderSize;

	alignas(Header) std::array<uint8_t, BufferSize> buffer {};

	Header *header()
	{
		return reinterpret_cast<Header *>(buffer.data());
	}
	Header *operator->()
	{
		return header();
	}
	operator Header *()
	{
		return header();
	}
	std::span<uint8_t, kDataSize> data()
	{
		return std::span<uint8_t, kDataSize>(
			buffer.data() + kHeaderSize, kDataSize);
	}
	enc_buffer *enc() requires(kDataSize >= sizeof(enc_buffer))
	{
		return reinterpret_cast<enc_buffer *>(data().data());
	}
	constexpr size_t packet_size(size_t chunk_size) const
	{
		return kHeaderSize + chunk_size;
	}
};

template <typename Header>
using FpFullTemplatePacket =
	FpTemplatePacket<Header, offsetof(Header, data) + sizeof(enc_buffer)>;

inline uint32_t get_fp_mode()
{
	ec_params_fp_mode params = {
		.mode = FP_MODE_DONT_CHANGE,
	};
	ec_response_fp_mode response{};
	zassert_ok(ec_cmd_fp_mode(nullptr, &params, &response));
	return response.mode;
}

inline uint32_t set_fp_mode(uint32_t mode)
{
	ec_params_fp_mode params = {
		.mode = mode,
	};
	ec_response_fp_mode response{};
	zassert_ok(ec_cmd_fp_mode(nullptr, &params, &response));
	k_msleep(1);
	return response.mode;
}

inline void set_default_tpm_seed()
{
	ec_params_fp_seed fp_seed_params = {
		.struct_version = FP_TEMPLATE_FORMAT_VERSION,
		.reserved = 0,
	};
	std::ranges::copy(kDefaultTpmSeed, fp_seed_params.seed);
	zassert_ok(ec_cmd_fp_seed(nullptr, &fp_seed_params));
}

inline void reset_test_fp_context(
	std::span<const uint32_t, FP_CONTEXT_USERID_WORDS> userid =
		kDefaultUserId)
{
	ec_params_fp_context_v1 fp_context_params = {
		.action = FP_CONTEXT_ASYNC,
	};
	std::ranges::copy(userid, fp_context_params.userid);
	zassert_ok(ec_cmd_fp_context_v1(nullptr, &fp_context_params));
	k_msleep(1);

	fp_context_params.action = FP_CONTEXT_GET_RESULT;
	zassert_ok(ec_cmd_fp_context_v1(nullptr, &fp_context_params));
}

extern "C" inline ec_status test_send_host_command(int command, int version,
						   const void *params,
						   int params_size, void *resp,
						   int resp_size)
{
	struct host_cmd_handler_args args{};

	args.command = command;
	args.version = version;
	args.params = params;
	args.params_size = params_size;
	args.response = resp;
	args.response_max = resp_size;
	args.response_size = 0;

	return static_cast<ec_status>(host_command_process(&args));
}

template <typename Params, typename Resp>
inline ec_status send_host_cmd(int command, int version, const Params &params,
			       Resp &resp)
{
	return test_send_host_command(command, version, &params, sizeof(params),
				      &resp, sizeof(resp));
}

template <typename Params>
inline ec_status send_host_cmd_req(int command, int version,
				   const Params &params)
{
	return test_send_host_command(command, version, &params, sizeof(params),
				      nullptr, 0);
}

template <typename Resp>
inline ec_status send_host_cmd_resp(int command, int version, Resp &resp)
{
	return test_send_host_command(command, version, nullptr, 0, &resp,
				      sizeof(resp));
}

#define FP_SIMULATOR_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)            \
	{                                                                   \
		.frame_size = FINGERPRINT_SENSOR_FRAME_SIZE(idx, node_id),  \
		.image_data_offset_bytes =                                  \
			FINGERPRINT_SENSOR_IMAGE_OFFSET(idx, node_id),      \
		.pixel_format =                                             \
			FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(idx, node_id), \
		.width = FINGERPRINT_SENSOR_RES_X(idx, node_id),            \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, node_id),           \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, node_id),            \
		.fp_capture_type =                                          \
			FINGERPRINT_SENSOR_CAPTURE_TYPE(idx, node_id),      \
		.reserved = 0,                                              \
	}

class FpImageFrameParamsCacheTestHelper {
    public:
	FpImageFrameParamsCacheTestHelper() = delete;

	static bool set_frame_size(FpImageFrameParamsCache &cache,
				   enum fp_capture_type capture_type,
				   uint32_t size)
	{
		const auto valid_type = FpCaptureType::create(capture_type);
		if (!valid_type.has_value()) {
			printk("Error: Invalid capture type %d\n",
			       capture_type);
			return false;
		}
		cache.frame_params_[valid_type->raw()].frame_size_bytes = size;
		return true;
	}
};

#endif /* PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_ \
	*/
