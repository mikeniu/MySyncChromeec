/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_USB_TRANSPORT_H_
#define UTIL_FOCALTECH_USB_TRANSPORT_H_

#include <chrono>
#include <cstdint>
#include <expected>
#include <span>

#include "ft_util.h"

namespace focaltech {

// USB Full Speed bulk endpoint maximum packet size.
inline constexpr size_t kUsbBulkMaxPacketSize = 64;

struct UsbDeviceId {
  uint16_t vid = 0;
  uint16_t pid = 0;

  bool operator==(const UsbDeviceId&) const = default;
};

class UsbTransport {
 public:
  virtual ~UsbTransport() = default;

  virtual UsbDeviceId device_id() const = 0;
  virtual std::expected<size_t, Error> Send(
      std::span<const uint8_t> data, std::chrono::milliseconds timeout) = 0;
  virtual std::expected<size_t, Error> Recv(
      std::span<uint8_t> data, std::chrono::milliseconds timeout) = 0;
  virtual std::expected<void, Error> WaitForReset(
      std::chrono::milliseconds timeout) = 0;
};

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_USB_TRANSPORT_H_
