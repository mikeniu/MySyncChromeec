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

class FpPgmImage {
    public:
	[[nodiscard]] static std::expected<FpPgmImage, ec_error_list>
	create(std::span<const uint8_t> frame,
	       const fp_image_frame_params_v2 &params) noexcept
	{
		const uint8_t bytes_per_pixel = DIV_ROUND_UP(params.bpp, 8);
		if (bytes_per_pixel != 1 && bytes_per_pixel != 2) {
			return std::unexpected(EC_ERROR_UNKNOWN);
		}
		return FpPgmImage(frame, params.width, params.height,
				  bytes_per_pixel);
	}

	[[nodiscard]] constexpr uint16_t max_pixel_value() const noexcept
	{
		return (bytes_per_pixel_ == 2) ? 65535 : 255;
	}

	void stream() const
	{
		/* fake Z-modem ZRQINIT signature */
		CPRINTF("#IGNORE for ZModem\r**\030B00");
		sleep_for(2s); /* let the download program start */

		/* Print 8-bpp or 16-bpp PGM ASCII header */
		CPRINTF("P2\n%d %d\n%d\n", width_, height_, max_pixel_value());

		std::span<const uint8_t> remaining = frame_;
		for (int y = 0; y < height_; y++) {
			watchdog_reload();
			for (int x = 0; x < width_; x++) {
				const uint16_t pixel =
					(bytes_per_pixel_ == 2) ?
						UINT16_FROM_BYTE_ARRAY_LE(
							remaining, 0) :
						remaining[0];
				CPRINTF("%d ", pixel);
				remaining = remaining.subspan(bytes_per_pixel_);
			}
			CPRINTF("\n");
			cflush();
		}

		CPRINTF("\x04"); /* End Of Transmission */
	}

    private:
	constexpr FpPgmImage(std::span<const uint8_t> frame, uint16_t width,
			     uint16_t height, uint8_t bytes_per_pixel) noexcept
		: frame_(frame),
		  width_(width),
		  height_(height),
		  bytes_per_pixel_(bytes_per_pixel)
	{
	}

	std::span<const uint8_t> frame_{};
	uint16_t width_ = 0;
	uint16_t height_ = 0;
	uint8_t bytes_per_pixel_ = 1;
};

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
		 const fp_image_frame_params_v2 &image_frame_params)
{
	return FpPgmImage::create(frame, image_frame_params)
		.transform(&FpPgmImage::stream);
}

std::expected<void, ec_error_list>
FpFrameStorage::upload_hex_chunk(uint32_t offset, std::string_view hex_pixels)
{
	auto dest = checked_subspan(image_data(), offset,
				    DIV_ROUND_UP(hex_pixels.size(), 2));
	if (!dest.has_value()) {
		return std::unexpected(EC_ERROR_PARAM1);
	}

	for (uint8_t &byte : *dest) {
		const std::string_view chunk = hex_pixels.substr(0, 2);
		byte = parse_int<uint8_t>(chunk, 16).value_or(0);
		hex_pixels.remove_prefix(chunk.size());
	}

	return {};
}

std::expected<void, ec_error_list> FpFrameStorage::stream_pgm() const
{
	const auto params =
		FpSensorInfoBuffer::find_capture_params(current_capture_type);
	if (!params.has_value()) {
		CPRINTF("Failed to get image frame params, error: %d\n",
			params.error());
		return std::unexpected(params.error());
	}
	return upload_pgm_image(image_data(), *params);
}

std::expected<void, ec_error_list>
FpSensorContext::wait_for_mode_clear(uint32_t mask,
				     std::chrono::microseconds poll_interval,
				     int max_tries) const
{
	while (max_tries < 0 || max_tries-- > 0) {
		if (!sensor_mode.has_any(mask)) {
			return {};
		}
		sleep_for(poll_interval);
	}
	return std::unexpected(EC_ERROR_TIMEOUT);
}

