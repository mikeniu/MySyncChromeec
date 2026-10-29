/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_MOCK_USB_BACKEND_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_MOCK_USB_BACKEND_H_

#include <gmock/gmock.h>

#include <chrono>
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

#include "rts5817_usb_backend.h"

namespace realtekfp {

class MockRtsUsbBackend : public RtsUsbBackend {
 public:
  MOCK_METHOD((std::expected<void, int>), Init, (), (override));
  MOCK_METHOD((std::expected<void, int>), Open, (UsbDeviceId id), (override));
  MOCK_METHOD(void, Close, (), (override));
  MOCK_METHOD(bool, IsOpen, (), (const, override));
  MOCK_METHOD((std::expected<size_t, int>), ControlTransferIn,
              (uint8_t request_type, uint8_t request, uint16_t value,
               uint16_t index, std::span<uint8_t> data,
               std::chrono::milliseconds timeout),
              (override));
  MOCK_METHOD((std::expected<size_t, int>), ControlTransferOut,
              (uint8_t request_type, uint8_t request, uint16_t value,
               uint16_t index, std::span<const uint8_t> data,
               std::chrono::milliseconds timeout),
              (override));
  MOCK_METHOD(std::string_view, ErrorName, (int error), (const, override));
};

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_MOCK_USB_BACKEND_H_
