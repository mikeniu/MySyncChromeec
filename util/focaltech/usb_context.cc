/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <chrono>
#include <thread>
#include <vector>

#include "ft_log.h"
#include "ft_util.h"

// To prevent the formatter from rearranging the header include order.
#include "usb_context.h"

namespace {

struct LibusbDeviceListDeleter {
  void operator()(libusb_device** list) const {
    if (list != nullptr) {
      libusb_free_device_list(list, /*unref_devices=*/1);
    }
  }
};

using ScopedDeviceList =
    std::unique_ptr<libusb_device*, LibusbDeviceListDeleter>;

}  // namespace

libusb_device* FindFocalDevice(libusb_device** device_list, ssize_t num_devices,
                               libusb_device_descriptor& out_desc) {
  for (ssize_t i = 0; i < num_devices; ++i) {
    libusb_device* dev = device_list[i];
    if (libusb_get_device_descriptor(dev, &out_desc) != LIBUSB_SUCCESS) {
      continue;  // Skip if descriptor cannot be read
    }

    const uint16_t vid = out_desc.idVendor;
    const uint16_t pid = out_desc.idProduct;

    // Check for known Focal device VID/PID combinations
    const bool is_boot_device = (vid == kFocalBootVid && pid == kFocalBootPid);
    const bool is_rom_device = (vid == kFocalRomVid && pid == kFocalRomPid);
    const bool is_ec_device = (vid == kFocalEcVid);

    if (is_boot_device || is_rom_device || is_ec_device) {
      // For EC devices, ensure it's a fingerprint model
      if (is_ec_device) {
        const bool is_valid_fingerprint = (pid == kFocalEcPidTest) ||
                                          (pid == kFocalEcPid9865) ||
                                          (pid == kFocalEcPid9869);
        if (!is_valid_fingerprint) {
          LOGI("Found Google device {:04x}:{:04x}, but not Focal. Skipping.",
               vid, pid);
          continue;
        }
      }
      return dev;  // Matching device found
    }
  }
  return nullptr;  // No matching device
}

bool FindBulkEndpoints(libusb_device* dev, UsbConnectionDetails& details) {
  libusb_config_descriptor* config = nullptr;
  if (libusb_get_config_descriptor(dev, 0, &config) != LIBUSB_SUCCESS) {
    return false;
  }

  bool found_endpoints = false;

  for (int j = 0; j < config->bNumInterfaces && !found_endpoints; ++j) {
    const libusb_interface& interface = config->interface[j];

    for (int k = 0; k < interface.num_altsetting && !found_endpoints; ++k) {
      const libusb_interface_descriptor& alt = interface.altsetting[k];
      details.interface_num = alt.bInterfaceNumber;

      for (int l = 0; l < alt.bNumEndpoints; ++l) {
        const libusb_endpoint_descriptor& ep = alt.endpoint[l];

        if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
            LIBUSB_TRANSFER_TYPE_BULK) {
          if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
              LIBUSB_ENDPOINT_IN) {
            details.ep_bulk_in = ep.bEndpointAddress;
          } else {
            details.ep_bulk_out = ep.bEndpointAddress;
          }
        }
      }
      if (details.ep_bulk_in != 0 && details.ep_bulk_out != 0) {
        found_endpoints = true;
      }
    }
  }

  libusb_free_config_descriptor(config);
  return found_endpoints;
}

