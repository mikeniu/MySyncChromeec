/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_BOOTROM_H_
#define UTIL_EGIS_BOOTROM_H_

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bootrom_protocol.h"
#include "crypto_util.h"
#include "egis_util.h"
#include "usb_interface.h"

namespace egis {

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
  kFlashWpClearFailed,
  kFileOperationFailed,
  kBufferTooSmall,
  kInvalidFlashAddress,
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
    case BootromError::kBufferTooSmall:
      return "Provided buffer is too small for serialization";
    case BootromError::kInvalidFlashAddress:
      return "Flash offset and size exceed total flash capacity";
    case BootromError::kUnknown:
      return "An unknown error occurred";
  }
  return "An unknown error occurred";
}

class BootromComm {
 public:
  static constexpr size_t kMaxPayloadSize = 4 * 1024;
  static constexpr size_t kMaxFlashWriteSize = 32 * 1024;
  static constexpr size_t kMaxFlashReadSize = 4 * 1024;
  static constexpr auto kFlashTimeout = std::chrono::milliseconds(5000);
  static constexpr auto kResetTimeout = std::chrono::milliseconds(500);
  static constexpr auto kDefaultCommandTimeout = std::chrono::milliseconds(200);
  static constexpr auto kFlashReadyPollTimeout = std::chrono::milliseconds(500);
  static constexpr auto kFlashReadyPollInterval = std::chrono::milliseconds(1);
  static constexpr auto kFlashStatusReadDelay = std::chrono::milliseconds(10);

  struct RegWrite {
    uint32_t address;
    uint32_t value;
  };

  explicit BootromComm(UsbInterface& usb, bool dry_run = false)
      : dry_run_(dry_run), usb_(usb) {}
  ~BootromComm();
  void SetAesKey(std::span<const uint8_t, crypto::kAes256KeySize> key) {
    std::ranges::copy(key, aes_key_.begin());
  }
  std::expected<void, BootromError> EnableRawCmdDump(std::string_view filename);

  std::expected<bootrom_protocol::BootromInfoResponse, BootromError>
  GetBootromInfo();
  std::expected<bootrom_protocol::FlashInfoResponse, BootromError>
  GetFlashInfo();
  std::expected<void, BootromError> SystemReset();

  std::expected<void, BootromError> DisableWatchdog();
  std::expected<void, BootromError> DisableWp();

  std::expected<uint32_t, BootromError> ReadReg(uint32_t addr);
  std::expected<void, BootromError> WriteReg(uint32_t addr, uint32_t value);
  std::expected<void, BootromError> WriteRegs(
      std::initializer_list<RegWrite> regs);
  std::expected<void, BootromError> BackupFlash(std::string_view filename,
                                                uint32_t backup_size);
  std::expected<std::vector<uint8_t>, BootromError> ReadFlash(uint32_t offset,
                                                              uint32_t size);
  std::expected<void, BootromError> EraseFlash(uint32_t offset, uint32_t size);
  std::expected<void, BootromError> WriteFlash(uint32_t offset,
                                               std::span<const uint8_t> data);
  std::expected<void, BootromError> WriteEnable();

  void SetCryptoAlgorithm(EgisCryptoType algo) { crypto_algorithm_ = algo; }

  template <typename T>
  std::expected<std::vector<uint8_t>, BootromError> SendStruct(
      const T& command,
      std::chrono::milliseconds timeout = kDefaultCommandTimeout) {
    return SendBootromCommand(AsBytes(command), timeout);
  }

  template <TriviallyCopyable Resp, TriviallyCopyable Req>
  std::expected<Resp, BootromError> SendAndReadStruct(
      const Req& req,
      std::chrono::milliseconds timeout = kDefaultCommandTimeout) {
    auto resp = SendStruct(req, timeout);
    if (!resp) return std::unexpected(resp.error());

    auto parsed = ReadStruct<Resp>(*resp);
    if (!parsed) return std::unexpected(BootromError::kResponseTruncated);
    return *parsed;
  }

  // Public for testing
  std::expected<std::vector<uint8_t>, BootromError> SendBootromCommand(
      std::span<const uint8_t> unwrapped_command,
      std::chrono::milliseconds timeout = kDefaultCommandTimeout);

  std::expected<std::vector<uint8_t>, BootromError> SendPrewrappedPacket(
      std::span<const uint8_t> tx_cmd,
      std::chrono::milliseconds timeout = kDefaultCommandTimeout);

 private:
  EgisCryptoType crypto_algorithm_ = EgisCryptoType::kSha256;
  std::array<uint8_t, crypto::kAes256KeySize> aes_key_{};
  std::ofstream raw_cmd_dump_file_;
  bool dry_run_ = false;

  UsbInterface& usb_;
  std::vector<uint8_t> GetMockSecureResponse(
      std::span<const uint8_t> unwrapped_command);
  std::expected<uint8_t, BootromError> ReadStatusRegister1();
  std::expected<void, BootromError> WriteStatusRegister1(uint8_t status_value);

  std::expected<void, BootromError> WaitForFlashReady(
      std::chrono::milliseconds timeout = kFlashReadyPollTimeout);

  std::expected<std::span<const uint8_t>, BootromError> ExtractSecureData(
      std::span<const uint8_t> raw_response);

  void DumpRawCmd(std::span<const uint8_t> cmd);

  std::expected<std::vector<uint8_t>, BootromError> SendBootromCommandRaw(
      std::span<const uint8_t> unwrapped_command,
      std::chrono::milliseconds timeout = kDefaultCommandTimeout);

  std::expected<std::vector<uint8_t>, BootromError> SendBootromCommandSha(
      std::span<const uint8_t> unwrapped_command,
      std::chrono::milliseconds timeout = kDefaultCommandTimeout);

  std::expected<std::vector<uint8_t>, BootromError> SendBootromCommandAes(
      std::span<const uint8_t> unwrapped_command,
      std::chrono::milliseconds timeout = kDefaultCommandTimeout);
};

}  // namespace egis

#endif  // UTIL_EGIS_BOOTROM_H_
