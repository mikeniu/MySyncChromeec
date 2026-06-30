/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_FLASHER_LOGIC_H_
#define UTIL_EGIS_FLASHER_LOGIC_H_

#include <expected>

#include "bootrom.h"
#include "cli_options.h"

namespace egis {

std::expected<void, AppError> ExecuteFirmwareFlash(const FlasherConfig& config,
                                                   BootromComm& bootrom_comm);

std::expected<void, AppError> ExecuteRawCommandFlash(
    const FlasherConfig& config, BootromComm& bootrom_comm);

}  // namespace egis

#endif  // UTIL_EGIS_FLASHER_LOGIC_H_
