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
struct libusb_config_descriptor;
struct libusb_device;

namespace focaltech {

inline constexpr uint16_t kGoogleVid = 0x18D1;

inline constexpr UsbDeviceId kFocalRomId = {0x2FD0, 0x0000};
inline constexpr UsbDeviceId kFocalBootId = {0x2808, 0x0001};
inline constexpr UsbDeviceId kFocalEcTestId = {kGoogleVid, 0xFACE};
inline constexpr UsbDeviceId kFocalEcPid9865Id = {kGoogleVid, 0x5403};
inline constexpr UsbDeviceId kFocalEcPid9869Id = {kGoogleVid, 0x5404};

struct LibusbContextDeleter {
  void operator()(libusb_context* ctx) const;
};

struct LibusbDeviceHandleDeleter {
  void operator()(libusb_device_handle* handle) const;
};

struct LibusbDeviceDeleter {
  void operator()(libusb_device* dev) const;
};

struct LibusbConfigDeleter {
  void operator()(libusb_config_descriptor* config) const;
};

class LibusbTransport : public UsbTransport {
 public:
  static std::expected<LibusbTransport, Error> Create();

  LibusbTransport(const LibusbTransport&) = delete;
  LibusbTransport& operator=(const LibusbTransport&) = delete;

  LibusbTransport(LibusbTransport&&) noexcept = default;
  LibusbTransport& operator=(LibusbTransport&&) noexcept = default;

  UsbDeviceId device_id() const override { return device_id_; }
  std::expected<int, Error> Send(std::span<const uint8_t> data,
                                 std::chrono::milliseconds timeout) override;
  std::expected<int, Error> Recv(std::span<uint8_t> data,
                                 std::chrono::milliseconds timeout) override;
  std::expected<void, Error> WaitForReset(
      std::chrono::milliseconds timeout) override;

  ~LibusbTransport() override;

 private:
  explicit LibusbTransport(
      std::unique_ptr<libusb_context, LibusbContextDeleter> ctx,
      libusb_device* dev, UsbDeviceId id);

  std::expected<void, Error> EnsureOpened();
  std::expected<int, Error> HandleTransferError(int libusb_error) const;

  std::unique_ptr<libusb_context, LibusbContextDeleter> ctx_;
  std::unique_ptr<libusb_device, LibusbDeviceDeleter> device_;
  UsbDeviceId device_id_;

  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle_;
  uint8_t endpoint_in_ = 0;
  uint8_t endpoint_out_ = 0;
  uint8_t interface_num_ = 0;
  bool opened_ = false;
};

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_LIBUSB_TRANSPORT_H_
