/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_detect.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_utils.h"
#include "system.h"
#include "util.h"
#include "watchdog.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

#ifdef CONFIG_CMD_FPSENSOR_DEBUG
/* --- Debug console commands --- */

using namespace std::chrono_literals;

namespace
{

void sleep_for(std::chrono::microseconds d)
{
	crec_usleep(static_cast<unsigned int>(d.count()));
}

template <typename T>
std::expected<T, ec_error_list> parse_int(std::string_view str, int base = 10)
{
	T value{};
	const auto [ptr, ec] = std::from_chars(
		str.data(), str.data() + str.size(), value, base);
	if (ec != std::errc{} || ptr != str.data() + str.size()) {
		return std::unexpected(EC_ERROR_PARAM1);
	}
	return value;
}

} /* namespace */

/*
 * Send the current Fingerprint buffer to the host
 * it is formatted as an 8-bpp PGM ASCII file.
 *
 * In addition, it prepends a short Z-Modem download signature,
 * which triggers automatically your preferred viewer if you configure it
 * properly in "File transfer protocols" in the Minicom options menu.
 * (as triggered by Ctrl-A O)
 * +--------------------------------------------------------------------------+
 * |     Name             Program             Name U/D FullScr IO-Red. Multi  |
 * | A  zmodem     /usr/bin/sz -vv -b          Y    U    N       Y       Y    |
 *  [...]
 * | L  pgm        /usr/bin/display_pgm        N    D    N       Y       N    |
 * | M  Zmodem download string activates... L                                 |
 *
 * My /usr/bin/display_pgm looks like this:
 * #!/bin/sh
 * TMPF=$(mktemp)
 * ascii-xfr -rdv ${TMPF}
 * display ${TMPF}
 *
 * Alternative (if you're using screen as your terminal):
 *
 * From *outside* the chroot:
 *
 * Install ascii-xfr: sudo apt-get install minicom
 * Install imagemagick: sudo apt-get install imagemagick
 *
 * Add the following to your ${HOME}/.screenrc:
 *
 * zmodem catch
 * zmodem recvcmd '!!! bash -c "ascii-xfr -rdv /tmp/finger.pgm && display
 * /tmp/finger.pgm"'
 *
 * From *outside the chroot*, use screen to connect to UART console:
 *
 * sudo screen -c ${HOME}/.screenrc /dev/pts/NN 115200
 *
 */
test_export_static std::expected<void, ec_error_list>
upload_pgm_image(std::span<const uint8_t> frame,
		 const struct fp_image_frame_params_v2 &image_frame_params)
{
	uint8_t bytes_per_pixel = DIV_ROUND_UP(image_frame_params.bpp, 8);

	if (bytes_per_pixel != 1 && bytes_per_pixel != 2) {
		return std::unexpected(EC_ERROR_UNKNOWN);
	}

	/* fake Z-modem ZRQINIT signature */
	CPRINTF("#IGNORE for ZModem\r**\030B00");
	sleep_for(2s); /* let the download program start */

	/* Print 8-bpp or 16-bpp PGM ASCII header */
	CPRINTF("P2\n%d %d\n%d\n", image_frame_params.width,
		image_frame_params.height,
		(bytes_per_pixel == 2) ? 65535 : 255);

	std::span<const uint8_t> remaining = frame;
	for (int y = 0; y < image_frame_params.height; y++) {
		watchdog_reload();
		for (int x = 0; x < image_frame_params.width; x++) {
			const uint16_t pixel =
				(bytes_per_pixel == 2) ?
					UINT16_FROM_BYTE_ARRAY_LE(remaining,
								  0) :
					remaining[0];
			CPRINTF("%d ", pixel);
			remaining = remaining.subspan(bytes_per_pixel);
		}
		CPRINTF("\n");
		cflush();
	}

	CPRINTF("\x04"); /* End Of Transmission */
	return {};
}

static std::expected<void, ec_error_list> fp_console_action(uint32_t mode)
{
	if (!global_context.has_any_mode(FP_MODE_RESET_SENSOR))
		CPRINTS("Waiting for finger ...");

	if (!fp_set_sensor_mode(mode).has_value()) {
		/*
		 * EC host command errors do not directly map to console command
		 * errors.
		 */
		return std::unexpected(EC_ERROR_UNKNOWN);
	}

	int tries = 200;
	while (tries--) {
		if (!global_context.has_any_mode(FP_MODE_ANY_CAPTURE)) {
			CPRINTS("done (events:%x)",
				static_cast<int>(global_context.fp_events));
			return {};
		}
		sleep_for(100ms);
	}
	return std::unexpected(EC_ERROR_TIMEOUT);
}

