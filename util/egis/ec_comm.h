/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_EC_COMM_H_
#define UTIL_EGIS_EC_COMM_H_

// C++ system files
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

// Project headers
#include "ec_protocol_defs.h"
#include "usb_comm.h"

enum class EcError {
  kUsbSendFailed,
  kUsbReceiveFailed,
  kResponseTooShort,
  kInvalidChecksum,
  kDeviceError,
};

constexpr std::string_view ToString(EcError err) {
  switch (err) {
    case EcError::kUsbSendFailed:
      return "Failed to send command over USB";
    case EcError::kUsbReceiveFailed:
      return "Failed to receive response over USB";
    case EcError::kResponseTooShort:
      return "Response was truncated or too short";
    case EcError::kInvalidChecksum:
      return "Response checksum verification failed";
    case EcError::kDeviceError:
      return "EC device reported an error";
    default:
      return "An unknown EC communication error occurred";
  }
}

class EcComm {
 public:
  static constexpr size_t kMaxResponseSize = 512;

  explicit EcComm(UsbComm &usb);

  std::expected<std::vector<uint8_t>, EcError> SendCommand(
      uint16_t id, uint8_t ver,
      std::span<const uint8_t> payload = std::span<const uint8_t>());

 private:
  UsbComm &usb_;
};

#endif  // UTIL_EGIS_EC_COMM_H_
