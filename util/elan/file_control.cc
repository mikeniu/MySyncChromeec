// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_control.h"

#include <format>
#include <fstream>

std::expected<size_t, elan::IapError> GetBinarySize(
    const std::string &file_path) {
  std::ifstream bin_file(file_path, std::ios::binary | std::ios::ate);

  if (!bin_file) {
    return std::unexpected(elan::IapError::FileOpenFailed);
  }

  std::streampos pos = bin_file.tellg();
  if (pos == std::streampos(-1) || pos == 0) {
    return std::unexpected(elan::IapError::InvalidFileSize);
  }

  return static_cast<size_t>(pos);
}

std::expected<std::vector<uint8_t>, elan::IapError> GetBinary(
    const std::string &file_path) {
  auto size_res = GetBinarySize(file_path);
  if (!size_res) {
    return std::unexpected(size_res.error());
  }

  std::ifstream bin_file(file_path, std::ios::binary);
  if (!bin_file) {
    return std::unexpected(elan::IapError::FileOpenFailed);
  }

  std::vector<uint8_t> buffer(*size_res);
  if (!bin_file.read(reinterpret_cast<char *>(buffer.data()), *size_res)) {
    return std::unexpected(elan::IapError::FileReadFailed);
  }

  return buffer;
}
