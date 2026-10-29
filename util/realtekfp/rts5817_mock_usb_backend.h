/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_MOCK_USB_BACKEND_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_MOCK_USB_BACKEND_H_

#include <gmock/gmock.h>

#include <expected>
#include <string_view>

#include "rts5817_usb_backend.h"

namespace realtekfp {

class MockRtsUsbBackend : public RtsUsbBackend {
 public:
  MOCK_METHOD((std::expected<void, int>), Init, (), (override));
  MOCK_METHOD((std::expected<void, int>), Open, (UsbDeviceId id), (override));
  MOCK_METHOD(void, Close, (), (override));
  MOCK_METHOD(std::string_view, ErrorName, (int error), (const, override));
};

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_MOCK_USB_BACKEND_H_
