// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_UTILITY_H_
#define UTIL_ELAN_UTILITY_H_

#include <bit>
#include <concepts>
#include <cstdio>
#include <cstring>
#include <expected>
#include <print>
#include <span>
#include <string_view>
#include <utility>

namespace elan {

// Converts an integral type between native byte order and little-endian.
template <std::integral T>
constexpr T ToLittleEndian(T val) {
  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(val);
  }
  return val;
}

// Semantic alias for converting from little-endian to native byte order.
template <std::integral T>
constexpr T FromLittleEndian(T val) {
  return ToLittleEndian(val);
}

enum class IapError {
  None,
  FileOpenFailed,
  FileReadFailed,
  InvalidFileSize,
  UsbInitFailed,
  DeviceListNotFound,
  DeviceNotFound,
  DeviceOpenFailed,
  GetConfigDescriptorFailed,
  HidInterfaceNotFound,
  HidInterfaceCountMismatch,
  ClaimInterfaceFailed,
  ReleaseInterfaceFailed,
  AttachKernelDriverFailed,
  TransferFailed,
  TransferSizeMismatch,
  InvalidDeviceHandle,
  InvalidFirmware,
  ChecksumMismatch,
  DataMismatch,
  BufferTooSmall,
  BufferEmpty,
  InvalidAlignment,
  CommandFailed
};

constexpr std::string_view ToString(IapError err) {
  switch (err) {
    case IapError::FileOpenFailed:
      return "Failed to open file";
    case IapError::FileReadFailed:
      return "Failed to read file data";
    case IapError::InvalidFileSize:
      return "File size is 0 or invalid";
    case IapError::UsbInitFailed:
      return "libusb initialization failed";
    case IapError::DeviceListNotFound:
      return "Usb device list not found";
    case IapError::DeviceNotFound:
      return "Target USB device not found";
    case IapError::DeviceOpenFailed:
      return "Failed to open USB device";
    case IapError::GetConfigDescriptorFailed:
      return "Failed to read USB device config";
    case IapError::HidInterfaceNotFound:
      return "HID interface not found";
    case IapError::HidInterfaceCountMismatch:
      return "Unexpected number of HID interface";
    case IapError::ClaimInterfaceFailed:
      return "Failed to claim interface";
    case IapError::ReleaseInterfaceFailed:
      return "Failed to release interface";
    case IapError::AttachKernelDriverFailed:
      return "Failed to attach kernel driver";
    case IapError::TransferFailed:
      return "USB transfer failed or incomplete";
    case IapError::TransferSizeMismatch:
      return "Unexpected USB transfer size";
    case IapError::InvalidDeviceHandle:
      return "Invalid device handle";
    case IapError::InvalidFirmware:
      return "Firmware prefix does not match Google requirement";
    case IapError::ChecksumMismatch:
      return "Checksum verification failed";
    case IapError::DataMismatch:
      return "Data verification failed";
    case IapError::BufferTooSmall:
      return "Buffer too small for operation";
    case IapError::BufferEmpty:
      return "Buffer is empty for operation";
    case IapError::InvalidAlignment:
      return "Buffer size is not 4-byte aligned";
    case IapError::CommandFailed:
      return "Device rejected command";
    default:
      return "Unknown Error";
  }
}

}  // namespace elan

template <typename... Args>
void LogInfo(std::format_string<Args...> fmt, Args&&... args) {
  std::print(stdout, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void LogErr(std::format_string<Args...> fmt, Args&&... args) {
  std::print(stderr, fmt, std::forward<Args>(args)...);
}

#endif
