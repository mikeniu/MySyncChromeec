/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "libusb_transport.h"

#include <libusb.h>

#include <chrono>
#include <span>
#include <thread>

#include "ft_log.h"
#include "ft_util.h"

namespace focaltech {
namespace {

// Timing constants
using namespace std::chrono_literals;
constexpr auto kUsbEnumerationDelay = 200ms;
constexpr auto kPollinterval = 100ms;

// RAII wrapper for libusb device list
struct LibusbDeviceListDeleter {
  void operator()(libusb_device** list) const {
    if (list) libusb_free_device_list(list, /*unref_devices=*/1);
  }
};
using ScopedDeviceList =
    std::unique_ptr<libusb_device*, LibusbDeviceListDeleter>;

libusb_device* FindFocalDevice(std::span<libusb_device*> devices,
                               libusb_device_descriptor& out_desc) {
  for (libusb_device* dev : devices) {
    if (libusb_get_device_descriptor(dev, &out_desc) != LIBUSB_SUCCESS)
      continue;

    const uint16_t vid = out_desc.idVendor;
    const uint16_t pid = out_desc.idProduct;

    const bool is_boot = (vid == kFocalBootVid && pid == kFocalBootPid);
    const bool is_rom = (vid == kFocalRomVid && pid == kFocalRomPid);
    const bool is_ec = (vid == kFocalEcVid);

    if (is_boot || is_rom || is_ec) {
      if (is_ec) {
        const bool valid_fp = (pid == kFocalEcPidTest) ||
                              (pid == kFocalEcPid9865) ||
                              (pid == kFocalEcPid9869);
        if (!valid_fp) {
          FT_LOGI("Found Google device {:04x}:{:04x}, but not Focal. Skipping.",
                  vid, pid);
          continue;
        }
      }
      return dev;
    }
  }
  return nullptr;
}

// Helper structure to hold endpoint and interface info
struct EndpointInfo {
  uint8_t interface_num = 0;
  uint8_t ep_bulk_in = 0;
  uint8_t ep_bulk_out = 0;
};

// Locate bulk IN/OUT endpoints on the device
bool FindBulkEndpoints(libusb_device* dev, EndpointInfo& info) {
  libusb_config_descriptor* config = nullptr;
  if (libusb_get_config_descriptor(dev, 0, &config) != LIBUSB_SUCCESS)
    return false;

  bool found = false;
  for (int i = 0; i < config->bNumInterfaces && !found; ++i) {
    const libusb_interface& iface = config->interface[i];
    for (int j = 0; j < iface.num_altsetting && !found; ++j) {
      const libusb_interface_descriptor& alt = iface.altsetting[j];
      info.interface_num = alt.bInterfaceNumber;

      for (int k = 0; k < alt.bNumEndpoints; ++k) {
        const libusb_endpoint_descriptor& ep = alt.endpoint[k];
        if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
            LIBUSB_TRANSFER_TYPE_BULK) {
          if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
              LIBUSB_ENDPOINT_IN)
            info.ep_bulk_in = ep.bEndpointAddress;
          else
            info.ep_bulk_out = ep.bEndpointAddress;
        }
      }
      if (info.ep_bulk_in != 0 && info.ep_bulk_out != 0) found = true;
    }
  }
  libusb_free_config_descriptor(config);
  return found;
}

}  // anonymous namespace

// --------------------------------------------------------------------------
// Deleter implementations
// --------------------------------------------------------------------------
void LibusbContextDeleter::operator()(libusb_context* ctx) const {
  if (ctx) libusb_exit(ctx);
}

void LibusbDeviceHandleDeleter::operator()(libusb_device_handle* handle) const {
  if (handle) libusb_close(handle);
}

