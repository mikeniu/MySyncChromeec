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
struct libusb_device;

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

// Intermediate structure returned by FindDevice()
struct FoundDeviceInfo {
  std::unique_ptr<libusb_context, LibusbContextDeleter> ctx;
  libusb_device* dev = nullptr;  // raw pointer, reference already taken
  UsbDeviceId id;
};

// USB transport implementation using raw libusb calls.
// The device is not opened immediately; opening is deferred until first
// Send/Recv.
class LibusbTransport : public UsbTransport {
 public:
  // Full create (find + open) – kept for backward compatibility.
  // Internally calls FindDevice() and then constructs the transport (lazy
  // open).
  static std::expected<LibusbTransport, FtError> Create();

  // Step 1: Initialize libusb and find a matching Focal device.
  // Returns FoundDeviceInfo on success. This can be used to construct a
  // LibusbTransport without opening the device yet.
  static std::expected<FoundDeviceInfo, FtError> FindDevice();

  // Construct a transport from a previously found device.
  // The device is not opened until first Send/Recv.
  explicit LibusbTransport(FoundDeviceInfo info);

  LibusbTransport(const LibusbTransport&) = delete;
  LibusbTransport& operator=(const LibusbTransport&) = delete;

  // Move operations are defaulted – safe because members are destroyed.
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

  ~LibusbTransport() override;

 private:
  // Ensure the device is opened (claim interface, find endpoints, etc.).
  // Called automatically from Send/Recv.
  std::expected<void, FtError> EnsureOpened();

  // Helper to handle bulk transfer errors (resets device, maps error).
  std::expected<int, FtError> HandleTransferError(int libusb_error) const;

  std::unique_ptr<libusb_context, LibusbContextDeleter> ctx_;
  libusb_device* device_ =
      nullptr;  // raw pointer, referenced, not owned by handle_
  UsbDeviceId device_id_;

  // Lazy-opened resources (initially empty)
  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle_;
  uint8_t endpoint_in_ = 0;
  uint8_t endpoint_out_ = 0;
  uint8_t interface_num_ = 0;
  bool opened_ = false;
};

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_
