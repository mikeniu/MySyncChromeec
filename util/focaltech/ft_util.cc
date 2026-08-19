/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft_util.h"

#include <fstream>
#include <string>
#include <vector>

#include "ft_log.h"

namespace focaltech {

std::expected<std::vector<uint8_t>, Error> ReadFileToVector(
    std::string_view path) {
  std::ifstream file(std::string{path}, std::ios::binary | std::ios::ate);
  if (!file) {
    FT_LOGE("Cannot open file: {}", path);
    return std::unexpected(Error::kFileNotFound);
  }

  const auto file_size_bytes = file.tellg();
  if (file_size_bytes <= 0) {
    FT_LOGE("File is empty or invalid: {}", path);
    return std::unexpected(Error::kInvalidParameter);
  }

  std::vector<uint8_t> buffer(static_cast<size_t>(file_size_bytes));
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size_bytes)) {
    FT_LOGE("Failed to read file payload.");
    return std::unexpected(Error::kHardwareFailure);
  }

  return buffer;
}

}  // namespace focaltech
