/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "file_util.h"

#include <algorithm>
#include <cerrno>
#include <fstream>

#include "crypto_util.h"

namespace egis {

std::expected<std::vector<uint8_t>, std::errc> ReadBinaryFile(
    std::string_view path) {
  errno = 0;
  std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
  if (!file) {
    return std::unexpected(errno ? static_cast<std::errc>(errno)
                                 : std::errc::io_error);
  }

  std::streamsize size = file.tellg();
  if (size <= 0) return std::unexpected(std::errc::invalid_argument);

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    return std::unexpected(std::errc::io_error);
  }
  return buffer;
}

std::expected<void, std::errc> LoadAesKey(std::string_view key_file_path,
                                          std::span<uint8_t, 32> key_buffer) {
  auto key_res = ReadBinaryFile(key_file_path);
  if (!key_res) return std::unexpected(key_res.error());

  auto &key_opt = *key_res;
  if (key_opt.size() != key_buffer.size()) {
    crypto::Cleanse(key_opt);
    return std::unexpected(std::errc::invalid_argument);
  }

  std::ranges::copy(key_opt, key_buffer.begin());
  crypto::Cleanse(key_opt);
  return {};
}

}  // namespace egis
