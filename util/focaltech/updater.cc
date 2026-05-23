
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <openssl/sha.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include "ft_log.h"
#include "ft_scsi.h"
#include "ft_util.h"

namespace {

using namespace std::chrono_literals;

// --- USB Transfer Timeouts ---
// Before bootloader v1.8, erasing the flash takes about 4.4 seconds.
constexpr auto kErasePageTimeout = 6000ms;
constexpr auto kDownloadPageTimeout = 1000ms;
constexpr auto kVerifyHashTimeout = 1000ms;
constexpr auto kMcuResetTimeout = 200ms;
constexpr auto kGenericInfoTimeout = 200ms;

// FocalTech vendor-specific SCSI command opcodes
constexpr uint8_t kFtCmdReadInfo = 0xDB;
constexpr uint8_t kFtCmdAction = 0xDC;

// Sub-commands for kFtCmdAction
constexpr uint8_t kFtSubcmdReadData = 0x01;
constexpr uint8_t kFtSubcmdWriteBin = 0x03;
constexpr uint8_t kFtSubcmdCodeBin = 0x04;
constexpr uint8_t kFtSubcmdErasePage = 0x08;
constexpr uint8_t kFtSubcmdVerify = 0xBB;
constexpr uint8_t kFtSubcmdSoftReset = 0xDD;
constexpr uint8_t kFtSubcmdFlashWpSet = 0xF7;
constexpr uint8_t kFtSubcmdFlashWpGet = 0xF8;

constexpr size_t kFtBootVersionMax = 36;
constexpr uint32_t kFtWriteSramAddr = 0x20002000;
constexpr size_t kFtEcBinSizeMax = 2 * 1024 * 1024;

constexpr uint32_t kFocalConfigSize = 0x1000;
constexpr uint32_t kFocalRomCfgAddr = 0x10000000;
constexpr uint32_t kFocalBootCfgAddr = 0x10080000;
constexpr uint32_t kFocalRomRunAddr = 0x10002000;
constexpr uint32_t kFocalBootRunAddr = 0x10081000;

constexpr uint32_t kCodeValidMagic = 0x3AEC3721;

constexpr uint8_t kSoftPor = 0;

struct FtConfigPage {
  uint32_t code_valid_control_word;
  uint8_t reserved[28];
  uint32_t code_start_address;
} __attribute__((packed));

std::expected<std::vector<uint8_t>, FtError> ReadFileToVector(
    std::string_view path) {
  std::ifstream file(std::string{path}, std::ios::binary | std::ios::ate);
  if (!file) {
    LOGE("Cannot open file: {}", path);
    return std::unexpected(FtError::kNoSuchFile);
  }

  auto size = file.tellg();
  if (size <= 0 || size > kFtEcBinSizeMax) {
    LOGE("Invalid file size: {}", static_cast<int64_t>(size));
    return std::unexpected(FtError::kParameterError);
  }

  std::vector<uint8_t> buffer(size);
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
    LOGE("Failed to read file payload.");
    return std::unexpected(FtError::kResultFail);
  }

  return buffer;
}

std::array<uint8_t, SHA256_DIGEST_LENGTH> CalculateSha256(
    std::span<const uint8_t> data) {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash{};
  SHA256(data.data(), data.size(), hash.data());
  return hash;
}

}  // namespace

// Helper to build a standard kFtCmdAction command and send it immediately
std::expected<void, FtError> SendAction(
    const UsbDevice& device, uint8_t subcmd, uint32_t address,
    std::span<uint8_t> data, TransferDirection dir,
    std::chrono::milliseconds timeout, uint32_t length = 0,
    uint32_t aux_address = 0, uint8_t flags = 0) {
  auto cmd =
      BuildFtCmd(kFtCmdAction, subcmd, address, length, aux_address, flags);
  return SendBulkCommand(device, cmd, data, dir, timeout);
}

static std::expected<void, FtError> FtErasePage(const UsbDevice& device,
                                                int length, int addr) {
  uint32_t pages =
      FtDivRoundUp(static_cast<uint32_t>(length), kFocalConfigSize);
  return SendAction(device, kFtSubcmdErasePage, addr, {},
                    TransferDirection::kOut, kErasePageTimeout, pages);
}

