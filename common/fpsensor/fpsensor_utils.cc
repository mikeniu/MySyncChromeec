/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "fpsensor/fpsensor_utils.h"

#include <stdio.h>

#include <cctype>
#include <string>

bool skip_image_offset(enum fp_capture_type capture_type)
{
	return !(capture_type == FP_CAPTURE_VENDOR_FORMAT ||
		 capture_type == FP_CAPTURE_QUALITY_TEST);
}

const std::string fourcc_to_string(uint32_t value)
{
	std::string str(4, '.');
	auto get_char = [&value](int index) {
		const unsigned char ch = (value >> (index * 8)) & 0xFF;
		return std::isprint(ch) ? ch : '.';
	};
	snprintf(str.data(), str.size() + 1, "%c%c%c%c", get_char(0),
		 get_char(1), get_char(2), get_char(3));
	return str;
}
