// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

size_t getBinarySize(const std::string &filePath);
std::optional<std::vector<uint8_t> > getBinary(const std::string &filePath);
