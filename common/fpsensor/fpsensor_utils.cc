/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_utils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <string>

std::string fourcc_to_string(uint32_t value)
{
	std::string result(sizeof(value), '.');
	std::ranges::transform(
		std::bit_cast<std::array<unsigned char, sizeof(value)> >(value),
		result.begin(), [](unsigned char ch) {
			return std::isprint(ch) ? static_cast<char>(ch) : '.';
		});
	return result;
}
