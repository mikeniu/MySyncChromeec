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
// Factory: initialize libusb, find device, open, claim interface
// --------------------------------------------------------------------------
std::expected<LibusbTransport, FtError> LibusbTransport::Create() {
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
  UsbDeviceId id{desc.idVendor, desc.idProduct};
  FT_LOGI("Found device VID {:04x}: PID {:04x}", id.vid, id.pid);

  // 4. Open device
  libusb_device_handle* raw_handle = nullptr;
  int r = libusb_open(target, &raw_handle);
  if (r != LIBUSB_SUCCESS || !raw_handle) {
    FT_LOGE("Failed to open device: {}", libusb_error_name(r));
    return std::unexpected(FtError::kNoSuchDevice);
  }
  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle(
      raw_handle);

  // 5. Find bulk endpoints
  EndpointInfo ep_info;
  if (!FindBulkEndpoints(target, ep_info)) {
    FT_LOGE("Could not find required bulk IN/OUT endpoints");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // 6. Detach kernel driver if active
  if (libusb_kernel_driver_active(handle.get(), ep_info.interface_num) == 1) {
    if (libusb_detach_kernel_driver(handle.get(), ep_info.interface_num) !=
        LIBUSB_SUCCESS) {
      FT_LOGE("Failed to detach kernel driver");
      return std::unexpected(FtError::kResultFail);
    }
  }

  // 7. Claim the interface – no proactive device reset needed.
  if (libusb_claim_interface(handle.get(), ep_info.interface_num) < 0) {
    FT_LOGE("Failed to claim interface");
    return std::unexpected(FtError::kResultFail);
  }

  // 8. Construct and return the transport object
  return LibusbTransport(std::move(ctx), std::move(handle), id,
                         ep_info.ep_bulk_in, ep_info.ep_bulk_out);
}

// --------------------------------------------------------------------------
// Private constructor
// --------------------------------------------------------------------------
LibusbTransport::LibusbTransport(
    std::unique_ptr<libusb_context, LibusbContextDeleter> ctx,
    std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter> handle,
    UsbDeviceId id, uint8_t ep_in, uint8_t ep_out)
    : ctx_(std::move(ctx)),
      handle_(std::move(handle)),
      device_id_(id),
      endpoint_in_(ep_in),
      endpoint_out_(ep_out) {}

// --------------------------------------------------------------------------
// Send data over bulk OUT endpoint
// --------------------------------------------------------------------------
std::expected<int, FtError> LibusbTransport::Send(
    std::span<const uint8_t> data, std::chrono::milliseconds timeout) {
  if (!handle_) return std::unexpected(FtError::kNoSuchDevice);

  int actual = 0;
  int r = libusb_bulk_transfer(handle_.get(), endpoint_out_,
                               const_cast<uint8_t*>(data.data()),
                               static_cast<int>(data.size()), &actual,
                               static_cast<unsigned int>(timeout.count()));
  if (r != LIBUSB_SUCCESS) return HandleTransferError(r);
  return actual;
}

// --------------------------------------------------------------------------
// Receive data over bulk IN endpoint
// --------------------------------------------------------------------------
std::expected<int, FtError> LibusbTransport::Recv(
    std::span<uint8_t> data, std::chrono::milliseconds timeout) {
  if (!handle_) return std::unexpected(FtError::kNoSuchDevice);

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
  if (!handle_) {
    FT_LOGE("Invalid device handle");
    return std::unexpected(FtError::kNoSuchDevice);
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
  // Attempt to reset the device to recover
  libusb_reset_device(handle_.get());
  std::this_thread::sleep_for(kUsbEnumerationDelay);

  if (libusb_error == LIBUSB_ERROR_NO_DEVICE)
    return std::unexpected(FtError::kNoSuchDevice);
  return std::unexpected(FtError::kResultFail);
}

}  // namespace focaltech
