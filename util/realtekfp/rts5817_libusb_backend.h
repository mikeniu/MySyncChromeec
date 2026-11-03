/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_LIBUSB_BACKEND_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_LIBUSB_BACKEND_H_

#include <libusb-1.0/libusb.h>

#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "rts5817_usb_backend.h"

namespace realtekfp {

// Deleter that calls the C function `Fn`, for unique_ptr to libusb objects. A
// named type rather than a lambda, which has internal linkage and so makes
// every class holding it warn when defined in a header.
template <auto Fn>
struct CDeleter {
  template <typename T>
  void operator()(T* p) const {
    Fn(p);
  }
};

class RtsLibusbBackend : public RtsUsbBackend {
 public:
  RtsLibusbBackend() = default;
  RtsLibusbBackend(const RtsLibusbBackend&) = delete;
  RtsLibusbBackend& operator=(const RtsLibusbBackend&) = delete;
  ~RtsLibusbBackend() override = default;

  std::expected<void, int> Init() override;
  std::expected<void, int> Open(UsbDeviceId id) override;
  void Close() override;
  bool IsOpen() const override;
  std::expected<size_t, int> ControlTransferIn(
      uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
      std::span<uint8_t> data, std::chrono::milliseconds timeout) override;
  std::expected<size_t, int> ControlTransferOut(
      uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
      std::span<const uint8_t> data,
      std::chrono::milliseconds timeout) override;
  std::expected<std::vector<RtsUsbInterface>, int> GetInterfaces() override;
  std::expected<void, int> ClaimInterface(int interface_number) override;
  void ReleaseInterface(int interface_number) override;
  std::string_view ErrorName(int error) const override;

 private:
  // `ctx_` is declared first so that it is destroyed last: the device handle
  // must be closed before the context is exited.
  std::unique_ptr<libusb_context, CDeleter<libusb_exit>> ctx_;
  std::unique_ptr<libusb_device_handle, CDeleter<libusb_close>> dev_handle_;
};

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_LIBUSB_BACKEND_H_
