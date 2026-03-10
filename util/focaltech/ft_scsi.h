/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_SCSI_H_
#define UTIL_FOCALTECH_FT_SCSI_H_

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "usb_context.h"

std::array<uint8_t, 16> BuildFtCmd(uint8_t opcode, uint8_t subcmd,
                                   uint32_t address = 0, uint32_t length = 0,
                                   uint32_t aux_address = 0, uint8_t flags = 0);

std::expected<void, FtError> SendBulkCommand(
    const UsbDevice& device, std::span<const uint8_t> scsi_cmd,
    std::span<uint8_t> data_buffer, TransferDirection dir,
    std::chrono::milliseconds timeout_ms);

#endif  // UTIL_FOCALTECH_FT_SCSI_H_
