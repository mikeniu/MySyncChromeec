<<<<<<< PATCH SET (1453e788f70905e1537b7037445d9018459c2ba0 Convert the code to C++.  Remove USB suspend/resume function)
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_context.h"

#include <chrono>
#include <thread>
#include <vector>

#include "ft_log.h"
#include "ft_util.h"

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

std::expected<UsbDevice, FtError> UsbDevice::Create(
    const UsbContext& ctx, UsbConnectionDetails& out_details) {
  libusb_device** list_raw = nullptr;
  ssize_t count = libusb_get_device_list(ctx.get(), &list_raw);
  if (count < 0) {
    LOGE("Failed to get USB device list: {}", libusb_error_name(count));
    return std::unexpected(FtError::kNoSuchDevice);
  }

  ScopedDeviceList dev_list(list_raw);
  libusb_device* target_dev = nullptr;
  libusb_device_descriptor target_desc{};

  // Find target Focal device
  for (ssize_t i = 0; i < count; ++i) {
    libusb_device* dev = dev_list.get()[i];
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(dev, &desc) < 0) {
      continue;
    }

    if (desc.idVendor == kFocalBootVid || desc.idVendor == kFocalRomVid ||
        desc.idVendor == kFocalEcVid) {
      if (desc.idVendor == kFocalEcVid && (desc.idProduct != kFocalEcPidTest &&
                                           desc.idProduct != kFocalEcPid9865 &&
                                           desc.idProduct != kFocalEcPid9869)) {
        LOGI(
            "Found Google device {:04x}:{:04x}, but not Focal fingerprint."
            "Skipping.",
            desc.idVendor, desc.idProduct);
        continue;
      }

      LOGI("Found device VID {:04x}: PID {:04x}", desc.idVendor,
           desc.idProduct);
      target_dev = dev;
      target_desc = desc;
      break;
    }
  }

  if (target_dev == nullptr) {
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // Open device
  libusb_device_handle* handle = nullptr;
  int r = libusb_open(target_dev, &handle);
  if (r != LIBUSB_SUCCESS || handle == nullptr) {
    LOGE("Failed to open device: {}", libusb_error_name(r));
    return std::unexpected(FtError::kNoSuchDevice);
  }

  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter>
      handle_holder(handle);

  // Get configuration descriptor
  libusb_config_descriptor* config = nullptr;
  r = libusb_get_config_descriptor(target_dev, 0, &config);
  if (r != LIBUSB_SUCCESS) {
    LOGE("Couldn't retrieve configuration descriptors.");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // Find bulk IN/OUT endpoints
  bool found_endpoints = false;
  uint8_t ep_in = 0, ep_out = 0;
  uint8_t interface_num = 0;

  for (int j = 0; j < config->bNumInterfaces && !found_endpoints; ++j) {
    const libusb_interface& interface = config->interface[j];
    for (int k = 0; k < interface.num_altsetting && !found_endpoints; ++k) {
      const libusb_interface_descriptor& alt = interface.altsetting[k];
      interface_num = alt.bInterfaceNumber;

      for (int l = 0; l < alt.bNumEndpoints; ++l) {
        const libusb_endpoint_descriptor& ep = alt.endpoint[l];
        if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
            LIBUSB_TRANSFER_TYPE_BULK) {
          if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
              LIBUSB_ENDPOINT_IN) {
            ep_in = ep.bEndpointAddress;
          } else {
            ep_out = ep.bEndpointAddress;
          }
        }
      }

      if (ep_in != 0 && ep_out != 0) {
        found_endpoints = true;
      }
    }
  }
  libusb_free_config_descriptor(config);

  if (!found_endpoints) {
    LOGE("Could not find required bulk IN/OUT endpoints.");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // Fill output details
  out_details.vid = target_desc.idVendor;
  out_details.pid = target_desc.idProduct;
  out_details.interface_num = interface_num;
  out_details.ep_bulk_in = ep_in;
  out_details.ep_bulk_out = ep_out;

  // Detach kernel driver if active
  if (libusb_kernel_driver_active(handle_holder.get(), interface_num) == 1) {
    LOGD("Kernel driver active, detaching...");
    if (libusb_detach_kernel_driver(handle_holder.get(), interface_num) !=
        LIBUSB_SUCCESS) {
      LOGE("Failed to detach kernel driver.");
      return std::unexpected(FtError::kResultFail);
    }
  }

  // Reset device
  r = libusb_reset_device(handle_holder.get());
  if (r < 0) {
    LOGE("failed to reset the device");
    return std::unexpected(FtError::kResultFail);
  }

  // Wait for device to re-enumerate after reset
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  LOGD("reset mcu success");

  // Claim interface
  r = libusb_claim_interface(handle_holder.get(), interface_num);
  if (r < 0) {
    LOGE("Failed to claim interface: {}", libusb_error_name(r));
    return std::unexpected(FtError::kResultFail);
  }

  LOGD("Device connected and interface claimed successfully.");

  // Create UsbDevice object, transferring ownership of handle
  return UsbDevice(handle_holder.release(), ep_in, ep_out, ctx.get());
}

