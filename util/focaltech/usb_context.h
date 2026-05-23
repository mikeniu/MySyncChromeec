#ifndef UTIL_FOCALTECH_USB_CONTEXT_H_
#define UTIL_FOCALTECH_USB_CONTEXT_H_

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>

#include "ft_log.h"

enum class FtError {
  kSuccess = 0,
  kNoSuchDevice = -1,
  kParameterError = -2,
  kNoSuchFile = -3,
  kResultFail = -4,
  kInvalidMode = -5,
};

enum class FtDeviceMode {
  kUnknown,
  kRom,
  kBoot,
  kTest,
  kEc,
  kRo,
  kRw,
  kForceUpdate,
};

enum class FtDeviceAction {
  kNormal,
  kWpGet,
  kWpSet,
  kTest,
  kUsbEn,
  kUsbSuspend,
};

enum class FtFlashWpArea {
  kUnknown,
  kAll,
  kBoot,
  kRo,
  kRw,
  kOutRange,
};

constexpr uint16_t kFocalRomVid = 0x2FD0;
constexpr uint16_t kFocalBootVid = 0x2808;
constexpr uint16_t kFocalEcVid = 0x18D1;

constexpr uint16_t kFocalEcPidTest = 0xFACE;
constexpr uint16_t kFocalEcPid9865 = 0x5403;
constexpr uint16_t kFocalEcPid9869 = 0x5404;

constexpr int kUsbSendOnly = 2;

constexpr int kFtUsbDeviceRemoveMagic = 0x55aa;

struct FtDeviceInfo {
  FtDeviceMode current_mode = FtDeviceMode::kUnknown;
  FtDeviceMode next_mode = FtDeviceMode::kUnknown;
  FtDeviceAction action = FtDeviceAction::kNormal;
  uint32_t action_para = 0;
  uint32_t down_addr = 0;
  std::string_view version;
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
      LOGD("Close USB device handle");
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

  std::expected<int, FtError> Send(std::span<const uint8_t> buf,
                                   uint32_t timeout_ms) const;
  std::expected<int, FtError> Recv(std::span<uint8_t> buf,
                                   uint32_t timeout_ms) const;

  libusb_device_handle* get() const { return handle_.get(); }

  void ReleaseInterface(int interface_num) {
    if (handle_) {
      libusb_release_interface(handle_.get(), interface_num);
    }
  }

  // Global device removal flag (thread‑safe)
  static int GetDeviceRemovedFlag() { return g_device_removed_flag.load(); }
  static void ResetDeviceRemovedFlag() { g_device_removed_flag.store(0); }

  // Wait for device removal (hotplug event)
  int ResetComplete(int timeout_sec) const;  // renamed from ResetComplate

 private:
  // Constructor takes a libusb_context* for later event handling
  UsbDevice(libusb_device_handle* handle, uint8_t ep_in, uint8_t ep_out,
            libusb_context* ctx)
      : handle_(handle),
        endpoint_in_(ep_in),
        endpoint_out_(ep_out),
        ctx_(ctx) {}

  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle_;
  uint8_t endpoint_in_ = 0;
  uint8_t endpoint_out_ = 0;
  libusb_context* ctx_ = nullptr;  // Stored libusb context

  static std::atomic<int> g_device_removed_flag;  // Thread‑safe removal flag
};

#endif  // UTIL_FOCALTECH_USB_CONTEXT_H_