std::expected<void, ec_error_list>
FpSensorContext::run_console_capture(uint32_t mode)
{
	if (!sensor_mode.is_reset_sensor()) {
		CPRINTS("Waiting for finger ...");
	}

	if (!set_sensor_mode(mode).has_value()) {
		/*
		 * EC host command errors do not directly map to console command
		 * errors.
		 */
		return std::unexpected(EC_ERROR_UNKNOWN);
	}

	return wait_for_mode_clear(FP_MODE_ANY_CAPTURE, 100ms, 200)
		.transform([this] {
			CPRINTS("done (events:%x)",
				static_cast<int>(events.peek().raw));
		});
}

std::expected<void, ec_error_list>
FpSensorContext::console_capture(FpCaptureType capture_type)
{
	const uint32_t mode = FP_MODE_CAPTURE | capture_type.to_mode_bits();
	return run_console_capture(mode).and_then(
		[this] { return frame.stream_pgm(); });
}

std::expected<void, ec_error_list> FpSensorContext::console_enroll()
{
	std::expected<void, ec_error_list> rc{};

	while (true) {
		rc = run_console_capture(FP_MODE_ENROLL_SESSION |
					 FP_MODE_ENROLL_IMAGE);
		if (!rc.has_value()) {
			break;
		}
		const FpMkbpEvent event = events.take();
		const int percent = event.enroll_progress();
		CPRINTS("Enroll capture: %s (%d%%)", event.enroll_error_name(),
			percent);
		if (percent == 100) {
			break;
		}
		/* wait for finger release between captures */
		(void)set_sensor_mode(FP_MODE_ENROLL_SESSION |
				      FP_MODE_FINGER_UP);
		(void)wait_for_mode_clear(FP_MODE_FINGER_UP, 20ms, 1000);
	}
	(void)set_sensor_mode(0); /* reset FP_MODE_ENROLL_SESSION */

	return rc;
}

std::expected<void, ec_error_list> FpSensorContext::console_match()
{
	const auto rc = run_console_capture(FP_MODE_MATCH);
	const FpMkbpEvent event = events.take();

	if (rc.has_value() && event.is_match()) {
		CPRINTS("Match: %s (%d)",
			event.is_match_success() ? "YES" : "NO",
			event.error_code());
	}

	return rc;
}

std::expected<void, ec_error_list> FpSensorContext::console_clear()
{
	/*
	 * We intentionally run this on the fp_task so that we use the
	 * same code path as host commands.
	 */
	const auto rc = run_console_capture(FP_MODE_RESET_SENSOR);

	if (!rc.has_value()) {
		CPRINTS("Failed to clear fingerprint context: %d", rc.error());
	}

	(void)events.take();

	return rc;
}

std::expected<void, ec_error_list> FpSensorContext::console_maintenance()
{
#ifdef HAVE_FP_PRIVATE_DRIVER
	if (!set_sensor_mode(FP_MODE_SENSOR_MAINTENANCE).has_value()) {
		/*
		 * EC host command errors do not directly map to console command
		 * errors.
		 */
		return std::unexpected(EC_ERROR_UNKNOWN);
	}

	/* Block console until maintenance is finished. */
	(void)wait_for_mode_clear(FP_MODE_SENSOR_MAINTENANCE, 100ms, -1);
#endif /* #ifdef HAVE_FP_PRIVATE_DRIVER */

	return {};
}