std::expected<int, FtError> UsbDevice::Send(std::span<const uint8_t> buf,
                                            uint32_t timeout_ms) const {
  if (!handle_) {
    return std::unexpected(FtError::kNoSuchDevice);
  }

  LOGV("timeout_ms={}", timeout_ms);

  int actual_len = 0;
  int r = libusb_bulk_transfer(
      handle_.get(), endpoint_out_, const_cast<uint8_t*>(buf.data()),
      static_cast<int>(buf.size()), &actual_len, timeout_ms);

  if (r != LIBUSB_SUCCESS) {

    libusb_reset_device(handle_.get());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (r == LIBUSB_ERROR_NO_DEVICE) {
      return std::unexpected(FtError::kNoSuchDevice);
    }

    return std::unexpected(FtError::kResultFail);
  }
  return actual_len;
}

std::expected<int, FtError> UsbDevice::Recv(std::span<uint8_t> buf,
                                            uint32_t timeout_ms) const {
  if (!handle_) {
    return std::unexpected(FtError::kNoSuchDevice);
  }

  LOGV("timeout_ms={}", timeout_ms);

  int actual_len = 0;
  int r = libusb_bulk_transfer(handle_.get(), endpoint_in_, buf.data(),
                               static_cast<int>(buf.size()), &actual_len,
                               timeout_ms);
  if (r != LIBUSB_SUCCESS) {

    libusb_reset_device(handle_.get());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (r == LIBUSB_ERROR_NO_DEVICE) {
      return std::unexpected(FtError::kNoSuchDevice);
    }

    return std::unexpected(FtError::kResultFail);
  }

  return std::min(actual_len, static_cast<int>(buf.size()));
}

