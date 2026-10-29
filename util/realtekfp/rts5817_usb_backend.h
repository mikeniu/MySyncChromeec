/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// USB transport seam for the RTS5817 flashing tool. RtsUsbDevice talks to
// the hardware only through this interface so that it can be unit tested
// without a physical device.

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_BACKEND_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_BACKEND_H_

#include <cstdint>
#include <expected>
#include <string_view>

namespace realtekfp {

// Values of the libusb_error codes the device layer needs to tell apart.
inline constexpr int kUsbErrorAccess = -3;
inline constexpr int kUsbErrorNoDevice = -4;
inline constexpr int kUsbErrorNotFound = -5;

struct UsbDeviceId {
  uint16_t vid = 0;
  uint16_t pid = 0;

  bool operator==(const UsbDeviceId&) const = default;
};

// Fallible backend operations return the negative libusb_error code on
// failure.
class RtsUsbBackend {
 public:
  virtual ~RtsUsbBackend() = default;

  // Creates the USB context.
  virtual std::expected<void, int> Init() = 0;

  // Opens the device with the given VID:PID.
  virtual std::expected<void, int> Open(UsbDeviceId id) = 0;

  // Closes the device and destroys the context. Safe to call twice.
  virtual void Close() = 0;

  // Name of a negative libusb error code, for diagnostics.
  virtual std::string_view ErrorName(int error) const = 0;
};

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_BACKEND_H_
