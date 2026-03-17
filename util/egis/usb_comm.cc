/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_comm.h"

// C system files
#include <cstdio>
#include <cstdlib>

// C++ system files
#include <array>
#include <chrono>
#include <print>
#include <string>

namespace {
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

UsbComm::UsbComm(libusb_context *ctx, uint16_t vid, uint16_t pid)
    : ctx_(ctx), vid_(vid), pid_(pid) {}

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

std::expected<int, UsbError> UsbComm::BulkWrite(
    uint8_t endpoint, std::span<const uint8_t> data,
    std::chrono::milliseconds timeout) {
  if (!handle_) return std::unexpected(UsbError::kNotConnected);

  int transferred = 0;
  int rc = libusb_bulk_transfer(handle_.get(), endpoint,
                                const_cast<uint8_t *>(data.data()),
                                static_cast<int>(data.size()), &transferred,
                                static_cast<unsigned int>(timeout.count()));
  if (rc != 0) return std::unexpected(LibusbErrorToUsbError(rc));
  return transferred;
}

std::expected<int, UsbError> UsbComm::BulkRead(
    uint8_t endpoint, std::span<uint8_t> data,
    std::chrono::milliseconds timeout) {
  if (!handle_) return std::unexpected(UsbError::kNotConnected);

  int transferred = 0;
  int rc = libusb_bulk_transfer(handle_.get(), endpoint, data.data(),
                                static_cast<int>(data.size()), &transferred,
                                static_cast<unsigned int>(timeout.count()));
  if (rc != 0 && rc != LIBUSB_ERROR_TIMEOUT)
    return std::unexpected(LibusbErrorToUsbError(rc));
  return transferred;
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
    handle_.reset();
    return std::unexpected(LibusbErrorToUsbError(rc));
  }

  std::println("--- Device connected: VID=0x{:x}, PID=0x{:x} ---", vid_, pid_);
  return {};
}

std::expected<void, UsbError> UsbComm::Send(std::span<const uint8_t> data) {
  if (!handle_) {
    std::println(stderr, "[ERROR] Not connected. Call connect() first.");
    return std::unexpected(UsbError::kNotConnected);
  }

  int transferred = 0;
  int rc = libusb_bulk_transfer(handle_.get(), kEpOutBulk,
                                const_cast<uint8_t *>(data.data()), data.size(),
                                &transferred, kTransferTimeout.count());

  if (rc != 0 || transferred != static_cast<int>(data.size())) {
    std::println(stderr, "[ERROR] Failed to send command: {}",
                 LibusbErrorToString(rc));
    return std::unexpected(LibusbErrorToUsbError(rc));
  }
  return {};
}

std::expected<void, UsbError> UsbComm::Wait() {
  if (!handle_) {
    std::println(stderr, "[ERROR] Not connected. Call connect() first.");
    return std::unexpected(UsbError::kNotConnected);
  }

  std::array<uint8_t, 4> irq_buf{};
  int transferred = 0;
  int retries = 0;
  const int kMaxRetries = 10;

  while (retries < kMaxRetries) {
    int rc = libusb_interrupt_transfer(handle_.get(), kEpInInt, irq_buf.data(),
                                       irq_buf.size(), &transferred,
                                       kTransferTimeout.count());

    if (rc == 0) {
      if (transferred > 0 && irq_buf[0] == kResponseReadyIrq) {
        return {};
      }
    } else if (rc == LIBUSB_ERROR_TIMEOUT) {
      retries++;
      continue;
    } else {
      std::println(stderr, "[ERROR] Interrupt IN error: {}",
                   LibusbErrorToString(rc));
      break;
    }
  }
  std::println(
      stderr,
      "[ERROR] Failed to receive RESPONSE_READY_IRQ after retries or error.");
  return std::unexpected(UsbError::kResponseIrqFailed);
}

std::expected<int, UsbError> UsbComm::Receive(std::span<uint8_t> data) {
  if (!handle_) {
    std::println(stderr, "[ERROR] Not connected. Call connect() first.");
    return std::unexpected(UsbError::kNotConnected);
  }

  int transferred = 0;
  int rc =
      libusb_bulk_transfer(handle_.get(), kEpInBulk, data.data(), data.size(),
                           &transferred, kTransferTimeout.count());

  if (rc != 0) {
    std::println(stderr, "[ERROR] Failed to receive response: {}",
                 LibusbErrorToString(rc));
    return std::unexpected(LibusbErrorToUsbError(rc));
  }

  return transferred;
}

void UsbComm::SetIds(uint16_t vid, uint16_t pid) {
  vid_ = vid;
  pid_ = pid;
}
