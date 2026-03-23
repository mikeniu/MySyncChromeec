// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_control.h"
#include "utility.h"

#include <format>
#include <fstream>
#include <iostream>

std::optional<size_t> GetBinarySize(const std::string &file_path)
{
	std::ifstream bin_file(file_path, std::ios::binary | std::ios::ate);

	if (!bin_file) {
		LogInfo("[IAP] {} - open file fail ({})\n", __func__,
			file_path);
		return std::nullopt;
	}

	std::streampos pos = bin_file.tellg();
	if (pos == std::streampos(-1) || pos == 0) {
		LogInfo("[IAP] {} - file size is 0 or invalid\n", __func__);
		return std::nullopt;
	}

	return static_cast<size_t>(pos);
}

std::optional<std::vector<uint8_t> > GetBinary(const std::string &file_path)
{
	auto size_opt = GetBinarySize(file_path);
	if (!size_opt) {
		return std::nullopt;
	}

	size_t size = *size_opt;

	std::ifstream bin_file(file_path, std::ios::binary);
	if (!bin_file) {
		LogInfo("[IAP] {} - open file fail ({})\n", __func__,
			file_path);
		return std::nullopt;
	}

	std::vector<uint8_t> buffer(size);
	if (bin_file.read(reinterpret_cast<char *>(buffer.data()), size)) {
		return buffer;
	}

	LogInfo("[IAP] {} - read file data fail\n", __func__);
	return std::nullopt;
}
