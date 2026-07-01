/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_comm.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <string>

namespace egis {

namespace {
constexpr int kInterfaceNumber = 0;

std::string LibusbErrorToString(int errcode) {
  return std::string(libusb_error_name(errcode));
}

UsbError LibusbErrorToUsbError(int rc) {
  switch (rc) {
    case LIBUSB_ERROR_TIMEOUT:
      return UsbError::kTimeout;
    case LIBUSB_ERROR_PIPE:
      return UsbError::kPipe;
    case LIBUSB_ERROR_OVERFLOW:
      return UsbError::kOverflow;
    case LIBUSB_ERROR_NO_DEVICE:
      return UsbError::kNoDevice;
    case LIBUSB_ERROR_BUSY:
      return UsbError::kBusy;
    case LIBUSB_ERROR_INVALID_PARAM:
      return UsbError::kInvalidParam;
    case LIBUSB_ERROR_ACCESS:
      return UsbError::kAccessDenied;
    case LIBUSB_ERROR_IO:
      return UsbError::kIoError;
    default:
      return UsbError::kOther;
  }
}
}  // namespace

UsbComm::UsbComm(libusb_context* ctx, const UsbDeviceProfile& profile)
    : ctx_(ctx),
      vid_(profile.id.vid),
      pid_(profile.id.pid),
      ep_out_(profile.ep_out),
      ep_in_(profile.ep_in) {}

UsbComm::~UsbComm() {
  if (handle_) {
    libusb_release_interface(handle_.get(), kInterfaceNumber);
    if (kernel_driver_detached_) {
      int rc = libusb_attach_kernel_driver(handle_.get(), kInterfaceNumber);
      if (rc == 0) {
        std::println("Kernel driver successfully reattached.");
      } else {
        std::println(stderr, "[WARN] Failed to reattach kernel driver: {}",
                     LibusbErrorToString(rc));
      }
    }
  }
}

std::expected<void, UsbError> UsbComm::Connect() {
  if (handle_) return {};

  if (!ctx_) {
    std::println(stderr, "[ERROR] libusb context is not initialized.");
    return std::unexpected(UsbError::kInvalidParam);
  }

  handle_.reset(libusb_open_device_with_vid_pid(ctx_, vid_, pid_));
  if (!handle_) {
    std::println(stderr,
                 "[ERROR] Cannot open device VID: 0x{:04x} PID: 0x{:04x}", vid_,
                 pid_);
    return std::unexpected(UsbError::kNoDevice);
  }

  if (libusb_kernel_driver_active(handle_.get(), kInterfaceNumber) == 1) {
    if (libusb_detach_kernel_driver(handle_.get(), kInterfaceNumber) == 0) {
      std::println("Kernel driver successfully detached.");
      kernel_driver_detached_ = true;
    }
  }

  int rc = libusb_claim_interface(handle_.get(), kInterfaceNumber);
  if (rc != 0) {
    std::println(stderr,
                 "[WARN] Failed to claim interface {} for VID: 0x{:04x} PID: "
                 "0x{:04x}: {}",
                 kInterfaceNumber, vid_, pid_, LibusbErrorToString(rc));
    handle_.reset();
    return std::unexpected(LibusbErrorToUsbError(rc));
  }

  std::println("--- Device connected: VID=0x{:x}, PID=0x{:x} ---", vid_, pid_);
  return {};
}
std::expected<int, UsbError> UsbComm::ExecuteBulkTransfer(
    uint8_t ep, uint8_t* data, int length, std::chrono::milliseconds timeout) {
  if (!handle_) {
    std::println(stderr, "[ERROR] Not connected. Call connect() first.");
    return std::unexpected(UsbError::kNotConnected);
  }

  int transferred = 0;
  int rc = libusb_bulk_transfer(handle_.get(), ep, data, length, &transferred,
                                static_cast<unsigned int>(timeout.count()));
  if (rc != 0) {
    std::println(stderr, "[ERROR] USB Transfer failed: {}",
                 LibusbErrorToString(rc));
    return std::unexpected(LibusbErrorToUsbError(rc));
  }
  return transferred;
}

std::expected<void, UsbError> UsbComm::DoSend(
    std::span<const uint8_t> data, std::chrono::milliseconds timeout) {
  auto res = ExecuteBulkTransfer(ep_out_, const_cast<uint8_t*>(data.data()),
                                 static_cast<int>(data.size()), timeout);
  if (!res || *res != static_cast<int>(data.size())) {
    return std::unexpected(res ? UsbError::kIoError : res.error());
  }
  return {};
}

std::expected<int, UsbError> UsbComm::DoReceive(
    std::span<uint8_t> rx_buffer, std::chrono::milliseconds timeout) {
  return ExecuteBulkTransfer(ep_in_, rx_buffer.data(),
                             static_cast<int>(rx_buffer.size()), timeout);
}

}  // namespace egis
