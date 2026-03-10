#include <openssl/sha.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include "ft_log.h"
#include "ft_util.h"
#include "usb_context.h"

std::array<uint8_t, 16> BuildFtCmd(uint8_t opcode, uint8_t subcmd,
                                   uint32_t para2, uint32_t para3,
                                   uint32_t para4, uint8_t sub_para);
std::expected<void, FtError> SendBulkCommand(const UsbDevice& device,
                                             std::span<const uint8_t> scsi_cmd,
                                             std::span<uint8_t> data_buffer,
                                             int is_data_in,
                                             uint32_t timeout_ms);

namespace {

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
// -----------------------------------------------------------------------------
// Constants (replace macros where possible)
// -----------------------------------------------------------------------------
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

static std::expected<void, FtError> FtErasePage(const UsbDevice& device,
                                                int length, int addr) {
  auto cmd = BuildFtCmd(
      kFtCmdAction, kFtSubcmdErasePage, addr,
      FtDivRoundUp(static_cast<uint32_t>(length), kFocalConfigSize), 0, 0);
  // endpoints not used in this low‑level wrapper
  // No data transfer, direction none or write (original used WRITE_DATA_TO_USB)
  return SendBulkCommand(device, cmd, {}, false, 6000);
}

static std::expected<void, FtError> FtDownloadPage(
    const UsbDevice& device, uint32_t downaddr, int per_len,
    std::span<const uint8_t> data) {
  auto write_cmd = BuildFtCmd(kFtCmdAction, kFtSubcmdWriteBin, kFtWriteSramAddr,
                              per_len, 0, 0);
  auto code_cmd = BuildFtCmd(kFtCmdAction, kFtSubcmdCodeBin, downaddr, per_len,
                             kFtWriteSramAddr, 0);
  // Write data to SRAM
  auto res = SendBulkCommand(
      device, write_cmd,
      std::span<uint8_t>(const_cast<uint8_t*>(data.data()), per_len), false,
      1000);
  if (!res) {
    LOGE("Write bin error");
    return res;
  }

  // Execute code (burn to flash)
  res = SendBulkCommand(device, code_cmd, {}, false, 1000);
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

  uint8_t hash_mode = 0x04;               // SHA256 mode
  if (target_addr == kFocalRomCfgAddr) {  // FOCAL_ROM_CFG_ADDR
    hash_mode = 0x04 << 4;
  }

  auto verify_cmd = BuildFtCmd(kFtCmdAction, kFtSubcmdVerify, target_addr,
                               firmware_data.size(), 0, hash_mode);

  std::array<uint8_t, SHA256_DIGEST_LENGTH> device_hash{};
  auto io_res = SendBulkCommand(device, verify_cmd, device_hash, true, 1000);
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
  auto cmd = BuildFtCmd(kFtCmdAction, kFtSubcmdSoftReset, 0, 0, 0, kSoftPor);

  return SendBulkCommand(device, cmd, {}, kUsbSendOnly, 200);
}
// -----------------------------------------------------------------------------
// Get boot version string from device
// -----------------------------------------------------------------------------
static std::expected<void, FtError> FtGetBootVersion(const UsbDevice& device) {
  auto cmd = BuildFtCmd(kFtCmdReadInfo, kFtSubcmdReadData, 0, 0, 0, 0);
  std::vector<uint8_t> data(kFtBootVersionMax, 0);

  auto res = SendBulkCommand(device, cmd, data, /*is_data_in=*/true, 200);
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
    goto cleanup;
  }

  if (info.ec_bin.empty()) {
    LOGE("ec bin is null");
    goto cleanup;
  }
  LOGI("Begin firmware update: {}", info.ec_bin);
  // Download, verify, reset
  res = FtDownloadBin(device, info.ec_bin, target_addr);
  if (!res) {
    LOGE("Download failed");
    goto cleanup;
  }

  res = FtVerifyData(device, info.ec_bin, target_addr);
  if (!res) {
    LOGE("Verification failed");
    goto cleanup;
  }

  LOGI("Firmware flashed successfully");

  res = FtMcuReset(device, target_addr);
  if (!res) {
    LOGE("MCU reset failed");
    goto cleanup;
  }

  return {};

cleanup:
  if (target_addr == kFocalBootCfgAddr || target_addr == kFocalRomCfgAddr) {
    FtErasePage(device, kFocalConfigSize, target_addr);
    LOGW("Staying in {} mode",
         target_addr == kFocalBootCfgAddr ? "boot" : "rom");
  }

  return std::unexpected(FtError::kResultFail);
}

// -----------------------------------------------------------------------------
// Get flash write-protect area from device (only valid in bootloader mode)
// -----------------------------------------------------------------------------
std::expected<FtFlashWpArea, FtError> FtGetFlashProtect(
    const UsbDevice& device) {
  auto cmd = BuildFtCmd(kFtCmdAction, kFtSubcmdFlashWpGet, 0, 0, 0, 0);
  std::vector<uint8_t> data(
      512, 0);  // Device returns 512 bytes, first byte holds area

  auto res = SendBulkCommand(device, cmd, data, true, 200);
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
                                               FtFlashWpArea wp_area) {
  if (wp_area <= FtFlashWpArea::kUnknown ||
      wp_area >= FtFlashWpArea::kOutRange) {
    LOGE("Invalid wp_area: {}", static_cast<int>(wp_area));
    return std::unexpected(FtError::kParameterError);
  }

  auto cmd = BuildFtCmd(
      kFtCmdAction, kFtSubcmdFlashWpSet, static_cast<uint32_t>(wp_area),
      ~static_cast<uint32_t>(wp_area),  // complement as per original
      0, 0);
  std::vector<uint8_t> dummy(512, 0);  // Some firmware expects a buffer

  auto res = SendBulkCommand(device, cmd, dummy, true, 200);
  if (!res) {
    LOGE("Failed to set flash protect");
    return std::unexpected(res.error());
  }

  // Verify the setting
  auto get_res = FtGetFlashProtect(device);
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
std::expected<void, FtError> FtBackToRomBoot(const UsbDevice& device) {
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