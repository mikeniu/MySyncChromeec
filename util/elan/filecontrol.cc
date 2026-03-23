// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filecontrol.h"
#include "utility.h"

#include <fstream>
#include <iostream>

std::optional<size_t> getBinarySize(const std::string &filePath)
{
	std::ifstream binFile(filePath, std::ios::binary | std::ios::ate);

	if (!binFile) {
		logInfo("[IAP] {} - open file fail ({})\n", __func__, filePath);
		return std::nullopt;
	}

	std::streampos pos = binFile.tellg();
	if (pos == std::streampos(-1) || pos == 0) {
		logInfo("[IAP] {} - file size is 0 or invalid\n", __func__);
		return std::nullopt;
	}

	return static_cast<size_t>(pos);
}

std::optional<std::vector<uint8_t> > getBinary(const std::string &filePath)
{
	auto sizeOpt = getBinarySize(filePath);
	if (!sizeOpt) {
		return std::nullopt;
	}

	size_t size = *sizeOpt;

	std::ifstream binFile(filePath, std::ios::binary);
	if (!binFile) {
		logInfo("[IAP] {} - open file fail ({})\n", __func__, filePath);
		return std::nullopt;
	}

	std::vector<uint8_t> buffer(size);
	if (binFile.read(reinterpret_cast<char *>(buffer.data()), size)) {
		return buffer;
	}

	logInfo("[IAP] {} - read file data fail\n", __func__);
	return std::nullopt;
}
