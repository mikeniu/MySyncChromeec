/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_
#define UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_

#include <libusb-1.0/libusb.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "ft_util.h"
#include "usb_transport.h"

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

// RAII deleters for libusb types
struct LibusbContextDeleter {
  void operator()(libusb_context* ctx) const {
    if (ctx) libusb_exit(ctx);
  }
};

struct LibusbDeviceHandleDeleter {
  void operator()(libusb_device_handle* handle) const {
    if (handle) libusb_close(handle);
  }
};

// Concrete USB transport implementation using libusb-1.0
class LibusbTransport : public UsbTransport {
 public:
  // Factory method: finds and opens the first matching Focal device.
  static std::expected<LibusbTransport, FtError> Create();

  // Move semantics only (no copy)
  LibusbTransport(LibusbTransport&& other) noexcept;
  LibusbTransport& operator=(LibusbTransport&& other) noexcept;
  LibusbTransport(const LibusbTransport&) = delete;
  LibusbTransport& operator=(const LibusbTransport&) = delete;

  ~LibusbTransport() override = default;

  // ---------- UsbTransport interface implementation ----------
  UsbDeviceId device_id() const override { return device_id_; }
  std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                   std::chrono::milliseconds timeout) override;
  std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                   std::chrono::milliseconds timeout) override;
  std::expected<void, FtError> WaitForReset(
      std::chrono::milliseconds timeout) override;

  // Optional: raw handle access for debugging
  libusb_device_handle* GetHandle() const { return handle_.get(); }

 private:
  // Private constructor – objects are created via Create()
  LibusbTransport(
      std::unique_ptr<libusb_context, LibusbContextDeleter> ctx,
      std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle,
      UsbDeviceId id, uint8_t ep_in, uint8_t ep_out, uint8_t interface_num);

  // Helper: handle bulk transfer errors (reset device, map error)
  std::expected<int, FtError> HandleTransferError(int libusb_error) const;

  // Member variables
  std::unique_ptr<libusb_context, LibusbContextDeleter> ctx_;
  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle_;
  UsbDeviceId device_id_;
  uint8_t endpoint_in_ = 0;
  uint8_t endpoint_out_ = 0;
  uint8_t interface_num_ = 0;
};

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_
