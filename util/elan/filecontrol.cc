// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filecontrol.h"

#include <format>
#include <fstream>
#include <iostream>

std::optional<size_t> getBinarySize(const std::string &filePath)
{
	const char funcName[] = "get binary size";

	std::ifstream binFile(filePath, std::ios::binary | std::ios::ate);

	if (!binFile) {
		std::cout << std::format("[IAP] {} - open file fail ({})\n",
					 funcName, filePath);
		return std::nullopt;
	}

	std::streampos pos = binFile.tellg();
	if (pos == std::streampos(-1)) {
		std::cout
			<< std::format("[IAP] {} - file size is 0\n", funcName);
		return std::nullopt;
	}

	return static_cast<size_t>(pos);
}

std::optional<std::vector<uint8_t> > getBinary(const std::string &filePath)
{
	const char funcName[] = "get binary";

	std::ifstream binFile(filePath, std::ios::binary | std::ios::ate);

	if (!binFile) {
		std::cout << std::format("[IAP] {} - open file fail ({})\n",
					 funcName, filePath);
		return std::nullopt;
	}

	std::streampos pos = binFile.tellg();
	if (pos == std::streampos(-1)) {
		std::cout
			<< std::format("[IAP] {} - file size is 0\n", funcName);
		return std::nullopt;
	}

	size_t size = static_cast<size_t>(pos);

	binFile.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer(size);
	if (binFile.read(reinterpret_cast<char *>(buffer.data()), size)) {
		return buffer;
	}

	return std::nullopt;
}