int UsbDevice::ResetComplete(int timeout_sec) const {
  if (!handle_) {
    LOGE("Invalid device handle");
    return -1;
  }

  const int poll_ms = 100;
  const int timeout_ms = timeout_sec * 1000;
  int elapsed_ms = 0;

  while (timeout_sec == 0 || elapsed_ms < timeout_ms) {
    int config = 0;
    // Attempt a harmless control transfer to the device
    int r = libusb_get_configuration(handle_.get(), &config);

    // If the OS dropped the device due to the MCU reset, the handle
    // becomes stale and libusb throws this specific error.
    if (r == LIBUSB_ERROR_NO_DEVICE) {
      LOGI("USB device removal/reset detected");
      return 0;  // Success
    } else {
      LOGV("r={}", r);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    elapsed_ms += poll_ms;
  }

  LOGW("Timeout after {} seconds waiting for device removal", timeout_sec);
  return -1;
}
||||||| BASE      (15158824c8a1973786840dac479eeb84a7cc33a8 ec: fix missing includes)
=======
#include "usb_context.h"

#include <chrono>
#include <thread>
#include <vector>

#include "ft_log.h"

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

// Definition of global device removal flag
std::atomic<int> UsbDevice::g_device_removed_flag = 0;

// Hotplug callback for USB device removal events (uses std::atomic<int>)
int LIBUSB_CALL UsbRemoveEventCallback(libusb_context* /*ctx*/,
                                       libusb_device* dev,
                                       libusb_hotplug_event event,
                                       void* user_data) {
  libusb_device_descriptor desc{};
  libusb_get_device_descriptor(dev, &desc);

  if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
    auto* flag = static_cast<std::atomic<int>*>(user_data);
    if (flag) {
      flag->store(kFtUsbDeviceRemoveMagic);
    }
    LOGW("USB device removed: {:04x}:{:04x}", desc.idVendor, desc.idProduct);
  }
  return 0;
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
  libusb_device* target_dev = nullptr;
  libusb_device_descriptor target_desc{};

  // Find target Focal device
  for (ssize_t i = 0; i < count; ++i) {
    libusb_device* dev = dev_list.get()[i];
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(dev, &desc) < 0) {
      continue;
    }

    if (desc.idVendor == kFocalBootVid || desc.idVendor == kFocalRomVid ||
        desc.idVendor == kFocalEcVid) {
      if (desc.idVendor == kFocalEcVid && (desc.idProduct != kFocalEcPidTest &&
                                           desc.idProduct != kFocalEcPid9865 &&
                                           desc.idProduct != kFocalEcPid9869)) {
        LOGI(
            "Found Google device {:04x}:{:04x}, but not Focal fingerprint. "
            "Skipping.",
            desc.idVendor, desc.idProduct);
        continue;
      }

      LOGI("Found device VID {:04x}: PID {:04x}", desc.idVendor,
           desc.idProduct);
      target_dev = dev;
      target_desc = desc;
      break;
    }
  }

  if (target_dev == nullptr) {
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // Open device
  libusb_device_handle* handle = nullptr;
  int r = libusb_open(target_dev, &handle);
  if (r != LIBUSB_SUCCESS || handle == nullptr) {
    LOGE("Failed to open device: {}", libusb_error_name(r));
    return std::unexpected(FtError::kNoSuchDevice);
  }

  std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter>
      handle_holder(handle);

  // Get configuration descriptor
  libusb_config_descriptor* config = nullptr;
  r = libusb_get_config_descriptor(target_dev, 0, &config);
  if (r != LIBUSB_SUCCESS) {
    LOGE("Couldn't retrieve configuration descriptors.");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // Find bulk IN/OUT endpoints
  bool found_endpoints = false;
  uint8_t ep_in = 0, ep_out = 0;
  uint8_t interface_num = 0;

  for (int j = 0; j < config->bNumInterfaces && !found_endpoints; ++j) {
    const libusb_interface& interface = config->interface[j];
    for (int k = 0; k < interface.num_altsetting && !found_endpoints; ++k) {
      const libusb_interface_descriptor& alt = interface.altsetting[k];
      interface_num = alt.bInterfaceNumber;

      for (int l = 0; l < alt.bNumEndpoints; ++l) {
        const libusb_endpoint_descriptor& ep = alt.endpoint[l];
        if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
            LIBUSB_TRANSFER_TYPE_BULK) {
          if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
              LIBUSB_ENDPOINT_IN) {
            ep_in = ep.bEndpointAddress;
          } else {
            ep_out = ep.bEndpointAddress;
          }
        }
      }

      if (ep_in != 0 && ep_out != 0) {
        found_endpoints = true;
      }
    }
  }
  libusb_free_config_descriptor(config);

  if (!found_endpoints) {
    LOGE("Could not find required bulk IN/OUT endpoints.");
    return std::unexpected(FtError::kNoSuchDevice);
  }

  // Fill output details
  out_details.vid = target_desc.idVendor;
  out_details.pid = target_desc.idProduct;
  out_details.interface_num = interface_num;
  out_details.ep_bulk_in = ep_in;
  out_details.ep_bulk_out = ep_out;

  // Short delay for device enumeration
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Detach kernel driver if active
  if (libusb_kernel_driver_active(handle_holder.get(), interface_num) == 1) {
    LOGD("Kernel driver active, detaching...");
    if (libusb_detach_kernel_driver(handle_holder.get(), interface_num) !=
        LIBUSB_SUCCESS) {
      LOGE("Failed to detach kernel driver.");
      return std::unexpected(FtError::kResultFail);
    }
  }

  // Reset device
  r = libusb_reset_device(handle_holder.get());
  if (r < 0) {
    LOGE("failed to reset the device");
    return std::unexpected(FtError::kResultFail);
  }

  // Wait for device to re-enumerate after reset
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Register hotplug callback for device removal events
  if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
    libusb_hotplug_callback_handle hotplug_handle;
    int ret = libusb_hotplug_register_callback(
        ctx.get(),                                   // correct context
        LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,            // listen only for removal
        0,                                           // flags
        out_details.vid,                             // vendor ID
        out_details.pid,                             // product ID
        LIBUSB_HOTPLUG_MATCH_ANY,                    // device class (any)
        UsbRemoveEventCallback,                      // callback function
        static_cast<void*>(&g_device_removed_flag),  // user data
        &hotplug_handle);  // output handle (not saved)
    if (ret != LIBUSB_SUCCESS) {
      LOGW("Failed to register hotplug callback: {}", libusb_error_name(ret));
    } else {
      LOGD("Hotplug callback registered for VID:PID {:04x}:{:04x}",
           out_details.vid, out_details.pid);
    }
  } else {
    LOGW("Hotplug capability not available");
  }

  LOGD("reset mcu success");

  // Claim interface
  r = libusb_claim_interface(handle_holder.get(), interface_num);
  if (r < 0) {
    LOGE("Failed to claim interface: {}", libusb_error_name(r));
    return std::unexpected(FtError::kResultFail);
  }

  LOGD("Device connected and interface claimed successfully.");

  // Create UsbDevice object, transferring ownership of handle
  return UsbDevice(handle_holder.release(), ep_in, ep_out, ctx.get());
}

