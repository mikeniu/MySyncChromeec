/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_USB_CONTEXT_H_
#define UTIL_FOCALTECH_USB_CONTEXT_H_

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>

// Third-party libraries come AFTER standard C++ libraries
#include <libusb-1.0/libusb.h>

#include "ft_util.h"

enum class FtDeviceMode {
  kUnknown,
  kRom,
  kBoot,
  kTest,
  kEc,
  kForceUpdate,
};

enum class FtFlashWpArea {
  kUnknown,
  kAll,
  kBoot,
  kRo,
  kRw,
  kOutRange,
};

enum class TransferDirection { kIn, kOut, kSendOnly };

constexpr uint16_t kFocalRomVid = 0x2FD0;
constexpr uint16_t kFocalRomPid = 0x0000;

constexpr uint16_t kFocalBootVid = 0x2808;
constexpr uint16_t kFocalBootPid = 0x0001;

constexpr uint16_t kFocalEcVid = 0x18D1;
constexpr uint16_t kFocalEcPidTest = 0xFACE;
constexpr uint16_t kFocalEcPid9865 = 0x5403;
constexpr uint16_t kFocalEcPid9869 = 0x5404;

// Centralized, strongly-typed timing configurations
using namespace std::chrono_literals;
constexpr auto kFastUsbTimeout = 200ms;
constexpr auto kSlowUsbTimeout = 1000ms;
constexpr auto kMcuRebootPollInterval = 10ms;
constexpr auto kPollinterval = 100ms;
constexpr auto kResetWaitTimeout = 3s;
constexpr auto kUsbEnumerationDelay = 200ms;

// --- USB Transfer Timeouts ---
// Before bootloader v1.8, erasing the flash takes about 4.4 seconds.
constexpr auto kErasePageTimeout = 6000ms;
constexpr auto kDownloadPageTimeout = kSlowUsbTimeout;
constexpr auto kVerifyHashTimeout = kSlowUsbTimeout;
constexpr auto kMcuResetTimeout = kFastUsbTimeout;
constexpr auto kGenericInfoTimeout = kFastUsbTimeout;

struct FtDeviceInfo {
  FtDeviceMode current_mode = FtDeviceMode::kUnknown;
  FtDeviceMode next_mode = FtDeviceMode::kUnknown;
  std::string_view ec_bin;
};

struct LibusbContextDeleter {
  void operator()(libusb_context* ctx) const {
    if (ctx != nullptr) {
      libusb_exit(ctx);
    }
  }
};

struct LibusbDeviceHandleDeleter {
  void operator()(libusb_device_handle* handle) const {
    if (handle != nullptr) {
      libusb_close(handle);
    }
  }
};

class UsbContext {
 public:
  UsbContext() {
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) == LIBUSB_SUCCESS) {
      ctx_.reset(ctx);
    }
  }

  [[nodiscard]] bool IsValid() const { return ctx_ != nullptr; }
  libusb_context* get() const { return ctx_.get(); }

 private:
  std::unique_ptr<libusb_context, LibusbContextDeleter> ctx_;
};

struct UsbConnectionDetails {
  uint16_t vid = 0;
  uint16_t pid = 0;
  uint8_t interface_num = 0;
  uint8_t ep_bulk_in = 0;
  uint8_t ep_bulk_out = 0;
};

class UsbDevice {
 public:
  static std::expected<UsbDevice, FtError> Create(
      const UsbContext& ctx, UsbConnectionDetails& out_details);

  // Move constructor
  UsbDevice(UsbDevice&& other) noexcept
      : handle_(std::move(other.handle_)),
        endpoint_in_(other.endpoint_in_),
        endpoint_out_(other.endpoint_out_),
        ctx_(other.ctx_),
        cdb_tag_(std::move(other.cdb_tag_)) {
    other.endpoint_in_ = 0;
    other.endpoint_out_ = 0;
    other.ctx_ = nullptr;
  }

  // Move assignment
  UsbDevice& operator=(UsbDevice&& other) noexcept {
    if (this != &other) {
      handle_ = std::move(other.handle_);
      endpoint_in_ = other.endpoint_in_;
      endpoint_out_ = other.endpoint_out_;
      ctx_ = other.ctx_;
      cdb_tag_ = std::move(other.cdb_tag_);
      other.endpoint_in_ = 0;
      other.endpoint_out_ = 0;
      other.ctx_ = nullptr;
    }
    return *this;
  }

  UsbDevice(const UsbDevice&) = delete;
  UsbDevice& operator=(const UsbDevice&) = delete;

  std::expected<int, FtError> Send(std::span<const uint8_t> buf,
                                   std::chrono::milliseconds timeout_ms) const;
  std::expected<int, FtError> Recv(std::span<uint8_t> buf,
                                   std::chrono::milliseconds timeout_ms) const;

  // Wait for device removal (hotplug event)
  std::expected<void, FtError> ResetComplete(
      std::chrono::milliseconds timeout) const;

  libusb_device_handle* get() const { return handle_.get(); }

  uint32_t GetNextCdbTag() const;

 private:
  // Constructor takes a libusb_context* for later event handling
  UsbDevice(libusb_device_handle* handle, uint8_t ep_in, uint8_t ep_out,
            libusb_context* ctx)
      : handle_(handle),
        endpoint_in_(ep_in),
        endpoint_out_(ep_out),
        ctx_(ctx),
        cdb_tag_(std::make_unique<std::atomic<uint32_t>>(1)) {}

  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle_;
  uint8_t endpoint_in_ = 0;
  uint8_t endpoint_out_ = 0;
  libusb_context* ctx_ = nullptr;  // Stored libusb context

  // Encapsulated state for SCSI command sequence matching.
  // Marked mutable so it can be incremented even when UsbDevice is passed as
  // const.
  mutable std::unique_ptr<std::atomic<uint32_t>> cdb_tag_;
  std::expected<int, FtError> HandleTransferError(int libusb_error) const;
};

#endif  // UTIL_FOCALTECH_USB_CONTEXT_H_