test_export_static std::expected<fp_image_frame_params_v2, ec_error_list>
get_image_frame_params(std::optional<FpCaptureType> capture_type)
{
	if (!capture_type.has_value()) {
		return std::unexpected(EC_ERROR_INVAL);
	}
	FpSensorInfoBuffer info{};
	const auto types = info.query();
	if (!types.has_value()) {
		return std::unexpected(types.error());
	}
	const auto it =
		std::ranges::find(*types, *capture_type,
				  &fp_image_frame_params_v2::fp_capture_type);
	if (it == types->end()) {
		return std::unexpected(EC_ERROR_INVAL);
	}
	return *it;
}

static std::expected<void, ec_error_list> upload_current_pgm_image()
{
	const auto params = get_image_frame_params(
		global_context.frame.current_capture_type);
	if (!params.has_value()) {
		CPRINTF("Failed to get image frame params, error: %d\n",
			params.error());
		return std::unexpected(params.error());
	}
	return upload_pgm_image(global_context.frame.image_data(), *params);
}

static int command_fpcapture(int argc, const char **argv)
{
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	FpCaptureType capture_type{ *FpCaptureType::create(
		FP_CAPTURE_SIMPLE_IMAGE) };

	if (argc >= 2) {
		const auto parsed = parse_int<int>(argv[1]).and_then(
			&FpCaptureType::create<int>);
		if (!parsed.has_value())
			return EC_ERROR_PARAM1;
		capture_type = *parsed;
	}
	const uint32_t mode = FP_MODE_CAPTURE | capture_type.to_mode_bits();

	if (const auto rc = fp_console_action(mode); !rc.has_value()) {
		return rc.error();
	}

	return upload_current_pgm_image().error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpcapture, command_fpcapture, nullptr,
			"Capture fingerprint in PGM format");

/* Transfer a chunk of the image from the host to the FPMCU
 *
 * Command format:
 *  fpupload <offset> <hex encoded pixel string>
 *
 * To limit the size of the commands, only a chunk of the image is sent for
 * each command invocation.
 */
