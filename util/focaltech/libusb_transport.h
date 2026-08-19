/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_
#define UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_

#include <libusb.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "usb_transport.h"

namespace focaltech {

inline constexpr UsbDeviceId kFocalRomId = {0x2FD0, 0x0000};
inline constexpr UsbDeviceId kFocalBootId = {0x2808, 0x0001};

using ScopedContext = std::unique_ptr<libusb_context, decltype(&libusb_exit)>;
using ScopedDevice =
    std::unique_ptr<libusb_device, decltype(&libusb_unref_device)>;
using ScopedHandle =
    std::unique_ptr<libusb_device_handle, decltype(&libusb_close)>;
using ScopedConfig = std::unique_ptr<libusb_config_descriptor,
                                     decltype(&libusb_free_config_descriptor)>;

class LibusbTransport : public UsbTransport {
 public:
  static std::expected<LibusbTransport, Error> Create();

  LibusbTransport(const LibusbTransport&) = delete;
  LibusbTransport& operator=(const LibusbTransport&) = delete;

  LibusbTransport(LibusbTransport&&) noexcept = default;
  LibusbTransport& operator=(LibusbTransport&&) noexcept = default;

  UsbDeviceId device_id() const override { return device_id_; }
  std::expected<size_t, Error> Send(std::span<const uint8_t> data,
                                    std::chrono::milliseconds timeout) override;
  std::expected<size_t, Error> Recv(std::span<uint8_t> data,
                                    std::chrono::milliseconds timeout) override;
  std::expected<void, Error> WaitForReset(
      std::chrono::milliseconds timeout) override;

  ~LibusbTransport() override;

 private:
  LibusbTransport(ScopedContext context, libusb_device* usb_device,
                  UsbDeviceId id);

  std::expected<void, Error> Open();
  std::expected<size_t, Error> HandleTransferError(int libusb_error) const;

  ScopedContext context_;
  ScopedDevice device_{nullptr, libusb_unref_device};
  UsbDeviceId device_id_;

  ScopedHandle handle_{nullptr, libusb_close};
  uint8_t endpoint_in_ = 0;
  uint8_t endpoint_out_ = 0;
  uint8_t interface_number_ = 0;
  bool opened_ = false;
};

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_