// --------------------------------------------------------------------------
// Step 1: Find a Focal device (no opening)
// --------------------------------------------------------------------------
std::expected<FoundDeviceInfo, FtError> LibusbTransport::FindDevice() {
  // 1. Initialize libusb context
  libusb_context* raw_ctx = nullptr;
  if (libusb_init(&raw_ctx) != LIBUSB_SUCCESS) {
    FT_LOGE("Failed to initialize libusb");
    return std::unexpected(FtError::kResultFail);
  }
  std::unique_ptr<libusb_context, LibusbContextDeleter> ctx(raw_ctx);

  // 2. Get device list
  libusb_device** raw_list = nullptr;
  ssize_t count = libusb_get_device_list(ctx.get(), &raw_list);
  if (count < 0) {
    FT_LOGE("Failed to get USB device list: {}", libusb_error_name(count));
    return std::unexpected(FtError::kNoSuchDevice);
  }
  ScopedDeviceList dev_list(raw_list);

  // 3. Find target device
  libusb_device_descriptor desc;
  auto devices = std::span(raw_list, static_cast<size_t>(count));
  libusb_device* target = FindFocalDevice(devices, desc);
  if (!target) {
    FT_LOGE("No matching Focal device found");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // Increase reference count on the device so it remains valid after the device
  // list is freed
  libusb_ref_device(target);

  // Release the device list (all devices are unref'ed, but target has an extra
  // ref)
  dev_list.reset();

  UsbDeviceId id{.vid = desc.idVendor, .pid = desc.idProduct};
  FT_LOGI("Found device VID {:04x}: PID {:04x}", id.vid, id.pid);

  return FoundDeviceInfo{std::move(ctx), target, id};
}

// --------------------------------------------------------------------------
// Constructor (lazy open – does not open device)
// --------------------------------------------------------------------------
LibusbTransport::LibusbTransport(FoundDeviceInfo info)
    : ctx_(std::move(info.ctx)),
      device_(info.dev),
      device_id_(info.id),
      handle_(nullptr),
      endpoint_in_(0),
      endpoint_out_(0),
      interface_num_(0),
      opened_(false) {}

// --------------------------------------------------------------------------
// Destructor: release device reference
// --------------------------------------------------------------------------
LibusbTransport::~LibusbTransport() {
  if (device_) {
    libusb_unref_device(device_);
    device_ = nullptr;
  }
}

// --------------------------------------------------------------------------
// Ensure device is opened (lazy initialization)
// --------------------------------------------------------------------------
std::expected<void, FtError> LibusbTransport::EnsureOpened() {
  if (opened_) {
    return {};
  }

  FT_LOGD("start open usb device");

  if (!device_) {
    FT_LOGE("Invalid device pointer");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // 1. Open the device
  libusb_device_handle* raw_handle = nullptr;
  int r = libusb_open(device_, &raw_handle);
  if (r != LIBUSB_SUCCESS || !raw_handle) {
    FT_LOGE("Failed to open device: {}", libusb_error_name(r));
    return std::unexpected(FtError::kNoSuchDevice);
  }
  handle_.reset(raw_handle);

  // 2. Find bulk endpoints
  EndpointInfo ep_info;
  if (!FindBulkEndpoints(device_, ep_info)) {
    FT_LOGE("Could not find required bulk IN/OUT endpoints");
    return std::unexpected(FtError::kNoSuchDevice);
  }
  endpoint_in_ = ep_info.ep_bulk_in;
  endpoint_out_ = ep_info.ep_bulk_out;
  interface_num_ = ep_info.interface_num;

  // 3. Detach kernel driver if active
  if (libusb_kernel_driver_active(handle_.get(), interface_num_) == 1) {
    if (libusb_detach_kernel_driver(handle_.get(), interface_num_) !=
        LIBUSB_SUCCESS) {
      FT_LOGE("Failed to detach kernel driver");
      return std::unexpected(FtError::kResultFail);
    }
  }

  // 4. Claim the interface
  if (libusb_claim_interface(handle_.get(), interface_num_) < 0) {
    FT_LOGE("Failed to claim interface");
    return std::unexpected(FtError::kResultFail);
  }

  // 5. We no longer need the separate device reference (handle holds its own)
  if (device_) {
    libusb_unref_device(device_);
    device_ = nullptr;
  }

  opened_ = true;
  return {};
}

/*Only locate the Focal device to support the following scenario:
1.The system image contains this tool that automatically detects whether the
fingerprint device is in bootloader mode and automatically upgrades the
firmware.
2.After the upgrade, the fingerprint device is in FtDeviceMode::kEc,
and this tool must not perform USB claim / libusb_detach_kernel_driver
operations on the fingerprint device, as that could affect fingerprint
functionality.*/

std::expected<LibusbTransport, FtError> LibusbTransport::Create() {
  auto find_res = FindDevice();
  if (!find_res) {
    return std::unexpected(find_res.error());
  }
  return LibusbTransport(std::move(*find_res));
}

// --------------------------------------------------------------------------
// Send data over bulk OUT endpoint (lazy open)
// --------------------------------------------------------------------------
std::expected<int, FtError> LibusbTransport::Send(
    std::span<const uint8_t> data, std::chrono::milliseconds timeout) {
  auto open_res = EnsureOpened();
  if (!open_res) {
    return std::unexpected(open_res.error());
  }

  int actual = 0;
  int r = libusb_bulk_transfer(handle_.get(), endpoint_out_,
                               const_cast<uint8_t*>(data.data()),
                               static_cast<int>(data.size()), &actual,
                               static_cast<unsigned int>(timeout.count()));
  if (r != LIBUSB_SUCCESS) return HandleTransferError(r);
  return actual;
}

// --------------------------------------------------------------------------
// Receive data over bulk IN endpoint (lazy open)
// --------------------------------------------------------------------------
std::expected<int, FtError> LibusbTransport::Recv(
    std::span<uint8_t> data, std::chrono::milliseconds timeout) {
  auto open_res = EnsureOpened();
  if (!open_res) {
    return std::unexpected(open_res.error());
  }

  int actual = 0;
  int r = libusb_bulk_transfer(handle_.get(), endpoint_in_, data.data(),
                               static_cast<int>(data.size()), &actual,
                               static_cast<unsigned int>(timeout.count()));
  if (r != LIBUSB_SUCCESS) return HandleTransferError(r);
  return std::min(actual, static_cast<int>(data.size()));
}

// --------------------------------------------------------------------------
// Wait for device removal/reset (hotplug event)
// --------------------------------------------------------------------------
std::expected<void, FtError> LibusbTransport::WaitForReset(
    std::chrono::milliseconds timeout) {
  auto open_res = EnsureOpened();
  if (!open_res) {
    return std::unexpected(open_res.error());
  }

  const auto start = std::chrono::steady_clock::now();
  while (true) {
    int config = 0;
    int r = libusb_get_configuration(handle_.get(), &config);
    if (r == LIBUSB_ERROR_NO_DEVICE) {
      FT_LOGI("USB device removal/reset detected");
      return {};
    } else if (r != LIBUSB_SUCCESS) {
      FT_LOGV("libusb_get_configuration returned {}", r);
    }

    if (timeout != std::chrono::milliseconds::zero() &&
        (std::chrono::steady_clock::now() - start) >= timeout) {
      break;
    }
    std::this_thread::sleep_for(kPollinterval);
  }

  FT_LOGW("Timeout after {} ms waiting for device removal", timeout.count());
  return std::unexpected(FtError::kResultFail);
}

// --------------------------------------------------------------------------
// Handle bulk transfer error: reset device, wait, map to FtError
// --------------------------------------------------------------------------
std::expected<int, FtError> LibusbTransport::HandleTransferError(
    int libusb_error) const {
  if (!handle_) return std::unexpected(FtError::kNoSuchDevice);
  // Attempt to reset the device to recover
  libusb_reset_device(handle_.get());
  std::this_thread::sleep_for(kUsbEnumerationDelay);

  if (libusb_error == LIBUSB_ERROR_NO_DEVICE)
    return std::unexpected(FtError::kNoSuchDevice);
  return std::unexpected(FtError::kResultFail);
}

}  // namespace focaltech