std::expected<void, ec_error_list> FpSensorInfoBuffer::print_console() const
{
	constexpr int kAlign = 15;

	ccprintf("%*s: 0x%X (%s)\n", kAlign, "Vendor ID",
		 header.sensor_info.vendor_id,
		 FpFourCc(header.sensor_info.vendor_id).c_str());
	ccprintf("%*s: 0x%X\n", kAlign, "Product ID",
		 header.sensor_info.product_id);
	ccprintf("%*s: 0x%X\n", kAlign, "Model ID",
		 header.sensor_info.model_id);
	ccprintf("%*s: 0x%X\n", kAlign, "Version", header.sensor_info.version);

	ccprintf("%*s: 0x%X\n", kAlign, "Error State",
		 header.sensor_info.errors);

	ccprintf("%*s: %s\n", kAlign, "Sensor Strap",
		 FpDetectedHardware::sensor_type_name());

	for (const auto &param : capture_types()) {
		ccprintf("FP Capture Type: %d\n", param.fp_capture_type);
		ccprintf("  %*s: %u x %u %ubpp\n", kAlign, "Sensor (w x h)",
			 param.width, param.height, param.bpp);
		ccprintf("  %*s: %u\n", kAlign, "Frame Size", param.frame_size);
		ccprintf("  %*s: %u\n", kAlign, "Image Data Offset (bytes)",
			 param.image_data_offset_bytes);
		ccprintf("  %*s: 0x%X (%s)\n", kAlign, "Pixel Format",
			 param.pixel_format,
			 FpFourCc(param.pixel_format).c_str());
	}

	return {};
}

extern FpSensorContext global_context;

namespace
{

int command_fpcapture(int argc, const char **argv)
{
	if (system_is_locked()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	FpCaptureType capture_type{ *FpCaptureType::create(
		FP_CAPTURE_SIMPLE_IMAGE) };

	if (argc >= 2) {
		const auto parsed = parse_int<int>(argv[1]).and_then(
			&FpCaptureType::create<int>);
		if (!parsed.has_value()) {
			return EC_ERROR_PARAM1;
		}
		capture_type = *parsed;
	}

	return global_context.console_capture(capture_type).error_or(EC_SUCCESS);
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
int command_fpupload(int argc, const char **argv)
{
	if (argc != 3) {
		return EC_ERROR_PARAM_COUNT;
	}
	if (system_is_locked()) {
		return EC_ERROR_ACCESS_DENIED;
	}
	return parse_int<uint32_t>(argv[1])
		.and_then([&](uint32_t offset) {
			return global_context.frame.upload_hex_chunk(offset,
								     argv[2]);
		})
		.error_or(EC_SUCCESS);
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
int command_fpdownload(int argc, const char **argv)
{
	if (system_is_locked()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return global_context.frame.stream_pgm().error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpdownload, command_fpdownload, nullptr,
			"Copy fp image from fpmcu fpsensor buffer");

int command_fpenroll(int argc, const char **argv)
{
	if (system_is_locked()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return global_context.console_enroll().error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpenroll, command_fpenroll, nullptr,
			"Enroll a new fingerprint");

int command_fpinfo(int argc, const char **argv)
{
#if defined(HAVE_FP_PRIVATE_DRIVER) || defined(BOARD_HOST)
	FpSensorInfoBuffer info{};
	if (const auto types = info.query(); !types.has_value()) {
		ccprintf("Failed to get fp_info_v2\n");
		return types.error();
	}

	return info.print_console().error_or(EC_SUCCESS);
#else
	ccprintf("fpinfo command not supported on this firmware.\n");
	return EC_ERROR_UNKNOWN;
#endif
}
DECLARE_SAFE_CONSOLE_COMMAND(fpinfo, command_fpinfo, nullptr,
			     "Print fingerprint system info");

int command_fpmatch(int argc, const char **argv)
{
	if (system_is_locked()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return global_context.console_match().error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpmatch, command_fpmatch, nullptr,
			"Run match algorithm against finger");

int command_fpclear(int argc, const char **argv)
{
	return global_context.console_clear().error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpclear, command_fpclear, nullptr,
			"Clear fingerprint sensor context");

int command_fpmaintenance(int argc, const char **argv)
{
	return global_context.console_maintenance().error_or(EC_SUCCESS);
}
DECLARE_CONSOLE_COMMAND(fpmaintenance, command_fpmaintenance, nullptr,
			"Run fingerprint sensor maintenance");

} /* namespace */

#endif /* CONFIG_CMD_FPSENSOR_DEBUG */
