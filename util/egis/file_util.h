/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_FILE_UTIL_H_
#define UTIL_EGIS_FILE_UTIL_H_

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#include "crypto_util.h"

namespace egis {

std::expected<std::vector<uint8_t>, std::errc> ReadBinaryFile(
    std::string_view path);

std::expected<std::array<uint8_t, crypto::kAes256KeySize>, std::errc>
LoadAesKey(std::string_view key_file_path);

}  // namespace egis

#endif  // UTIL_EGIS_FILE_UTIL_H_
