// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_FILE_CONTROL_H_
#define UTIL_ELAN_FILE_CONTROL_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "utility.h"

std::expected<std::vector<uint8_t>, elan::IapError> GetBinary(
    const std::string& file_path);

#endif
