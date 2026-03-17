/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_BOOTROM_H_
#define UTIL_EGIS_BOOTROM_H_

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "usb_comm.h"

enum class EgisCryptoType : uint8_t {
  kSha256 = 0x00,
  kHmacSha256 = 0x01,  // TODO: Planned for future release. Not supported yet.
  kAesGcm = 0x02,
};

namespace EgisBootrom {

// Bootrom commands
constexpr uint8_t kCmdGetBootInfo = 0x10;
constexpr uint8_t kCmdFlashInfo = 0x11;
constexpr uint8_t kCmdSystemReset = 0x12;

// Memory / Flash commands
constexpr uint8_t kCmdMemRead = 0x20;
constexpr uint8_t kCmdMemWrite = 0x21;
constexpr uint8_t kCmdFlashErase = 0x22;

// OTP commands
constexpr uint8_t kCmdOtpRead = 0x23;
constexpr uint8_t kCmdOtpWrite = 0x24;

// Secure Command
constexpr uint8_t kCmdSecureWrapped = 0xF4;

// SPI registers
constexpr uint32_t kSpiRegBase = 0xf0b00000;
constexpr uint32_t kSpiRegId = kSpiRegBase + 0x0;
constexpr uint32_t kSpiRegTfmat = kSpiRegBase + 0x10;
constexpr uint32_t kSpiRegWrcnt = kSpiRegBase + 0x18;
constexpr uint32_t kSpiRegRdcnt = kSpiRegBase + 0x1C;
constexpr uint32_t kSpiRegTctrl = kSpiRegBase + 0x20;
constexpr uint32_t kSpiRegCmd = kSpiRegBase + 0x24;
constexpr uint32_t kSpiRegAddr = kSpiRegBase + 0x28;
constexpr uint32_t kSpiRegData = kSpiRegBase + 0x2C;

// TCTRL bit definitions
constexpr uint32_t kTctrlTrnsModeOffset = 24;
constexpr uint32_t kTctrlAddrFmtMsk = (1 << 28);
constexpr uint32_t kTctrlAddrEnMsk = (1 << 29);
constexpr uint32_t kTctrlCmdEnMsk = (1 << 30);

constexpr uint32_t kTrnsModeWriteOnly = (1 << kTctrlTrnsModeOffset);
constexpr uint32_t kTrnsModeReadOnly = (2 << kTctrlTrnsModeOffset);
constexpr uint32_t kTrnsModeNoneData = (7 << kTctrlTrnsModeOffset);

constexpr uint32_t kTfmat = 0x00020700;

// Flash commands
constexpr uint8_t kFlashWren = 0x06;
constexpr uint8_t kFlashWrsr = 0x01;
constexpr uint8_t kFlashRdsr = 0x05;
constexpr uint8_t kFlashSr1Wip = 0x01;

// Flash address map
constexpr uint32_t kFlashBaseAddr = 0x80000000;
constexpr uint32_t kFlashTotalSize = 0x00200000;  // 2 MB

// Watchdog / PWM registers
constexpr uint32_t kRegPwmChnen = 0xf040001c;
constexpr uint32_t kRegPwmCtrl = 0xf0400020;
constexpr uint32_t kRegPwmReload = 0xf0400024;
constexpr uint32_t kRegPwmMux = 0xf0e00100;
}  // namespace EgisBootrom

class BootromComm {
 public:
  static constexpr uint8_t kBootromEpOutBulk = 0x02;
  static constexpr uint8_t kBootromEpInBulk = 0x81;

  static constexpr size_t kStatusHeaderSize = 2;
  static constexpr size_t kWrappedCommandSize = 4;
  static constexpr size_t kAesGcmIvSize = 12;
  static constexpr size_t kAesGcmTagSize = 16;
  static constexpr size_t kSha256DigestSize = 32;
  static constexpr size_t kScHeaderSizeSha =
      kStatusHeaderSize + kWrappedCommandSize + kSha256DigestSize;
  static constexpr size_t kScHeaderSizeAes =
      kStatusHeaderSize + kWrappedCommandSize + kAesGcmIvSize + kAesGcmTagSize;

  explicit BootromComm(UsbComm &usb) : usb_(usb) {}
  ~BootromComm();
  void SetAesKey(const uint8_t *key) {
    std::memcpy(aes_key_.data(), key, aes_key_.size());
  }
  void SetRawCmdDump(std::ofstream *ptr) { raw_cmd_fw_ptr_ = ptr; }

  // --- Basic Bootrom commands ---
  int GetBootromInfo();
  int GetFlashInfo();
  int SystemReset();

  // --- Security related ---
  int DisableWatchdog();
  int DisableWp();  // Disable SPI write-protect

  // --- Memory / Flash operations (Secure Channel) ---
  std::expected<uint32_t, int> ReadReg(uint32_t addr);
  int WriteReg(uint32_t addr, uint32_t value);
  int BackupFlash(std::string_view filename, uint32_t total_size);
  std::expected<std::vector<uint8_t>, int> ReadFlash(uint32_t offset,
                                                     uint32_t length);
  int EraseFlash(uint32_t offset, uint32_t size);
  int WriteFlash(uint32_t offset, std::span<const uint8_t> data);
  int WriteEnable();

  void SetWrappedAlgo(EgisCryptoType algo) { wrapped_algo_ = algo; }

 private:
  EgisCryptoType wrapped_algo_ = EgisCryptoType::kSha256;
  std::array<uint8_t, 32> aes_key_{};
  std::ofstream *raw_cmd_fw_ptr_ = nullptr;

  UsbComm &usb_;
  // --- SPI / Flash status register ---
  std::expected<uint8_t, int> GetSr1();
  void SetSr1(uint8_t sr1);

  int WaitForFlashReady(int timeout_ms = 500);

  void DumpRawCmd(std::span<const uint8_t> cmd);

  std::expected<std::vector<uint8_t>, int> SendBootromCommandRaw(
      std::span<const uint8_t> data, size_t resp_size, int timeout_ms = 200);

  std::expected<std::vector<uint8_t>, int> SendBootromCommandSha(
      std::span<const uint8_t> data, size_t resp_size, int timeout_ms = 200);

  std::expected<std::vector<uint8_t>, int> SendBootromCommandAes(
      std::span<const uint8_t> data, size_t resp_size, int timeout_ms = 200);

  std::expected<std::vector<uint8_t>, int> SendBootromCommand(
      std::span<const uint8_t> data, size_t resp_size, int timeout_ms = 200);
};

#endif  // UTIL_EGIS_BOOTROM_H_