static std::expected<void, FtError> FtDownloadPage(
    const UsbDevice& device, uint32_t downaddr, int per_len,
    std::span<const uint8_t> data) {
  // Write data to SRAM (kFtSubcmdWriteBin)
  auto res =
      SendAction(device, kFtSubcmdWriteBin, kFtWriteSramAddr,
                 std::span<uint8_t>(const_cast<uint8_t*>(data.data()), per_len),
                 TransferDirection::kOut, kDownloadPageTimeout, per_len);
  if (!res) {
    LOGE("Write bin error");
    return res;
  }

  // Execute code (burn to flash) (kFtSubcmdCodeBin)
  res = SendAction(device, kFtSubcmdCodeBin, downaddr, {},
                   TransferDirection::kOut, kDownloadPageTimeout, per_len,
                   kFtWriteSramAddr);
  if (!res) LOGE("Code bin error");
  return res;
}

static std::expected<void, FtError> FtDownloadBin(const UsbDevice& device,
                                                  std::string_view file_path,
                                                  uint32_t target_addr) {
  auto file_data = ReadFileToVector(file_path);
  if (!file_data) return std::unexpected(file_data.error());

  const auto& buffer = *file_data;
  int file_len = buffer.size();
  int total_frame =
      FtDivRoundUp(static_cast<uint32_t>(file_len), kFocalConfigSize);

  // Erase config page area if needed
  if (target_addr != kFocalBootCfgAddr) {
    auto erase_res = FtErasePage(device, kFocalConfigSize, kFocalRomCfgAddr);
    if (!erase_res) return erase_res;
  }

  auto erase_res = FtErasePage(device, file_len, target_addr);
  if (!erase_res) return erase_res;

  int len = total_frame * kFocalConfigSize;
  std::vector<uint8_t> write_buffer(len, 0xFF);
  std::memcpy(write_buffer.data(), buffer.data(), file_len);

  uint8_t* p = write_buffer.data();
  uint32_t downaddr = target_addr;
  int per_len = kFocalConfigSize;
  int frame = 0;

  if (target_addr == kFocalBootCfgAddr || target_addr == kFocalRomCfgAddr) {
    LOGI("Write config page last");
    downaddr += per_len;
    p += per_len;
    len -= per_len;
  }

  while (len >= per_len) {
    auto page_res =
        FtDownloadPage(device, downaddr, per_len, {p, size_t(per_len)});
    if (!page_res) return page_res;

    len -= per_len;
    p += per_len;
    downaddr += per_len;
    LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  if (len > 0) {
    len = FtDivRoundUp(static_cast<uint32_t>(len), static_cast<uint32_t>(4));
    auto page_res = FtDownloadPage(device, downaddr, len, {p, size_t(len)});
    if (!page_res) return page_res;
  }

  if (target_addr == kFocalBootCfgAddr || target_addr == kFocalRomCfgAddr) {
    LOGI("Write config page");
    auto page_res = FtDownloadPage(device, target_addr, kFocalConfigSize,
                                   {write_buffer.data(), kFocalConfigSize});
    if (!page_res) return page_res;
    LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  return {};
}

std::expected<void, FtError> FtVerifyData(const UsbDevice& device,
                                          std::string_view file_path,
                                          uint32_t target_addr) {
  auto file_res = ReadFileToVector(file_path);
  if (!file_res.has_value()) {
    return std::unexpected(file_res.error());
  }
  const auto& firmware_data = file_res.value();

  // HASH256 mode
  uint8_t hash_mode = (target_addr == kFocalRomCfgAddr) ? (0x04 << 4) : 0x04;

  std::array<uint8_t, SHA256_DIGEST_LENGTH> device_hash{};

  auto io_res = SendAction(device, kFtSubcmdVerify, target_addr, device_hash,
                           TransferDirection::kIn, kVerifyHashTimeout,
                           firmware_data.size(), 0, hash_mode);

  if (!io_res.has_value()) {
    LOGE("Verification command failed over USB.");
    return std::unexpected(io_res.error());
  }

  auto local_hash = CalculateSha256(firmware_data);
  if (!std::ranges::equal(local_hash, device_hash)) {
    LOGE("Hash mismatch. Local: {:02x}{:02x}... Device: {:02x}{:02x}...",
         local_hash[0], local_hash[1], device_hash[0], device_hash[1]);
    return std::unexpected(FtError::kResultFail);
  }

  LOGD("Verification successful.");
  return {};
}

static std::expected<void, FtError> FtMcuReset(const UsbDevice& device,
                                               uint32_t target_addr) {
  if (target_addr != kFocalRomCfgAddr && target_addr != kFocalBootCfgAddr) {
    LOGE("Reset address invalid: {:#x}", target_addr);
    return std::unexpected(FtError::kParameterError);
  }
  // Passing 0s for length and aux_address to reach the flags parameter
  return SendAction(device, kFtSubcmdSoftReset, 0, {},
                    TransferDirection::kSendOnly, kMcuResetTimeout, 0, 0,
                    kSoftPor);
}

// -----------------------------------------------------------------------------
// Get boot version string from device
// -----------------------------------------------------------------------------
static std::expected<void, FtError> FtGetBootVersion(const UsbDevice& device) {
  auto cmd = BuildFtCmd(kFtCmdReadInfo, kFtSubcmdReadData, 0, 0, 0, 0);
  std::vector<uint8_t> data(kFtBootVersionMax, 0);

  auto res = SendBulkCommand(device, cmd, data, TransferDirection::kIn,
                             kGenericInfoTimeout);
  if (!res) {
    LOGE("Failed to read boot version");
    return res;
  }

  // Ensure null termination
  data.back() = '\0';
  std::string version(reinterpret_cast<char*>(data.data()));
  LOGI("Boot version: {}", version);
  return {};
}
static std::expected<bool, FtError> IsEcBinHasConfigPage(
    std::string_view file_path, uint32_t& code_start_addr) {
  std::ifstream file(std::string{file_path}, std::ios::binary);
  if (!file) {
    LOGE("Cannot open {}", file_path);
    return std::unexpected(FtError::kNoSuchFile);
  }

  FtConfigPage config{};
  file.read(reinterpret_cast<char*>(&config), sizeof(config));
  if (!file.good() && !file.eof()) {
    LOGE("Failed to read config page");
    return std::unexpected(FtError::kResultFail);
  }

  if (config.code_valid_control_word != kCodeValidMagic) {
    return false;
  }

  code_start_addr = config.code_start_address;
  LOGD("Config page found: start=0x{:x}", code_start_addr);
  return true;
}

std::expected<void, FtError> UpdateFw(const UsbDevice& device,
                                      FtDeviceInfo& info) {
  std::string boot_version;
  uint32_t target_addr = kFocalRomCfgAddr;
  uint32_t code_start_addr = 0;

  auto abort_update = [&](FtError err) {
    if (target_addr == kFocalBootCfgAddr || target_addr == kFocalRomCfgAddr) {
      // Erase the config page so the device stays in its safe bootloader/rom
      // state
      FtErasePage(device, kFocalConfigSize, target_addr);
      LOGW("Update aborted. Staying in {} mode",
           target_addr == kFocalBootCfgAddr ? "boot" : "rom");
    }
    return std::unexpected(err);
  };

  // Normal firmware update: check config page
  auto config_res = IsEcBinHasConfigPage(info.ec_bin, code_start_addr);
  if (!config_res) return std::unexpected(config_res.error());
  if (!*config_res) {
    LOGE("Firmware lacks config page, cannot upgrade");
    return std::unexpected(FtError::kParameterError);
  }

  // Determine target address based on current mode
  if (info.current_mode == FtDeviceMode::kBoot) {
    target_addr = kFocalBootCfgAddr;
    if (code_start_addr != kFocalBootRunAddr) {
      LOGE("EC binary not for bootloader mode");
      return std::unexpected(FtError::kParameterError);
    }
  } else if (info.current_mode == FtDeviceMode::kRom) {
    if (code_start_addr == kFocalBootRunAddr) {
      LOGE("EC binary not for ROM mode");
      return std::unexpected(FtError::kParameterError);
    }
    if (code_start_addr != kFocalRomRunAddr) {
      LOGE("EC binary start address mismatch");
      return std::unexpected(FtError::kParameterError);
    }
  } else {
    LOGE("Invalid mode for update");
    return std::unexpected(FtError::kInvalidMode);
  }

  auto res = FtGetBootVersion(device);
  if (!res) {
    LOGE("Get boot version failed");
    return abort_update(res.error());
  }

  if (info.ec_bin.empty()) {
    LOGE("ec bin is null");
    return abort_update(res.error());
  }
  LOGI("Begin firmware update: {}", info.ec_bin);
  // Download, verify, reset
  res = FtDownloadBin(device, info.ec_bin, target_addr);
  if (!res) {
    LOGE("Download failed");
    return abort_update(res.error());
  }

  res = FtVerifyData(device, info.ec_bin, target_addr);
  if (!res) {
    LOGE("Verification failed");
    return abort_update(res.error());
  }

  LOGI("Firmware flashed successfully");

  res = FtMcuReset(device, target_addr);
  if (!res) {
    LOGE("MCU reset failed");
    return abort_update(res.error());
  }

  return {};
}

// -----------------------------------------------------------------------------
// Get flash write-protect area from device (only valid in bootloader mode)
// -----------------------------------------------------------------------------
std::expected<FtFlashWpArea, FtError> FtGetFlashProtect(
    const UsbDevice& device, FtDeviceMode current_mode) {
  if (current_mode != FtDeviceMode::kBoot) {
    LOGE("Get flash protect should only be done in boot mode");
    return std::unexpected(FtError::kInvalidMode);
  }

  LOGI("Getting flash write protect area...");

  std::vector<uint8_t> data(
      512, 0);  // Device returns 512 bytes, first byte holds area

  auto res = SendAction(device, kFtSubcmdFlashWpGet, 0, data,
                        TransferDirection::kIn, kGenericInfoTimeout);
  if (!res) {
    LOGE("Failed to get flash protect status");
    return std::unexpected(res.error());
  }

  auto wp_area = static_cast<FtFlashWpArea>(data[0]);
  LOGI("Flash write-protect area = {}", static_cast<int>(wp_area));
  return wp_area;
}

// -----------------------------------------------------------------------------
// Set flash write-protect area (only valid in bootloader mode)
// wp_area: desired protection area (kAll, kBoot, kRo, kRw)
// -----------------------------------------------------------------------------
std::expected<void, FtError> FtSetFlashProtect(const UsbDevice& device,
                                               FtFlashWpArea wp_area,
                                               FtDeviceMode current_mode) {
  if (current_mode != FtDeviceMode::kBoot) {
    LOGE("Get flash protect should only be done in boot mode");
    return std::unexpected(FtError::kInvalidMode);
  }

  if (wp_area <= FtFlashWpArea::kUnknown ||
      wp_area >= FtFlashWpArea::kOutRange) {
    LOGE("Invalid wp_area: {}", static_cast<int>(wp_area));
    return std::unexpected(FtError::kParameterError);
  }

  LOGI("Setting flash write protect to area: {}", static_cast<int>(wp_area));

  std::vector<uint8_t> dummy(512, 0);  // Some firmware expects a buffer

  auto res = SendAction(
      device, kFtSubcmdFlashWpSet, static_cast<uint32_t>(wp_area), dummy,
      TransferDirection::kIn, kGenericInfoTimeout,
      ~static_cast<uint32_t>(wp_area));  // complement as per original
  if (!res) {
    LOGE("Failed to set flash protect");
    return std::unexpected(res.error());
  }

  // Verify the setting
  auto get_res = FtGetFlashProtect(device, current_mode);
  if (!get_res || *get_res != wp_area) {
    LOGE("Flash protect verification failed");
    return std::unexpected(FtError::kResultFail);
  }

  LOGI("Flash write-protect successfully set to area {}",
       static_cast<int>(wp_area));
  return {};
}

// -----------------------------------------------------------------------------
// Switch device back to ROM boot mode (erase config page and reset)
// -----------------------------------------------------------------------------
std::expected<void, FtError> FtBackToRomBoot(const UsbDevice& device,
                                             FtDeviceMode current_mode) {
  if (current_mode != FtDeviceMode::kBoot) {
    LOGE("Current mode cannot switch to ROM via back command");
    return std::unexpected(FtError::kInvalidMode);
  }
  auto erase_res = FtErasePage(device, kFocalConfigSize, kFocalRomCfgAddr);
  if (!erase_res) {
    LOGE("Failed to erase ROM config page");
    return std::unexpected(erase_res.error());
  }

  auto reset_res = FtMcuReset(device, kFocalRomCfgAddr);
  if (!reset_res) {
    LOGE("Failed to reset MCU to ROM");
    return std::unexpected(reset_res.error());
  }

  LOGD("Device switched to ROM boot mode");
  return {};
}