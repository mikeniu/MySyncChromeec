/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_utils.h"

#include <cctype>
#include <string>

std::string fourcc_to_string(uint32_t value)
{
	auto get_char = [value](int index) -> char {
		const unsigned char ch = (value >> (index * 8)) & 0xFF;
		return std::isprint(ch) ? static_cast<char>(ch) : '.';
	};
	return { get_char(0), get_char(1), get_char(2), get_char(3) };
}
