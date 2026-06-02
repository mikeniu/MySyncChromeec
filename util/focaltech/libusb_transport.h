/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_
#define UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "usb_transport.h"

struct libusb_context;
struct libusb_device_handle;

// USB identifiers for Focal devices
constexpr uint16_t kFocalRomVid = 0x2FD0;
constexpr uint16_t kFocalRomPid = 0x0000;
constexpr uint16_t kFocalBootVid = 0x2808;
constexpr uint16_t kFocalBootPid = 0x0001;
constexpr uint16_t kFocalEcVid = 0x18D1;
constexpr uint16_t kFocalEcPidTest = 0xFACE;
constexpr uint16_t kFocalEcPid9865 = 0x5403;
constexpr uint16_t kFocalEcPid9869 = 0x5404;

namespace focaltech {

// Deleter for libusb_context
struct LibusbContextDeleter {
  void operator()(libusb_context* ctx) const;
};

// Deleter for libusb_device_handle
struct LibusbDeviceHandleDeleter {
  void operator()(libusb_device_handle* handle) const;
};

// USB transport implementation using raw libusb calls.
class LibusbTransport : public UsbTransport {
 public:
  // initializes libusb, finds a Focal device, opens it,
  // detaches kernel driver, claims interface, and returns a transport object.
  static std::expected<LibusbTransport, FtError> Create();

  LibusbTransport(const LibusbTransport&) = delete;
  LibusbTransport& operator=(const LibusbTransport&) = delete;

  // Move operations are defaulted – safe because members are destroyed
  LibusbTransport(LibusbTransport&&) noexcept = default;
  LibusbTransport& operator=(LibusbTransport&&) noexcept = default;

  // UsbTransport interface.
  UsbDeviceId device_id() const override { return device_id_; }
  std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                   std::chrono::milliseconds timeout) override;
  std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                   std::chrono::milliseconds timeout) override;
  std::expected<void, FtError> WaitForReset(
      std::chrono::milliseconds timeout) override;

 private:
  LibusbTransport(
      std::unique_ptr<libusb_context, LibusbContextDeleter> ctx,
      std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle,
      UsbDeviceId id, uint8_t ep_in, uint8_t ep_out);

  // Helper to handle bulk transfer errors (resets device, maps error).
  std::expected<int, FtError> HandleTransferError(int libusb_error) const;

  std::unique_ptr<libusb_context, LibusbContextDeleter> ctx_;
  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle_;
  UsbDeviceId device_id_;
  uint8_t endpoint_in_ = 0;
  uint8_t endpoint_out_ = 0;
};

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_