std::expected<int, FtError> UsbDevice::Send(std::span<const uint8_t> buf,
                                            uint32_t timeout_ms) const {
  if (!handle_) {
    return std::unexpected(FtError::kNoSuchDevice);
  }

  int actual_len = 0;
  int r = libusb_bulk_transfer(
      handle_.get(), endpoint_out_, const_cast<uint8_t*>(buf.data()),
      static_cast<int>(buf.size()), &actual_len, timeout_ms);
  if (r != LIBUSB_SUCCESS) {
    libusb_reset_device(handle_.get());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return std::unexpected(FtError::kResultFail);
  }
  return actual_len;
}

std::expected<int, FtError> UsbDevice::Recv(std::span<uint8_t> buf,
                                            uint32_t timeout_ms) const {
  if (!handle_) {
    return std::unexpected(FtError::kNoSuchDevice);
  }

  int actual_len = 0;
  int r = libusb_bulk_transfer(handle_.get(), endpoint_in_, buf.data(),
                               static_cast<int>(buf.size()), &actual_len,
                               timeout_ms);
  if (r != LIBUSB_SUCCESS) {
    libusb_reset_device(handle_.get());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return std::unexpected(FtError::kResultFail);
  }
  return std::min(actual_len, static_cast<int>(buf.size()));
}

// Renamed from ResetComplate
int UsbDevice::ResetComplete(int timeout_sec) const {
  if (!handle_) {
    LOGE("Invalid device handle");
    return -1;
  }

  libusb_context* ctx = ctx_;
  if (!ctx) {
    LOGE("Failed to obtain libusb context");
    return -1;
  }

  const int poll_ms = 100;  // poll every 100 ms
  const int timeout_ms = timeout_sec * 1000;
  int elapsed_ms = 0;

  while (timeout_sec == 0 || elapsed_ms < timeout_ms) {
    // Process USB events (triggers the hotplug callback)
    struct timeval tv = {0, poll_ms * 1000};  // microseconds
    int ret = libusb_handle_events_timeout(ctx, &tv);
    if (ret < 0 && ret != LIBUSB_ERROR_INTERRUPTED) {
      LOGW("libusb_handle_events_timeout failed: {}", libusb_error_name(ret));
    }

    // Check the global atomic flag
    if (g_device_removed_flag.load() == kFtUsbDeviceRemoveMagic) {
      LOGI("USB device removal detected");
      g_device_removed_flag.store(0);  // reset for next use
      return 0;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    elapsed_ms += poll_ms;
  }

  LOGW("Timeout after {} seconds waiting for device removal", timeout_sec);
  return -1;
}
>>>>>>> BASE      (aea89f2ea6978aa9a7fe54dd61b110e76f30dea8 util: focaltech: Add focaltech IAP tool)
