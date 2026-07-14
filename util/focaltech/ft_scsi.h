/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_SCSI_H_
#define UTIL_FOCALTECH_FT_SCSI_H_

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

#include "usb_device.h"

namespace focaltech {

constexpr uint32_t kUsbMsCbwSignature = 0x43425355;
constexpr uint32_t kUsbMsCswSignature = 0x53425355;

constexpr size_t kScsiCmdSize = 16;

enum class CbwDirection : uint8_t {
  kOut = 0x00,
  kIn = 0x80,
};

enum class Opcode : uint8_t {
  kReadInfo = 0xDB,
  kAction = 0xDC,
};

enum class Subcmd : uint8_t {
  kReadData = 0x01,
  kWriteBin = 0x03,
  kCodeBin = 0x04,
  kErasePage = 0x08,
  kVerify = 0xBB,
  kSoftReset = 0xDD,
  kFlashWpSet = 0xF7,
  kFlashWpGet = 0xF8,
};

struct CmdFormat {
  Opcode opcode;
  uint32_t address = 0;
  uint32_t length = 0;
  uint32_t aux_address = 0;
  Subcmd subcmd;
  uint8_t flags = 0;
  uint8_t reserved = 0;
} __attribute__((packed));

static_assert(sizeof(CmdFormat) == 16, "CmdFormat must be exactly 16 bytes");

struct BulkCbw {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_transfer_length;
  CbwDirection flags;
  uint8_t lun;
  uint8_t cmd_len;
  CmdFormat cmd;
} __attribute__((packed));

static_assert(sizeof(BulkCbw) == 31, "USB CBW must be exactly 31 bytes");

enum class CswStatus : uint8_t {
  kCommandPassed = 0x00,
  kCommandFailed = 0x01,
  kPhaseError = 0x02,
};

struct BulkCsw {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_residue;
  CswStatus status;
} __attribute__((packed));

static_assert(sizeof(BulkCsw) == 13, "USB CSW must be exactly 13 bytes");

enum class TransferDirection { kIn, kOut, kSendOnly };

std::expected<void, Error> SendBulkDataOut(const UsbDevice& device,
                                           const CmdFormat& scsi_cmd,
                                           std::span<const uint8_t> data_buffer,
                                           TransferDirection direction,
                                           std::chrono::milliseconds timeout);

std::expected<size_t, Error> ReceiveBulkDataIn(
    const UsbDevice& device, const CmdFormat& scsi_cmd,
    std::span<uint8_t> data_buffer, std::chrono::milliseconds timeout);

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_FT_SCSI_H_
