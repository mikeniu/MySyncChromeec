/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "libusb_transport.h"

#include <libusb.h>

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <thread>

#include "ft_log.h"
#include "ft_util.h"
#include "usb_device.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;
constexpr auto kUsbEnumerationDelay = 200ms;
constexpr auto kPollinterval = 100ms;
constexpr uint8_t kDefaultUsbConfigIndex = 0;

struct LibusbDeviceListDeleter {
  void operator()(libusb_device** list) const {
    if (list) libusb_free_device_list(list, /*unref_devices=*/1);
  }
};

using ScopedDeviceList =
    std::unique_ptr<libusb_device*, LibusbDeviceListDeleter>;

libusb_device* FindFocalDevice(std::span<libusb_device*> devices,
                               libusb_device_descriptor& out_desc,
                               UsbDeviceId& out_id) {
  for (libusb_device* dev : devices) {
    if (libusb_get_device_descriptor(dev, &out_desc) != LIBUSB_SUCCESS)
      continue;

    UsbDeviceId current_id{.vid = out_desc.idVendor, .pid = out_desc.idProduct};

    if (GetDeviceMode(current_id).has_value()) {
      out_id = current_id;
      return dev;
    }

    if (current_id.vid == kGoogleVid) {
      FT_LOGI("Found Google device {:04x}:{:04x}, but not Focal. Skipping.",
              current_id.vid, current_id.pid);
    }
  }
  return nullptr;
}

struct EndpointInfo {
  uint8_t interface_num = 0;
  uint8_t ep_bulk_in = 0;
  uint8_t ep_bulk_out = 0;
};

std::optional<EndpointInfo> ParseAltSetting(
    const libusb_interface_descriptor& alt) {
  EndpointInfo info{.interface_num = alt.bInterfaceNumber};

  for (const auto& ep : std::span(alt.endpoint, alt.bNumEndpoints)) {
    if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) !=
        LIBUSB_TRANSFER_TYPE_BULK) {
      continue;
    }

    if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
        LIBUSB_ENDPOINT_IN) {
      info.ep_bulk_in = ep.bEndpointAddress;
    } else {
      info.ep_bulk_out = ep.bEndpointAddress;
    }
  }

  if (info.ep_bulk_in != 0 && info.ep_bulk_out != 0) {
    return info;
  }
  return std::nullopt;
}

std::optional<EndpointInfo> FindBulkEndpoints(libusb_device* dev) {
  libusb_config_descriptor* raw_config = nullptr;
  if (libusb_get_config_descriptor(dev, kDefaultUsbConfigIndex, &raw_config) !=
      LIBUSB_SUCCESS) {
    return std::nullopt;
  }

  std::unique_ptr<libusb_config_descriptor, LibusbConfigDeleter> config(
      raw_config);

  for (const auto& iface :
       std::span(config->interface, config->bNumInterfaces)) {
    for (const auto& alt : std::span(iface.altsetting, iface.num_altsetting)) {
      if (auto info = ParseAltSetting(alt)) {
        return info;
      }
    }
  }

  return std::nullopt;
}

}  // namespace

void LibusbContextDeleter::operator()(libusb_context* ctx) const {
  if (ctx) libusb_exit(ctx);
}

void LibusbDeviceHandleDeleter::operator()(libusb_device_handle* handle) const {
  if (handle) libusb_close(handle);
}

void LibusbDeviceDeleter::operator()(libusb_device* dev) const {
  if (dev) libusb_unref_device(dev);
}

void LibusbConfigDeleter::operator()(libusb_config_descriptor* config) const {
  if (config) libusb_free_config_descriptor(config);
}

