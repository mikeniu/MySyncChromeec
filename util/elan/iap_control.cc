// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iap_control.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <format>

namespace elan {
using namespace std::chrono_literals;

IapControl::IapControl(std::unique_ptr<UsbBackend> usb_backend)
    : usb_(std::move(usb_backend)) {}

std::expected<void, IapError> IapControl::Initialize(UsbDeviceId device_id) {
  if (int err = usb_->Initialize(); err != 0) {
    return std::unexpected(IapError::UsbInitFailed);
  }
  // Pass the struct directly down to the backend
  if (int err = usb_->OpenDevice(device_id); err != 0) {
    return std::unexpected(IapError::DeviceNotFound);
  }
  return {};
}

std::expected<void, IapError> IapControl::RunIapProcess(
    std::span<const uint8_t> rom) {
  if (auto res = AbortProcess(); !res) return std::unexpected(res.error());

  auto fw_ver_info = GetFwVersion();
  if (!fw_ver_info) return std::unexpected(fw_ver_info.error());

  // the Google fw prefix is defined in bits 8-15 (not bits 24-31).
  uint8_t fw_ver_google =
      static_cast<uint8_t>((fw_ver_info->fw_ver >> kFwVerPrefixShift) & 0xFF);
  if (fw_ver_google != kGoogleFwVerPrefix) {
    return std::unexpected(IapError::InvalidFirmware);
  }

  auto checksum = GetChecksum(rom);
  if (!checksum) return std::unexpected(checksum.error());

  if (auto res = RunIap(IapMode::Normal); !res)
    return std::unexpected(res.error());

  LogInfo("[IAP] {} - write rom data\n", __func__);
  if (auto res = SetRomAddress(kBaseRomAddress); !res)
    return std::unexpected(res.error());

  if (auto res = WriteRom(rom.size()); !res)
    return std::unexpected(res.error());

  if (auto res = WriteRomData(rom); !res) return std::unexpected(res.error());

  if (auto res = WriteRomFinish(*checksum); !res)
    return std::unexpected(res.error());

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

  if (!std::ranges::equal(rom, rom_read))
    return std::unexpected(IapError::DataMismatch);

  if (auto res = FinishIap(*checksum); !res)
    return std::unexpected(res.error());

  LogInfo("[IAP] {} - iap success\n", __func__);

  if (auto res = SoftwareReset(); !res) return std::unexpected(res.error());

  LogInfo("[IAP] {} - sw reset\n", __func__);

  return {};
}

void IapControl::SetProgressCallback(
    std::function<void(size_t, size_t)> callback) {
  progress_callback_ = callback;
}

std::expected<void, IapError> IapControl::RunIap(IapMode iap_mode) {
  IapCommandPacket packet = {
      .cmd_code = Command::RunIap,
      .reserved = 0,
      .parameter_2 = {0},
      .parameter_1 = ToLittleEndian(static_cast<uint32_t>(iap_mode))};
  return IapCommand(packet);
}

std::expected<void, IapError> IapControl::FinishIap(uint32_t checksum) {
  return SendCmdWithU32Payload(Command::FinishedIap, checksum);
}

std::expected<void, IapError> IapControl::SoftwareReset() {
  return SendCmdOnly(Command::SoftwareReset);
}

std::expected<FwVerInfo, IapError> IapControl::GetFwVersion() {
  IapCommandPacket packet = {.cmd_code = Command::GetFwVersion,
                             .reserved = 0,
                             .parameter_2 = {0},
                             .parameter_1 = 0};
  IapStatusPacket stat_recv = {};

  if (auto res = IapSendCommand(packet.AsByteSpan()); !res) {
    return std::unexpected(res.error());
  }

  if (auto res = IapRecvStatus(stat_recv.AsByteSpan()); !res) {
    return std::unexpected(res.error());
  }

  if (stat_recv.cmd_echo != packet.cmd_code ||
      stat_recv.status_code != Status::Success) {
    return std::unexpected(IapError::CommandFailed);
  }

  uint32_t fw_ver = FromLittleEndian(stat_recv.parameter_1);

  LogInfo("[IAP] {} - fw version: 0x{:08X}, boot type: 0x{:02X}\n", __func__,
          fw_ver, stat_recv.parameter_2[0]);

  return FwVerInfo{.fw_ver = fw_ver, .boot_type = stat_recv.parameter_2[0]};
}

std::expected<void, IapError> IapControl::AbortProcess() {
  return SendCmdOnly(Command::AbortProcess);
}

std::expected<void, IapError> IapControl::WriteRom(uint32_t length) {
  return SendCmdWithU32Payload(Command::WriteRom, length);
}

std::expected<void, IapError> IapControl::WriteRomFinish(uint32_t checksum) {
  return SendCmdWithU32Payload(Command::WriteRomFinish, checksum);
}

std::expected<void, IapError> IapControl::SetRomAddress(uint32_t address) {
  return SendCmdWithU32Payload(Command::SetRomAddress, address);
}

std::expected<void, IapError> IapControl::ReadRom(uint32_t length) {
  return SendCmdWithU32Payload(Command::ReadRom, length);
}

std::expected<void, IapError> IapControl::ReadRomFinish(uint32_t checksum) {
  return SendCmdWithU32Payload(Command::ReadRomFinish, checksum);
}

std::expected<void, IapError> IapControl::WriteRomData(
    std::span<const uint8_t> buff) {
  if (buff.empty()) return std::unexpected(IapError::BufferEmpty);

  const size_t total_size = buff.size();
  auto remaining = buff;

  while (!remaining.empty()) {
    size_t chunk_size = std::min(remaining.size(), kEpDataLength);

    auto chunk = remaining.first(chunk_size);
    remaining = remaining.subspan(chunk_size);

    if (auto res = IapSendData(chunk); !res) {
      LogErr("[IAP] {} - fail\n", __func__);
      return std::unexpected(res.error());
    }

    if (progress_callback_) {
      progress_callback_(total_size - remaining.size(), total_size);
    }
  }
  return {};
}

std::expected<void, IapError> IapControl::ReadRomData(std::span<uint8_t> buff) {
  if (buff.empty()) return std::unexpected(IapError::BufferEmpty);

  const size_t total_size = buff.size();
  auto remaining = buff;

  while (!remaining.empty()) {
    size_t chunk_size = std::min(remaining.size(), kEpDataLength);

    auto chunk = remaining.first(chunk_size);
    remaining = remaining.subspan(chunk_size);

    if (auto res = IapRecvData(chunk); !res) {
      LogErr("[IAP] {} - fail\n", __func__);
      return std::unexpected(res.error());
    }

    if (progress_callback_) {
      progress_callback_(total_size - remaining.size(), total_size);
    }
  }
  return {};
}

std::expected<void, IapError> IapControl::SendCmdOnly(Command cmd_code) {
  IapCommandPacket packet = {.cmd_code = cmd_code,
                             .reserved = 0,
                             .parameter_2 = {0},
                             .parameter_1 = 0};

  return IapCommand(packet);
}

std::expected<void, IapError> IapControl::SendCmdWithU32Payload(
    Command cmd_code, uint32_t payload_val) {
  IapCommandPacket packet = {.cmd_code = cmd_code,
                             .reserved = 0,
                             .parameter_2 = {0},
                             .parameter_1 = ToLittleEndian(payload_val)};

  return IapCommand(packet);
}

std::expected<void, IapError> IapControl::IapCommand(
    const IapCommandPacket& packet) {
  IapStatusPacket stat_recv = {};

  if (auto res = IapSendCommand(packet.AsByteSpan()); !res) {
    return std::unexpected(res.error());
  }

  if (auto res = IapRecvStatus(stat_recv.AsByteSpan()); !res) {
    return std::unexpected(res.error());
  }

  if (stat_recv.cmd_echo != packet.cmd_code ||
      stat_recv.status_code != Status::Success) {
    LogErr("[IAP] {} - fail, cmd id: 0x{:02X}, error code: 0x{:02X}...\n",
           __func__, std::to_underlying(packet.cmd_code),
           std::to_underlying(stat_recv.status_code));

    return std::unexpected(IapError::CommandFailed);
  }

  return {};
}

std::expected<void, IapError> IapControl::IapSendCommand(
    std::span<const uint8_t> buff) {
  return DoUsbTransfer(kEpCmdOut, buff);
}

std::expected<void, IapError> IapControl::IapRecvStatus(
    std::span<uint8_t> buff) {
  return DoUsbTransfer(kEpStatusIn, buff);
}

std::expected<void, IapError> IapControl::IapSendData(
    std::span<const uint8_t> buff) {
  return DoUsbTransfer(kEpDataOut, buff);
}

std::expected<void, IapError> IapControl::IapRecvData(std::span<uint8_t> buff) {
  return DoUsbTransfer(kEpDataIn, buff);
}

std::expected<void, IapError> IapControl::DoUsbTransfer(
    uint8_t endpoint, std::span<uint8_t> buff,
    const std::source_location location) {
  if (buff.empty()) {
    return std::unexpected(IapError::BufferEmpty);
  }

  if (int err = usb_->InterruptTransfer(endpoint, buff, kUsbTimeout);
      err != 0) {
    LogErr("[IAP] {} - transfer fail, code: {}\n", location.function_name(),
           err);
    return std::unexpected(IapError::TransferFailed);
  }
  return {};
}

std::expected<void, IapError> IapControl::DoUsbTransfer(
    uint8_t endpoint, std::span<const uint8_t> buff,
    const std::source_location location) {
  // Safely cast away const for OUT transfers. The libusb C API lacks the
  // const qualifier, but it does not modify
  // the host buffer during an OUT transfer.
  return DoUsbTransfer(
      endpoint,
      std::span<uint8_t>(const_cast<uint8_t*>(buff.data()), buff.size()),
      location);
}

std::expected<uint32_t, IapError> IapControl::GetChecksum(
    std::span<const uint8_t> buff) {
  // Use the specific alignment error instead of BufferTooSmall
  if ((buff.size() % 4) != 0)
    return std::unexpected(IapError::InvalidAlignment);

  uint32_t checksum = 0;
  auto remaining = buff;

  while (!remaining.empty()) {
    uint32_t chunk_val = 0;
    // Extract exactly 4 bytes locally
    std::memcpy(&chunk_val, remaining.data(), sizeof(chunk_val));
    checksum += FromLittleEndian(chunk_val);

    // Advance the window
    remaining = remaining.subspan(4);
  }

  return checksum;
}
}  // namespace elan
