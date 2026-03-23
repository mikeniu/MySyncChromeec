// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_FILECONTROL_H_
#define UTIL_ELAN_FILECONTROL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

std::optional<size_t> getBinarySize(const std::string &filePath);
std::optional<std::vector<uint8_t> > getBinary(const std::string &filePath);

#endif