static int command_fpupload(int argc, const char **argv)
{
	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;
	const auto offset = parse_int<uint32_t>(argv[1]);
	if (!offset.has_value())
		return offset.error();

	std::string_view pixels_str(argv[2]);
	auto dest = checked_subspan(global_context.frame.image_data(), *offset,
				    DIV_ROUND_UP(pixels_str.size(), 2));
	if (!dest.has_value())
		return EC_ERROR_PARAM1;

	for (uint8_t &byte : *dest) {
		const std::string_view chunk = pixels_str.substr(0, 2);
		byte = parse_int<uint8_t>(chunk, 16).value_or(0);
		pixels_str.remove_prefix(chunk.size());
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fpupload, command_fpupload, nullptr,
			"Copy fp image onto fpmcu fpsensor buffer");

/* Transfer an image from the FPMCU to the host
 *
 * Command format:
 *  fpdownload
 *
 * This is useful to verify the data was transferred correctly. Note that it
 * requires the terminal to be configured as explained in the comment above
 * upload_pgm_image().
 */
static int command_fpdownload(int argc, const char **argv)
{
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	return upload_current_pgm_image().error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpdownload, command_fpdownload, nullptr,
			"Copy fp image from fpmcu fpsensor buffer");

static int command_fpenroll(int argc, const char **argv)
{
	std::expected<void, ec_error_list> rc{};
	int percent = 0;
	static constexpr std::array<const char *, 4> enroll_str = {
		"OK", "Low Quality", "Immobile", "Low Coverage"
	};

	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	while (true) {
		int tries = 1000;

		rc = fp_console_action(FP_MODE_ENROLL_SESSION |
				       FP_MODE_ENROLL_IMAGE);
		if (!rc.has_value())
			break;
		const FpMkbpEvent event{ global_context.take_events() };
		percent = event.enroll_progress();
		CPRINTS("Enroll capture: %s (%d%%)",
			enroll_str[event.error_code() % enroll_str.size()],
			percent);
		if (percent == 100) {
			break;
		}
		/* wait for finger release between captures */
		(void)fp_set_sensor_mode(FP_MODE_ENROLL_SESSION |
					 FP_MODE_FINGER_UP);
		while (tries-- &&
		       global_context.has_any_mode(FP_MODE_FINGER_UP))
			sleep_for(20ms);
	}
	(void)fp_set_sensor_mode(0); /* reset FP_MODE_ENROLL_SESSION */

	return rc.error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpenroll, command_fpenroll, nullptr,
			"Enroll a new fingerprint");

static int command_fpinfo(int argc, const char **argv)
{
#if defined(HAVE_FP_PRIVATE_DRIVER) || defined(BOARD_HOST)
	FpSensorInfoBuffer info{};
	const auto types = info.query();
	if (!types.has_value()) {
		ccprintf("Failed to get fp_info_v2\n");
		return types.error();
	}

	constexpr int align = 15;

	ccprintf("%*s: 0x%X (%s)\n", align, "Vendor ID",
		 info.header.sensor_info.vendor_id,
		 fourcc_to_string(info.header.sensor_info.vendor_id).data());
	ccprintf("%*s: 0x%X\n", align, "Product ID",
		 info.header.sensor_info.product_id);
	ccprintf("%*s: 0x%X\n", align, "Model ID",
		 info.header.sensor_info.model_id);
	ccprintf("%*s: 0x%X\n", align, "Version",
		 info.header.sensor_info.version);

	ccprintf("%*s: 0x%X\n", align, "Error State",
		 info.header.sensor_info.errors);

	ccprintf("%*s: %s\n", align, "Sensor Strap",
		 fp_sensor_type_to_str(fpsensor_detect_get_type()));

	for (const auto &param : *types) {
		ccprintf("FP Capture Type: %d\n", param.fp_capture_type);
		ccprintf("  %*s: %u x %u %ubpp\n", align, "Sensor (w x h)",
			 param.width, param.height, param.bpp);
		ccprintf("  %*s: %u\n", align, "Frame Size", param.frame_size);
		ccprintf("  %*s: %u\n", align, "Image Data Offset (bytes)",
			 param.image_data_offset_bytes);
		ccprintf("  %*s: 0x%X (%s)\n", align, "Pixel Format",
			 param.pixel_format,
			 fourcc_to_string(param.pixel_format).data());
	}

	return EC_SUCCESS;
#else
	ccprintf("fpinfo command not supported on this firmware.\n");
	return EC_ERROR_UNKNOWN;
#endif
}
DECLARE_SAFE_CONSOLE_COMMAND(fpinfo, command_fpinfo, nullptr,
			     "Print fingerprint system info");

static int command_fpmatch(int argc, const char **argv)
{
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	const auto rc = fp_console_action(FP_MODE_MATCH);
	const FpMkbpEvent event{ global_context.take_events() };

	if (rc.has_value() && event.is_match()) {
		const uint32_t match_errcode = event.error_code();

		CPRINTS("Match: %s (%d)",
			fp_match_success(match_errcode) ? "YES" : "NO",
			match_errcode);
	}

	return rc.error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpmatch, command_fpmatch, nullptr,
			"Run match algorithm against finger");

static int command_fpclear(int argc, const char **argv)
{
	/*
	 * We intentionally run this on the fp_task so that we use the
	 * same code path as host commands.
	 */
	const auto rc = fp_console_action(FP_MODE_RESET_SENSOR);

	if (!rc.has_value())
		CPRINTS("Failed to clear fingerprint context: %d", rc.error());

	(void)global_context.take_events();

	return rc.error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpclear, command_fpclear, nullptr,
			"Clear fingerprint sensor context");

static int command_fpmaintenance(int argc, const char **argv)
{
#ifdef HAVE_FP_PRIVATE_DRIVER
	if (!fp_set_sensor_mode(FP_MODE_SENSOR_MAINTENANCE).has_value()) {
		/*
		 * EC host command errors do not directly map to console command
		 * errors.
		 */
		return EC_ERROR_UNKNOWN;
	}

	/* Block console until maintenance is finished. */
	while (global_context.has_any_mode(FP_MODE_SENSOR_MAINTENANCE)) {
		sleep_for(100ms);
	}
#endif /* #ifdef HAVE_FP_PRIVATE_DRIVER */

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fpmaintenance, command_fpmaintenance, nullptr,
			"Run fingerprint sensor maintenance");

#endif /* CONFIG_CMD_FPSENSOR_DEBUG */
