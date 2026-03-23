// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iap_control.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>

namespace elan {
namespace {
// ---------------------------------------------------------
// Status Codes
// ---------------------------------------------------------
enum IapStatus : uint8_t { kErrStaReady = 0x00, kErrStaSuccess = 0x01 };

// ---------------------------------------------------------
// Internal Constants
// ---------------------------------------------------------
constexpr size_t kCountInterface = 4;
constexpr int kTimeoutTransferMs = 1000;
constexpr uint8_t kGoogleFwVerPrefix = 0x10;
constexpr uint32_t kBaseRomAddress = 0x0000;

// ---------------------------------------------------------
// Endpoints & Lengths
// ---------------------------------------------------------
constexpr uint8_t kEp1Addr = 0x01;
constexpr uint8_t kEp2Addr = 0x82;
constexpr uint8_t kEp3Addr = 0x03;
constexpr uint8_t kEp4Addr = 0x84;

constexpr size_t kEp1Length = 8;
constexpr size_t kEp2Length = 8;
constexpr size_t kEp3Length = 64;
constexpr size_t kEp4Length = 64;

// ---------------------------------------------------------
// Command Codes
// ---------------------------------------------------------
constexpr uint8_t kCmdRunIap = 0x20;
constexpr uint8_t kCmdFinishedIap = 0x21;
constexpr uint8_t kCmdSoftwareReset = 0x22;
constexpr uint8_t kCmdGetFwVersion = 0x41;
constexpr uint8_t kCmdAbortProcess = 0x48;
constexpr uint8_t kCmdWriteRom = 0xA0;
constexpr uint8_t kCmdWriteRomFinish = 0xA1;
constexpr uint8_t kCmdSetRomAddress = 0xA4;
constexpr uint8_t kCmdReadRom = 0xE0;
constexpr uint8_t kCmdReadRomFinish = 0xE1;
}  // namespace

IapControl::IapControl() { libusb_initialized_ = false; }

IapControl::~IapControl() {
  ReleaseResource();

  if (libusb_initialized_) {
    libusb_initialized_ = false;
    libusb_exit(nullptr);
  }
}

std::expected<void, IapError> IapControl::Initialize(uint16_t vid,
                                                     uint16_t pid) {
  ReleaseResource();

  if (!libusb_initialized_) {
    int err_code = libusb_init(nullptr);
    if (err_code != 0) {
      LogErr("[IAP] {} - libusb_init failed with code: {} ({})\n", __func__,
             err_code, libusb_error_name(err_code));
      return std::unexpected(IapError::UsbInitFailed);
    }
    libusb_initialized_ = true;
  }

  if (auto res = OpenDevice(vid, pid); !res)
    return std::unexpected(res.error());

  return {};
}

std::expected<void, IapError> IapControl::RunIapProcess(
    std::span<const uint8_t> rom) {
  if (auto res = AbortProcess(); !res) return std::unexpected(res.error());

  auto fw_ver_info = GetFwVersion();
  if (!fw_ver_info) return std::unexpected(fw_ver_info.error());

  // the Google fw prefix is defined in bits 8-15 (not bits 24-31).
  uint8_t fw_ver_google = fw_ver_info->fw_ver >> 8;
  if (fw_ver_google != kGoogleFwVerPrefix) {
    return std::unexpected(IapError::InvalidFirmware);
  }

  auto checksum = GetChecksum(rom);
  if (!checksum) return std::unexpected(checksum.error());

  if (auto res = RunIap(0); !res) return std::unexpected(res.error());

  // write rom
  LogInfo("[IAP] {} - write rom data\n", __func__);
  if (auto res = SetRomAddress(kBaseRomAddress); !res)
    return std::unexpected(res.error());

  if (auto res = WriteRom(rom.size()); !res)
    return std::unexpected(res.error());

  if (auto res = WriteRomData(rom); !res) return std::unexpected(res.error());

  if (auto res = WriteRomFinish(*checksum); !res)
    return std::unexpected(res.error());

  // read rom
  LogInfo("[IAP] {} - verify rom data\n", __func__);
  if (auto res = SetRomAddress(kBaseRomAddress); !res)
    return std::unexpected(res.error());

  if (auto res = ReadRom(rom.size()); !res) return std::unexpected(res.error());

  std::vector<uint8_t> rom_read(rom.size());
  if (auto res = ReadRomData(rom_read); !res)
    return std::unexpected(res.error());

  auto checksum_read = GetChecksum(rom_read);
  if (!checksum_read) return std::unexpected(checksum_read.error());

  if (auto res = ReadRomFinish(*checksum_read); !res)
    return std::unexpected(res.error());

  // compare data
  if (!std::ranges::equal(rom, rom_read))
    return std::unexpected(IapError::DataMismatch);

  // finish iap
  if (auto res = FinishIap(*checksum); !res)
    return std::unexpected(res.error());

  LogInfo("[IAP] {} - iap success\n", __func__);

  if (auto res = SoftwareReset(); !res) return std::unexpected(res.error());

  LogInfo("[IAP] {} - sw reset\n", __func__);

  return {};
}

std::expected<void, IapError> IapControl::SetProgressCallback(
    std::function<void(size_t, size_t)> callback) {
  progress_callback_ = callback;

  return {};
}

std::expected<void, IapError> IapControl::RunIap(uint8_t iap_mode) {
  uint8_t data_send[kEp1Length] = {};
  data_send[0] = kCmdRunIap;
  data_send[4] = iap_mode;
  return IapCommand(data_send);
}

std::expected<void, IapError> IapControl::FinishIap(uint32_t checksum) {
  return SendCmdWithU32Payload(kCmdFinishedIap, checksum);
}

std::expected<void, IapError> IapControl::SoftwareReset() {
  return SendCmdOnly(kCmdSoftwareReset);
}

std::expected<FwVerInfo, IapError> IapControl::GetFwVersion() {
  uint8_t data_send[kEp1Length] = {kCmdGetFwVersion};
  uint8_t stat_recv[kEp2Length] = {};

  if (auto res = IapSendCommand(data_send); !res) {
    return std::unexpected(res.error());
  }

  if (auto res = IapRecvStatus(stat_recv); !res) {
    return std::unexpected(res.error());
  }

  if (stat_recv[0] != data_send[0] || stat_recv[1] != kErrStaSuccess) {
    return std::unexpected(IapError::CommandFailed);
  }

  auto fw_ver = Get32Le(std::span<const uint8_t>(stat_recv + 4, 4));
  if (!fw_ver) {
    return std::unexpected(fw_ver.error());
  }

  LogInfo("[IAP] {} - fw version: 0x{:08X}, boot type: 0x{:02X}\n", __func__,
          *fw_ver, stat_recv[2]);

  return FwVerInfo{.fw_ver = *fw_ver, .boot_type = stat_recv[2]};
}

std::expected<void, IapError> IapControl::AbortProcess() {
  return SendCmdOnly(kCmdAbortProcess);
}

std::expected<void, IapError> IapControl::WriteRom(uint32_t length) {
  return SendCmdWithU32Payload(kCmdWriteRom, length);
}

std::expected<void, IapError> IapControl::WriteRomFinish(uint32_t checksum) {
  return SendCmdWithU32Payload(kCmdWriteRomFinish, checksum);
}

std::expected<void, IapError> IapControl::SetRomAddress(uint32_t address) {
  return SendCmdWithU32Payload(kCmdSetRomAddress, address);
}

std::expected<void, IapError> IapControl::ReadRom(uint32_t length) {
  return SendCmdWithU32Payload(kCmdReadRom, length);
}

std::expected<void, IapError> IapControl::ReadRomFinish(uint32_t checksum) {
  return SendCmdWithU32Payload(kCmdReadRomFinish, checksum);
}

std::expected<void, IapError> IapControl::WriteRomData(
    std::span<const uint8_t> buff) {
  if (buff.empty()) return std::unexpected(IapError::BufferEmpty);

  for (size_t offset = 0; offset < buff.size(); offset += kEp3Length) {
    size_t chunk_size = std::min(kEp3Length, buff.size() - offset);
    auto chunk = buff.subspan(offset, chunk_size);

    if (auto res = IapSendData(chunk); !res) {
      LogErr("[IAP] {} - fail, count: {}\n", __func__, offset);
      return std::unexpected(res.error());
    }

    if (progress_callback_) {
      progress_callback_(offset + chunk.size(), buff.size());
    }
  }
  return {};
}

std::expected<void, IapError> IapControl::ReadRomData(std::span<uint8_t> buff) {
  if (buff.empty()) return std::unexpected(IapError::BufferEmpty);

  for (size_t offset = 0; offset < buff.size(); offset += kEp4Length) {
    size_t chunk_size = std::min(kEp4Length, buff.size() - offset);
    auto chunk = buff.subspan(offset, chunk_size);

    if (auto res = IapRecvData(chunk); !res) {
      LogErr("[IAP] {} - fail, count: {}\n", __func__, offset);
      return std::unexpected(res.error());
    }

    if (progress_callback_) {
      progress_callback_(offset + chunk.size(), buff.size());
    }
  }
  return {};
}

std::expected<void, IapError> IapControl::OpenDevice(uint16_t vid,
                                                     uint16_t pid) {
  libusb_device **raw_list = nullptr;
  ssize_t usb_dev_count = libusb_get_device_list(nullptr, &raw_list);
  LibusbDeviceListPtr list(raw_list);
  int err_code = 0;

  if (usb_dev_count < 0) return std::unexpected(IapError::DeviceListNotFound);

  // 1. Find the target device
  libusb_device *found_dev = nullptr;
  for (ssize_t i = 0; i < usb_dev_count; i++) {
    struct libusb_device_descriptor desc = {0};
    if (libusb_get_device_descriptor(list.get()[i], &desc) == 0) {
      if (desc.idVendor == vid && desc.idProduct == pid) {
        found_dev = list.get()[i];
        break;
      }
    }
  }

  if (!found_dev) return std::unexpected(IapError::DeviceNotFound);

  // 2. Read configuration and validate HID interface
  libusb_config_descriptor *config = nullptr;
  err_code = libusb_get_config_descriptor(found_dev, 0, &config);
  if (err_code != 0) {
    LogErr("[IAP] {} - get config descriptor failed, error code: {} ({})\n",
           __func__, err_code, libusb_error_name(err_code));
    return std::unexpected(IapError::GetConfigDescriptorFailed);
  }

  hid_interfaces_.clear();

  for (int i = 0; i < config->bNumInterfaces; i++) {
    const struct libusb_interface *inter = &config->interface[i];
    for (int j = 0; j < inter->num_altsetting; j++) {
      const struct libusb_interface_descriptor *alt = &inter->altsetting[j];

      if (alt->bInterfaceClass != LIBUSB_CLASS_HID) {
        continue;
      }

      hid_interfaces_.push_back(alt->bInterfaceNumber);

      break;  // Found the HID interface, stop searching
    }
  }

  libusb_free_config_descriptor(config);

  if (hid_interfaces_.empty())
    return std::unexpected(IapError::HidInterfaceNotFound);

  if (hid_interfaces_.size() != kCountInterface) {
    LogErr("[IAP] {} - Expected {} HID interface, but found {}\n", __func__,
           kCountInterface, hid_interfaces_.size());
    return std::unexpected(IapError::HidInterfaceCountMismatch);
  }

  // 3. Claim Device and Detach Kernel Drivers
  libusb_device_handle *handle = nullptr;
  err_code = libusb_open(found_dev, &handle);
  if (err_code != 0) {
    LogErr("[IAP] {} - open device failed, error code: {} ({})\n", __func__,
           err_code, libusb_error_name(err_code));
    return std::unexpected(IapError::DeviceOpenFailed);
  }

  dev_handle_ptr_.reset(handle);

  for (size_t i = 0; i < hid_interfaces_.size(); i++) {
    int active =
        libusb_kernel_driver_active(dev_handle_ptr_.get(), hid_interfaces_[i]);
    if (active == 1) {
      libusb_detach_kernel_driver(dev_handle_ptr_.get(), hid_interfaces_[i]);

      hid_detach_interfaces_.push_back(hid_interfaces_[i]);
    } else if (active < 0) {
      LogErr("[IAP] {} - Failed to check kernel driver: {}\n", __func__,
             libusb_error_name(active));
    }

    err_code =
        libusb_claim_interface(dev_handle_ptr_.get(), hid_interfaces_[i]);
    if (err_code != 0) {
      LogErr("[IAP] {} - claim interface ({}) failed, error code: {} ({})\n",
             __func__, hid_interfaces_[i], err_code,
             libusb_error_name(err_code));
      ReleaseResource();
      return std::unexpected(IapError::ClaimInterfaceFailed);
    }
  }

  return {};
}

std::expected<void, IapError> IapControl::SendCmdOnly(uint8_t cmd_code) {
  uint8_t data_send[kEp1Length] = {cmd_code};
  return IapCommand(data_send);
}

std::expected<void, IapError> IapControl::SendCmdWithU32Payload(
    uint8_t cmd_code, uint32_t payload) {
  uint8_t data_send[kEp1Length] = {};
  data_send[0] = cmd_code;

  if (auto res = Set32Le(std::span<uint8_t>(data_send + 4, 4), payload); !res) {
    return res;
  }

  return IapCommand(data_send);
}

std::expected<void, IapError> IapControl::IapCommand(
    std::span<const uint8_t> buff) {
  uint8_t stat_recv[kEp2Length] = {};

  if (auto res = IapSendCommand(buff); !res) {
    return std::unexpected(res.error());
  }

  if (auto res = IapRecvStatus(stat_recv); !res) {
    return std::unexpected(res.error());
  }

  if (stat_recv[0] != buff[0] || stat_recv[1] != kErrStaSuccess) {
    auto debug_val = Get32Le(std::span<const uint8_t>(stat_recv + 4, 4));
    auto debug_str = debug_val ? std::format("0x{:08X}", *debug_val) : "N/A";

    LogErr(
        "[IAP] {} - fail, cmd id: 0x{:02X}, "
        "error code: 0x{:02X}, value: {}\n",
        __func__, buff[0], stat_recv[1], debug_str);
    return std::unexpected(IapError::CommandFailed);
  }

  return {};  // Success for expected<void, E>
}

std::expected<void, IapError> IapControl::IapSendCommand(
    std::span<const uint8_t> buff) {
  if (!dev_handle_ptr_) return std::unexpected(IapError::InvalidDeviceHandle);

  if (buff.empty()) return std::unexpected(IapError::BufferEmpty);

  return DoUsbTransfer(kEp1Addr, const_cast<uint8_t *>(buff.data()),
                       static_cast<int>(buff.size()));
}

std::expected<void, IapError> IapControl::IapRecvStatus(
    std::span<uint8_t> buff) {
  if (!dev_handle_ptr_) return std::unexpected(IapError::InvalidDeviceHandle);

  if (buff.empty()) return std::unexpected(IapError::BufferEmpty);

  return DoUsbTransfer(kEp2Addr, buff.data(), static_cast<int>(buff.size()));
}

std::expected<void, IapError> IapControl::IapSendData(
    std::span<const uint8_t> buff) {
  if (!dev_handle_ptr_) return std::unexpected(IapError::InvalidDeviceHandle);

  if (buff.empty()) return std::unexpected(IapError::BufferEmpty);

  return DoUsbTransfer(kEp3Addr, const_cast<uint8_t *>(buff.data()),
                       static_cast<int>(buff.size()));
}

std::expected<void, IapError> IapControl::IapRecvData(std::span<uint8_t> buff) {
  if (!dev_handle_ptr_) return std::unexpected(IapError::InvalidDeviceHandle);

  if (buff.empty()) return std::unexpected(IapError::BufferEmpty);

  return DoUsbTransfer(kEp4Addr, buff.data(), static_cast<int>(buff.size()));
}

std::expected<void, IapError> IapControl::DoUsbTransfer(
    uint8_t endpoint, uint8_t *data, int length,
    const std::source_location location) {
  int transferred = 0;
  int err_code =
      libusb_interrupt_transfer(dev_handle_ptr_.get(), endpoint, data, length,
                                &transferred, kTimeoutTransferMs);

  if (err_code != 0) {
    LogErr("[IAP] {} - fail, error code: {} ({})\n", location.function_name(),
           err_code, libusb_error_name(err_code));
    return std::unexpected(IapError::TransferFailed);
  }

  if (transferred != length) {
    LogErr("[IAP] {} - fail, data transferred {} is less than expected\n",
           location.function_name(), transferred);
    return std::unexpected(IapError::TransferSizeMismatch);
  }

  return {};
}

std::expected<void, IapError> IapControl::Set32Le(std::span<uint8_t> buff,
                                                  uint32_t value) {
  if (buff.size() < sizeof(value))
    return std::unexpected(IapError::BufferTooSmall);

  if constexpr (std::endian::native == std::endian::big) {
    value = std::byteswap(value);
  }

  std::memcpy(buff.data(), &value, sizeof(value));

  return {};
}

std::expected<uint32_t, IapError> IapControl::Get32Le(
    std::span<const uint8_t> buff) {
  uint32_t value;

  if (buff.size() < sizeof(value))
    return std::unexpected(IapError::BufferTooSmall);

  std::memcpy(&value, buff.data(), sizeof(value));

  if constexpr (std::endian::native == std::endian::big) {
    value = std::byteswap(value);
  }

  return value;
}

std::expected<uint32_t, IapError> IapControl::GetChecksum(
    std::span<const uint8_t> buff) {
  if ((buff.size() % 4) != 0) return std::unexpected(IapError::BufferTooSmall);

  uint32_t checksum = 0;
  for (size_t i = 0; i < buff.size(); i += 4) {
    if (auto val = Get32Le(buff.subspan(i, 4))) {
      checksum += *val;
    }
  }
  return checksum;
}

std::expected<void, IapError> IapControl::ReleaseResource() {
  if (dev_handle_ptr_ == nullptr) return {};

  if (hid_interfaces_.empty()) return {};

  std::expected<void, IapError> ret;
  int err_code = 0;

  for (size_t i = 0; i < hid_interfaces_.size(); i++) {
    err_code =
        libusb_release_interface(dev_handle_ptr_.get(), hid_interfaces_[i]);
    if (err_code != 0) {
      LogErr(
          "[IAP] {} - release interface ({}) failed, "
          "error code: {} ({})\n",
          __func__, hid_interfaces_[i], err_code, libusb_error_name(err_code));
      ret = std::unexpected(IapError::ReleaseInterfaceFailed);
    }
  }

  for (size_t i = 0; i < hid_detach_interfaces_.size(); i++) {
    err_code = libusb_attach_kernel_driver(dev_handle_ptr_.get(),
                                           hid_detach_interfaces_[i]);
    if (err_code != 0) {
      LogErr(
          "[IAP] {} - reattach kernel driver for interface ({}) failed, "
          "error code: {} ({})\n",
          __func__, hid_detach_interfaces_[i], err_code,
          libusb_error_name(err_code));
      ret = std::unexpected(IapError::AttachKernelDriverFailed);
    }
  }

  hid_detach_interfaces_.clear();

  dev_handle_ptr_.reset();

  return ret;
}
}  // namespace elan
