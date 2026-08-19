// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_control.h"

#include <fstream>

std::expected<std::vector<uint8_t>, elan::IapError> GetBinary(
    const std::string& file_path) {
  std::ifstream bin_file(file_path, std::ios::binary | std::ios::ate);
  if (!bin_file) return std::unexpected(elan::IapError::FileOpenFailed);

  std::streamsize size = bin_file.tellg();
  if (size <= 0) return std::unexpected(elan::IapError::InvalidFileSize);

  bin_file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);

  if (!bin_file.read(reinterpret_cast<char*>(buffer.data()), size)) {
    return std::unexpected(elan::IapError::FileReadFailed);
  }

  return buffer;
}