std::expected<LibusbTransport, Error> LibusbTransport::Create() {
  libusb_context* raw_ctx = nullptr;
  if (libusb_init(&raw_ctx) != LIBUSB_SUCCESS) {
    FT_LOGE("Failed to initialize libusb");
    return std::unexpected(Error::kResultFail);
  }
  std::unique_ptr<libusb_context, LibusbContextDeleter> ctx(raw_ctx);

  libusb_device** raw_list = nullptr;
  ssize_t count = libusb_get_device_list(ctx.get(), &raw_list);
  if (count < 0) {
    FT_LOGE("Failed to get USB device list: {}", libusb_error_name(count));
    return std::unexpected(Error::kNoSuchDevice);
  }
  ScopedDeviceList dev_list(raw_list);

  libusb_device_descriptor desc;
  UsbDeviceId found_id;
  auto devices = std::span(raw_list, static_cast<size_t>(count));
  libusb_device* target = FindFocalDevice(devices, desc, found_id);
  if (!target) {
    FT_LOGE("No matching Focal device found");
    return std::unexpected(Error::kNoSuchDevice);
  }

  // Increase reference count on the device so it remains valid after the device
  // list is freed
  libusb_ref_device(target);
  dev_list.reset();

  FT_LOGI("Found device VID {:04x}: PID {:04x}", found_id.vid, found_id.pid);

  return LibusbTransport(std::move(ctx), target, found_id);
}

LibusbTransport::LibusbTransport(
    std::unique_ptr<libusb_context, LibusbContextDeleter> ctx,
    libusb_device* dev, UsbDeviceId id)
    : ctx_(std::move(ctx)),
      device_(dev),
      device_id_(id),
      handle_(nullptr),
      endpoint_in_(0),
      endpoint_out_(0),
      interface_num_(0),
      opened_(false) {}

LibusbTransport::~LibusbTransport() = default;

std::expected<void, Error> LibusbTransport::EnsureOpened() {
  if (opened_) {
    return {};
  }

  FT_LOGD("start open usb device");

  if (!device_) {
    FT_LOGE("Invalid device pointer");
    return std::unexpected(Error::kNoSuchDevice);
  }

  libusb_device_handle* raw_handle = nullptr;
  int r = libusb_open(device_.get(), &raw_handle);
  if (r != LIBUSB_SUCCESS || !raw_handle) {
    FT_LOGE("Failed to open device: {}", libusb_error_name(r));
    return std::unexpected(Error::kNoSuchDevice);
  }
  handle_.reset(raw_handle);

  auto ep_info = FindBulkEndpoints(device_.get());
  if (!ep_info) {
    FT_LOGE("Could not find required bulk IN/OUT endpoints");
    return std::unexpected(Error::kNoSuchDevice);
  }
  endpoint_in_ = ep_info->ep_bulk_in;
  endpoint_out_ = ep_info->ep_bulk_out;
  interface_num_ = ep_info->interface_num;

  if (libusb_kernel_driver_active(handle_.get(), interface_num_)) {
    if (libusb_detach_kernel_driver(handle_.get(), interface_num_) !=
        LIBUSB_SUCCESS) {
      FT_LOGE("Failed to detach kernel driver");
      return std::unexpected(Error::kResultFail);
    }
  }

  if (libusb_claim_interface(handle_.get(), interface_num_) != LIBUSB_SUCCESS) {
    FT_LOGE("Failed to claim interface");
    return std::unexpected(Error::kResultFail);
  }

  device_.reset();

  opened_ = true;
  return {};
}

/* Only locate the Focal device to support the following scenario:
1. The system image contains this tool that automatically detects whether the
fingerprint device is in bootloader mode and automatically upgrades the
firmware.
2. After the upgrade, the fingerprint device is in DeviceMode::kEc,
and this tool must not perform USB claim / libusb_detach_kernel_driver
operations on the fingerprint device, as that could affect fingerprint
functionality. */

std::expected<int, Error> LibusbTransport::Send(
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

std::expected<int, Error> LibusbTransport::Recv(
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

std::expected<void, Error> LibusbTransport::WaitForReset(
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
  return std::unexpected(Error::kResultFail);
}

std::expected<int, Error> LibusbTransport::HandleTransferError(
    int libusb_error) const {
  if (!handle_) return std::unexpected(Error::kNoSuchDevice);
  libusb_reset_device(handle_.get());
  std::this_thread::sleep_for(kUsbEnumerationDelay);

  if (libusb_error == LIBUSB_ERROR_NO_DEVICE)
    return std::unexpected(Error::kNoSuchDevice);
  return std::unexpected(Error::kResultFail);
}

}  // namespace focaltech