std::expected<UsbDevice, FtError> UsbDevice::Create(
    const UsbContext& ctx, UsbConnectionDetails& out_details) {
  libusb_device** list_raw = nullptr;
  ssize_t count = libusb_get_device_list(ctx.get(), &list_raw);
  if (count < 0) {
    LOGE("Failed to get USB device list: {}", libusb_error_name(count));
    return std::unexpected(FtError::kNoSuchDevice);
  }
  ScopedDeviceList dev_list(list_raw);

  // 1. Find Device
  libusb_device_descriptor target_desc{};
  libusb_device* target_dev =
      FindFocalDevice(dev_list.get(), count, target_desc);
  if (!target_dev) {
    return std::unexpected(FtError::kNoSuchDevice);
  }

  out_details.vid = target_desc.idVendor;
  out_details.pid = target_desc.idProduct;
  LOGI("Found device VID {:04x}: PID {:04x}", out_details.vid, out_details.pid);

  // 2. Open Handle
  libusb_device_handle* handle = nullptr;
  int r = libusb_open(target_dev, &handle);
  if (r != LIBUSB_SUCCESS || !handle) {
    LOGE("Failed to open device: {}", libusb_error_name(r));
    return std::unexpected(FtError::kNoSuchDevice);
  }
  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter>
      handle_holder(handle);

  // 3. Find Endpoints
  if (!FindBulkEndpoints(target_dev, out_details)) {
    LOGE("Could not find required bulk IN/OUT endpoints.");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // 4. Initialize Connection (Detach, Reset, Claim)
  if (libusb_kernel_driver_active(handle_holder.get(),
                                  out_details.interface_num) == 1) {
    if (libusb_detach_kernel_driver(
            handle_holder.get(), out_details.interface_num) != LIBUSB_SUCCESS) {
      LOGE("Failed to detach kernel driver.");
      return std::unexpected(FtError::kResultFail);
    }
  }
  if (libusb_reset_device(handle_holder.get()) < 0) {
    LOGE("Failed to reset the device");
    return std::unexpected(FtError::kResultFail);
  }
  std::this_thread::sleep_for(kUsbEnumerationDelay);

  if (libusb_claim_interface(handle_holder.get(), out_details.interface_num) <
      0) {
    LOGE("Failed to claim interface.");
    return std::unexpected(FtError::kResultFail);
  }

  // 5. Transfer ownership to the class
  return UsbDevice(handle_holder.release(), out_details.ep_bulk_in,
                   out_details.ep_bulk_out, ctx.get());
}

std::expected<int, FtError> UsbDevice::HandleTransferError(
    int libusb_error) const {
  libusb_reset_device(handle_.get());
  std::this_thread::sleep_for(kUsbEnumerationDelay);

  if (libusb_error == LIBUSB_ERROR_NO_DEVICE) {
    return std::unexpected(FtError::kNoSuchDevice);
  }
  return std::unexpected(FtError::kResultFail);
}

std::expected<int, FtError> UsbDevice::Send(
    std::span<const uint8_t> buf, std::chrono::milliseconds timeout_ms) const {
  if (!handle_) return std::unexpected(FtError::kNoSuchDevice);

  int actual_len = 0;
  int r = libusb_bulk_transfer(handle_.get(), endpoint_out_,
                               const_cast<uint8_t*>(buf.data()),
                               static_cast<int>(buf.size()), &actual_len,
                               static_cast<unsigned int>(timeout_ms.count()));

  if (r != LIBUSB_SUCCESS) {
    return HandleTransferError(r);
  }
  return actual_len;
}

std::expected<int, FtError> UsbDevice::Recv(
    std::span<uint8_t> buf, std::chrono::milliseconds timeout_ms) const {
  if (!handle_) return std::unexpected(FtError::kNoSuchDevice);

  int actual_len = 0;
  int r = libusb_bulk_transfer(handle_.get(), endpoint_in_, buf.data(),
                               static_cast<int>(buf.size()), &actual_len,
                               static_cast<unsigned int>(timeout_ms.count()));

  if (r != LIBUSB_SUCCESS) {
    return HandleTransferError(r);
  }
  return std::min(actual_len, static_cast<int>(buf.size()));
}

std::expected<void, FtError> UsbDevice::ResetComplete(
    std::chrono::milliseconds timeout) const {
  if (!handle_) {
    LOGE("Invalid device handle");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  constexpr auto poll_interval = kPollinterval;

  const auto start_time = std::chrono::steady_clock::now();

  while (true) {
    int config = 0;
    int r = libusb_get_configuration(handle_.get(), &config);

    if (r == LIBUSB_ERROR_NO_DEVICE) {
      LOGI("USB device removal/reset detected");
      return {};
    } else {
      LOGV("r={}", r);
    }

    auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (timeout != std::chrono::milliseconds::zero() && elapsed >= timeout) {
      break;
    }

    std::this_thread::sleep_for(poll_interval);
  }

  LOGW("Timeout after {} ms waiting for device removal", timeout.count());
  return std::unexpected(FtError::kResultFail);
}

uint32_t UsbDevice::GetNextCdbTag() const {
  // fetch_add returns the value before addition, matching post‑increment
  // behavior.
  return cdb_tag_->fetch_add(1, std::memory_order_relaxed);
}
