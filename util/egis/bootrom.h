/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_BOOTROM_H_
#define UTIL_EGIS_BOOTROM_H_

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <fstream>
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

enum class BootromError {
  kUsbWriteFailed,
  kUsbReadFailed,
  kResponseTruncated,
  kInvalidStatusHeader,
  kPayloadSizeExceedsLimit,
  kGcmIvGenerationFailed,
  kGcmEncryptionFailed,
  kGcmAuthenticationFailed,
  kFlashReadyTimeout,
  kUnsupportedCrypto,
  kRegisterReadFailed,
  kRegisterWriteFailed,
  kFlashWpClearFailed,
  kFileOperationFailed,
  kUnknown,
};

constexpr std::string_view ToString(BootromError err) {
  switch (err) {
    case BootromError::kUsbWriteFailed:
      return "USB write failed";
    case BootromError::kUsbReadFailed:
      return "USB read failed or timed out";
    case BootromError::kResponseTruncated:
      return "Response payload is truncated or invalid";
    case BootromError::kInvalidStatusHeader:
      return "Invalid status header in response";
    case BootromError::kPayloadSizeExceedsLimit:
      return "Payload size exceeds supported limit";
    case BootromError::kGcmIvGenerationFailed:
      return "AES-GCM IV generation failed";
    case BootromError::kGcmEncryptionFailed:
      return "AES-GCM encryption failed";
    case BootromError::kGcmAuthenticationFailed:
      return "AES-GCM authentication failed (tag mismatch)";
    case BootromError::kFlashReadyTimeout:
      return "Timeout waiting for flash operation to complete";
    case BootromError::kUnsupportedCrypto:
      return "Unsupported cryptographic algorithm";
    case BootromError::kFlashWpClearFailed:
      return "Failed to clear flash write-protection";
    case BootromError::kFileOperationFailed:
      return "File I/O operation failed";
    default:
      return "An unknown error occurred";
  }
}

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

// Access sizes (Type field) for memory read/write commands
constexpr uint8_t kAccessSizeUint8 = 0x01;
constexpr uint8_t kAccessSizeUint16 = 0x02;
constexpr uint8_t kAccessSizeUint32 = 0x04;

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

// Watchdog / PWM disable payloads
constexpr uint32_t kPwmCtrlDisableMask = 0x00000004;
constexpr uint32_t kPwmReloadTimeoutVal = 0x00070a21;
constexpr uint32_t kPwmChnenDisableVal = 0x00000008;
constexpr uint32_t kPwmMuxDisableVal = 0x0c000004;
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

  static constexpr size_t kMaxPayloadSize = 4096;
  static constexpr size_t kMaxFlashWriteSize = 32768;  // 32 KB chunk limit
  static constexpr auto kFlashTimeout = std::chrono::milliseconds(5000);
  static constexpr auto kResetTimeout = std::chrono::milliseconds(500);

  explicit BootromComm(UsbComm &usb) : usb_(usb) {}
  ~BootromComm();
  void SetAesKey(std::span<const uint8_t, 32> key) {
    std::copy(key.begin(), key.end(), aes_key_.begin());
  }
  std::expected<void, BootromError> EnableRawCmdDump(std::string_view filename);

  // --- Basic Bootrom commands ---
  std::expected<void, BootromError> GetBootromInfo();
  std::expected<void, BootromError> GetFlashInfo();
  std::expected<void, BootromError> SystemReset();

  // --- Security related ---
  std::expected<void, BootromError> DisableWatchdog();
  std::expected<void, BootromError> DisableWp();  // Disable SPI write-protect

  // --- Memory / Flash operations (Secure Channel) ---
  std::expected<uint32_t, BootromError> ReadReg(uint32_t addr);
  std::expected<void, BootromError> WriteReg(uint32_t addr, uint32_t value);
  std::expected<void, BootromError> BackupFlash(std::string_view filename,
                                                uint32_t total_size);
  std::expected<std::vector<uint8_t>, BootromError> ReadFlash(uint32_t offset,
                                                              uint32_t length);
  std::expected<void, BootromError> EraseFlash(uint32_t offset, uint32_t size);
  std::expected<void, BootromError> WriteFlash(uint32_t offset,
                                               std::span<const uint8_t> data);
  std::expected<void, BootromError> WriteEnable();

  void SetWrappedAlgo(EgisCryptoType algo) { wrapped_algo_ = algo; }

 private:
  EgisCryptoType wrapped_algo_ = EgisCryptoType::kSha256;
  std::array<uint8_t, 32> aes_key_{};
  std::ofstream raw_cmd_dump_file_;

  UsbComm &usb_;
  // --- SPI / Flash status register ---
  std::expected<uint8_t, BootromError> GetSr1();
  std::expected<void, BootromError> SetSr1(uint8_t sr1);

  std::expected<void, BootromError> WaitForFlashReady(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

  void DumpRawCmd(std::span<const uint8_t> cmd);

  std::expected<std::vector<uint8_t>, BootromError> DoUsbTransfer(
      std::span<const uint8_t> tx_cmd, size_t min_expected_rx_size,
      std::chrono::milliseconds timeout);

  std::expected<std::vector<uint8_t>, BootromError> SendBootromCommandRaw(
      std::span<const uint8_t> data, size_t resp_size,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(200));

  std::expected<std::vector<uint8_t>, BootromError> SendBootromCommandSha(
      std::span<const uint8_t> data, size_t resp_size,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(200));

  std::expected<std::vector<uint8_t>, BootromError> SendBootromCommandAes(
      std::span<const uint8_t> data, size_t resp_size,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(200));

  std::expected<std::vector<uint8_t>, BootromError> SendBootromCommand(
      std::span<const uint8_t> data, size_t resp_size,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(200));
};

#endif  // UTIL_EGIS_BOOTROM_H_
