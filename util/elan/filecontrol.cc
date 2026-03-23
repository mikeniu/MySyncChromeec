// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filecontrol.h"

#include <stdio.h>
#include <stdlib.h>

#include <cstring>
#include <fstream>
#include <iostream>

size_t getBinarySize(const std::string &filePath)
{
	const char funcName[] = "get binary size";

	std::ifstream binFile(filePath, std::ios::binary | std::ios::ate);

	if (!binFile) {
		std::cout << std::format("[IAP] {} - open file fail ({})\n",
					 funcName, filePath);
		return 0;
	}

	return binFile.tellg();
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

	auto size = binFile.tellg();
	binFile.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer(size);
	if (binFile.read(reinterpret_cast<char *>(buffer.data()), size)) {
		return buffer;
	}

	return std::nullopt;
}
