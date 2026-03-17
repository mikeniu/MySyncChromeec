/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bootrom.h"

#include "egis_util.h"

// C system files
#include <cstring>

// C++ system files
#include <algorithm>
#include <bit>
#include <chrono>
#include <concepts>
#include <fstream>
#include <limits>
#include <print>
#include <ranges>
#include <thread>
#include <vector>

// Other libraries' .h files
#include <openssl/aes.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

using namespace std::chrono_literals;

namespace {
struct SecureCommandShaHeader {
  uint8_t command_id;
  uint8_t algo;
  uint32_t payload_len;
  std::array<uint8_t, BootromComm::kSha256DigestSize> hash;
} __attribute__((packed));

struct SecureCommandAesHeader {
  uint8_t command_id;
  uint8_t algo;
  uint32_t payload_len;
  std::array<uint8_t, BootromComm::kAesGcmIvSize> iv;
  std::array<uint8_t, BootromComm::kAesGcmTagSize> tag;
} __attribute__((packed));
}  // namespace

using EvpCipherCtxPtr =
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;

int GenerateGcmIv(std::span<uint8_t> iv_buf) {
  if (RAND_bytes(iv_buf.data(), iv_buf.size()) != 1) {
    std::println(stderr,
                 "[ERROR] Cryptographically secure RNG failed! "
                 "Critical security risk, aborting.");
    return -1;
  }
  return 0;
}

bool AesGcmEncryptOpenssl(std::span<const uint8_t> key,
                          std::span<const uint8_t> iv,
                          std::span<const uint8_t> aad,
                          std::span<const uint8_t> plaintext,
                          std::span<uint8_t> ciphertext,
                          std::span<uint8_t> tag) {
  EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &::EVP_CIPHER_CTX_free);
  if (!ctx) return false;

  int len;

  if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
    return false;
  }

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv.size(), NULL) !=
      1) {
    return false;
  }

  if (EVP_EncryptInit_ex(ctx.get(), NULL, NULL, key.data(), iv.data()) != 1) {
    return false;
  }

  if (!aad.empty()) {
    if (EVP_EncryptUpdate(ctx.get(), NULL, &len, aad.data(), aad.size()) != 1) {
      return false;
    }
  }

  if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &len, plaintext.data(),
                        plaintext.size()) != 1) {
    return false;
  }

  if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + len, &len) != 1) {
    return false;
  }

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, tag.size(),
                          tag.data()) != 1) {
    return false;
  }

  return true;
}

bool AesGcmDecryptOpenssl(std::span<const uint8_t> key,
                          std::span<const uint8_t> iv,
                          std::span<const uint8_t> aad,
                          std::span<const uint8_t> ciphertext,
                          std::span<const uint8_t> tag,
                          std::span<uint8_t> plaintext) {
  EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &::EVP_CIPHER_CTX_free);
  if (!ctx) return false;

  int len;

  if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
    return false;
  }

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv.size(), NULL) !=
      1) {
    return false;
  }

  if (EVP_DecryptInit_ex(ctx.get(), NULL, NULL, key.data(), iv.data()) != 1) {
    return false;
  }

  if (!aad.empty()) {
    if (EVP_DecryptUpdate(ctx.get(), NULL, &len, aad.data(), aad.size()) != 1) {
      return false;
    }
  }

  if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, ciphertext.data(),
                        ciphertext.size()) != 1) {
    return false;
  }

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag.size(),
                          const_cast<uint8_t *>(tag.data())) != 1) {
    return false;
  }

  if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len) <= 0) {
    return false;
  }

  return true;
}

// ========================================================
// Bootrom Communication Implementation
// ========================================================
BootromComm::~BootromComm() {
  OPENSSL_cleanse(aes_key_.data(), aes_key_.size());
}

std::expected<std::vector<uint8_t>, BootromError> BootromComm::DoUsbTransfer(
    std::span<const uint8_t> tx_cmd, size_t min_expected_rx_size,
    std::chrono::milliseconds timeout) {
  DumpRawCmd(tx_cmd);

  if (auto res = usb_.BulkWrite(kBootromEpOutBulk, tx_cmd, timeout); !res)
    return std::unexpected(BootromError::kUsbWriteFailed);

  std::vector<uint8_t> ret(kMaxPayloadSize + min_expected_rx_size, 0);
  auto read_res = usb_.BulkRead(kBootromEpInBulk, ret, timeout);
  if (!read_res || read_res.value() <= 0)
    return std::unexpected(BootromError::kUsbReadFailed);

  ret.resize(read_res.value());

  if (ret.size() < min_expected_rx_size)
    return std::unexpected(BootromError::kResponseTruncated);
  if (ret[0] != 0 || ret[1] != 0)
    return std::unexpected(BootromError::kInvalidStatusHeader);

  return ret;
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandRaw(std::span<const uint8_t> data,
                                   size_t resp_size,
                                   std::chrono::milliseconds timeout) {
  auto ret_res = DoUsbTransfer(data, kStatusHeaderSize, timeout);
  if (!ret_res) return std::unexpected(ret_res.error());
  auto &ret = ret_res.value();
  return std::vector<uint8_t>(ret.begin() + kStatusHeaderSize, ret.end());
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandSha(std::span<const uint8_t> data,
                                   size_t resp_size,
                                   std::chrono::milliseconds timeout) {
  SecureCommandShaHeader header;
  header.command_id = EgisBootrom::kCmdSecureWrapped;
  header.algo = static_cast<uint8_t>(wrapped_algo_);
  if (data.size() > std::numeric_limits<uint32_t>::max()) {
    std::println(stderr, "[ERROR] Payload size exceeds uint32_t max: {}",
                 data.size());
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }
  uint32_t len = static_cast<uint32_t>(data.size());
  if constexpr (std::endian::native != std::endian::little) {
    len = std::byteswap(len);
  }
  header.payload_len = len;
  SHA256(data.data(), data.size(), header.hash.data());

  std::vector<uint8_t> cmd;
  cmd.reserve(sizeof(header) + data.size());
  const auto *header_bytes = reinterpret_cast<const uint8_t *>(&header);
  cmd.append_range(std::span(header_bytes, sizeof(header)));
  cmd.append_range(data);

  auto ret_res = DoUsbTransfer(cmd, kScHeaderSizeSha, timeout);
  if (!ret_res) return std::unexpected(ret_res.error());
  auto &ret = ret_res.value();

  std::span<const uint8_t> ret_span = ret;
  auto len_res =
      ReadLittleEndian<uint32_t>(ret_span.subspan(kStatusHeaderSize));
  if (!len_res) return std::unexpected(BootromError::kResponseTruncated);
  uint32_t rsp_wrapped_len = len_res.value();

  if (rsp_wrapped_len > ret.size() - kScHeaderSizeSha) {
    rsp_wrapped_len = ret.size() - kScHeaderSizeSha;
  }

  auto rsp_payload = ret_span.subspan(kScHeaderSizeSha, rsp_wrapped_len);

  return std::vector<uint8_t>(rsp_payload.begin(), rsp_payload.end());
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandAes(std::span<const uint8_t> data,
                                   size_t resp_size,
                                   std::chrono::milliseconds timeout) {
  SecureCommandAesHeader header;
  header.command_id = EgisBootrom::kCmdSecureWrapped;
  header.algo = static_cast<uint8_t>(wrapped_algo_);
  if (data.size() > std::numeric_limits<uint32_t>::max()) {
    std::println(stderr, "[ERROR] Payload size exceeds uint32_t max: {}",
                 data.size());
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }
  uint32_t len = static_cast<uint32_t>(data.size());
  if constexpr (std::endian::native != std::endian::little) {
    len = std::byteswap(len);
  }
  header.payload_len = len;

  if (GenerateGcmIv(header.iv) != 0) {
    return std::unexpected(BootromError::kGcmIvGenerationFailed);
  }
  // The Additional Authenticated Data (AAD) is required by the Bootrom
  // protocol. It represents the Chip ID and revision as a 32-bit
  // little-endian integer. {0x01, 0x00, 0x00, 0x00} explicitly sets this
  // identifier for AES-GCM authentication.
  std::vector<uint8_t> ciphertext(data.size());
  uint8_t aad[4] = {0x01, 0x00, 0x00, 0x00};

  if (!AesGcmEncryptOpenssl(aes_key_, header.iv, aad, data, ciphertext,
                            header.tag)) {
    return std::unexpected(BootromError::kGcmEncryptionFailed);
  }

  std::vector<uint8_t> cmd;
  cmd.reserve(sizeof(header) + ciphertext.size());
  const auto *header_bytes = reinterpret_cast<const uint8_t *>(&header);
  cmd.append_range(std::span(header_bytes, sizeof(header)));
  cmd.append_range(ciphertext);

  auto ret_res = DoUsbTransfer(cmd, kScHeaderSizeAes, timeout);
  if (!ret_res) return std::unexpected(ret_res.error());
  auto &ret = ret_res.value();

  std::span<uint8_t> ret_span = ret;
  auto len_res =
      ReadLittleEndian<uint32_t>(ret_span.subspan(kStatusHeaderSize));
  if (!len_res) return std::unexpected(BootromError::kResponseTruncated);
  uint32_t rsp_wrapped_len = len_res.value();

  if (rsp_wrapped_len > ret.size() - kScHeaderSizeAes) {
    rsp_wrapped_len = ret.size() - kScHeaderSizeAes;
  }

  size_t offset = kStatusHeaderSize + kWrappedCommandSize;
  auto rsp_iv = ret_span.subspan(offset, kAesGcmIvSize);
  offset += kAesGcmIvSize;
  auto rsp_tag = ret_span.subspan(offset, kAesGcmTagSize);
  offset += kAesGcmTagSize;
  auto rsp_payload = ret_span.subspan(offset, rsp_wrapped_len);

  if (!AesGcmDecryptOpenssl(aes_key_, rsp_iv, aad, rsp_payload, rsp_tag,
                            rsp_payload)) {
    std::println(stderr,
                 "[ERROR] AES-GCM Authentication Failed (Tag mismatch)");
    return std::unexpected(BootromError::kGcmAuthenticationFailed);
  }

  return std::vector<uint8_t>(rsp_payload.begin(), rsp_payload.end());
}

std::expected<void, BootromError> BootromComm::WaitForFlashReady(
    std::chrono::milliseconds timeout) {
  auto start_time = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start_time < timeout) {
    auto sr1 = GetSr1();
    if (!sr1.has_value()) {
      std::println(stderr, "[ERROR] Failed to read SR1 during polling.");
      return std::unexpected(sr1.error());
    }
    if ((sr1.value() & EgisBootrom::kFlashSr1Wip) == 0) {
      return {};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::println(stderr, "[ERROR] Flash ready timeout.");
  return std::unexpected(BootromError::kFlashReadyTimeout);
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommand(std::span<const uint8_t> data, size_t resp_size,
                                std::chrono::milliseconds timeout) {
  if ((data[0] >= EgisBootrom::kCmdMemRead) &&
      (data[0] <= EgisBootrom::kCmdOtpWrite)) {
    switch (wrapped_algo_) {
      case EgisCryptoType::kSha256:
        return SendBootromCommandSha(data, resp_size, timeout);
      case EgisCryptoType::kHmacSha256:
        std::println(stderr, "[ERROR] HMAC is planned but not supported yet.");
        return std::unexpected(BootromError::kUnsupportedCrypto);
      case EgisCryptoType::kAesGcm:
        return SendBootromCommandAes(data, resp_size, timeout);
      default:
        std::println(stderr, "--- Wrapped_Algo is not defined ---");
        return std::unexpected(BootromError::kUnsupportedCrypto);
    }
  }
  return SendBootromCommandRaw(data, resp_size, timeout);
}

std::expected<void, BootromError> BootromComm::WriteReg(uint32_t addr,
                                                        uint32_t value) {
  std::vector<uint8_t> cmd = {EgisBootrom::kCmdMemWrite};
  AppendLittleEndian(cmd, addr);
  cmd.push_back(EgisBootrom::kAccessSizeUint32);
  AppendLittleEndian(cmd, uint32_t{1});
  AppendLittleEndian(cmd, value);

  auto resp = SendBootromCommand(cmd, 0);
  if (!resp) return std::unexpected(resp.error());
  return {};
}

std::expected<uint32_t, BootromError> BootromComm::ReadReg(uint32_t addr) {
  std::vector<uint8_t> cmd = {EgisBootrom::kCmdMemRead};
  AppendLittleEndian(cmd, addr);
  cmd.push_back(EgisBootrom::kAccessSizeUint32);
  AppendLittleEndian(cmd, uint32_t{1});

  auto resp = SendBootromCommand(cmd, 4);
  if (!resp) return std::unexpected(resp.error());
  if (resp->size() < 4)
    return std::unexpected(BootromError::kResponseTruncated);

  auto value_out = ReadLittleEndian<uint32_t>(*resp);
  if (!value_out) return std::unexpected(BootromError::kResponseTruncated);
  return value_out.value();
}

std::expected<void, BootromError> BootromComm::WriteEnable() {
  WriteReg(EgisBootrom::kSpiRegTfmat, 0);
  uint32_t tctrl = EgisBootrom::kTrnsModeNoneData | EgisBootrom::kTctrlCmdEnMsk;
  WriteReg(EgisBootrom::kSpiRegTctrl, tctrl);
  WriteReg(EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat);
  WriteReg(EgisBootrom::kSpiRegCmd, EgisBootrom::kFlashWren);
  return WaitForFlashReady();
}

std::expected<uint8_t, BootromError> BootromComm::GetSr1() {
  WriteReg(EgisBootrom::kSpiRegRdcnt, 0);
  uint32_t tctrl = EgisBootrom::kTrnsModeReadOnly | EgisBootrom::kTctrlCmdEnMsk;
  WriteReg(EgisBootrom::kSpiRegTctrl, tctrl);
  WriteReg(EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat);
  WriteReg(EgisBootrom::kSpiRegCmd, EgisBootrom::kFlashRdsr);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  auto res = ReadReg(EgisBootrom::kSpiRegData);
  if (!res) return std::unexpected(res.error());

  return static_cast<uint8_t>(res.value() & 0xFF);
}

std::expected<void, BootromError> BootromComm::SetSr1(uint8_t sr1) {
  if (auto res = WriteEnable(); !res) return res;

  if (auto res = WriteReg(EgisBootrom::kSpiRegWrcnt, 0); !res) return res;
  uint32_t tctrl =
      EgisBootrom::kTrnsModeWriteOnly | EgisBootrom::kTctrlCmdEnMsk;
  if (auto res = WriteReg(EgisBootrom::kSpiRegTctrl, tctrl); !res) return res;
  if (auto res = WriteReg(EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat); !res)
    return res;
  if (auto res = WriteReg(EgisBootrom::kSpiRegData, static_cast<uint32_t>(sr1));
      !res)
    return res;
  if (auto res = WriteReg(EgisBootrom::kSpiRegCmd, EgisBootrom::kFlashWrsr);
      !res)
    return res;

  if (auto res = WaitForFlashReady(); !res) {
    std::println(stderr, "[WARN] SetSr1 timed out waiting for flash.");
    return res;
  }
  return {};
}

std::expected<void, BootromError> BootromComm::EnableRawCmdDump(
    std::string_view filename) {
  raw_cmd_dump_file_.open(std::string(filename),
                          std::ios::binary | std::ios::trunc);
  if (!raw_cmd_dump_file_.is_open()) {
    return std::unexpected(BootromError::kFileOperationFailed);
  }
  return {};
}

void BootromComm::DumpRawCmd(std::span<const uint8_t> cmd) {
  if (!raw_cmd_dump_file_.is_open()) return;
  raw_cmd_dump_file_.write(reinterpret_cast<const char *>(cmd.data()),
                           cmd.size());
  raw_cmd_dump_file_.flush();
}

std::expected<void, BootromError> BootromComm::BackupFlash(
    std::string_view filename, uint32_t total_size) {
  std::ofstream outfile(std::string(filename),
                        std::ios::binary | std::ios::trunc);
  if (!outfile.is_open())
    return std::unexpected(BootromError::kFileOperationFailed);

  const uint32_t len_chunk = 4096;

  std::println("--- Executing CMD_MEM_READ (0x20) ---");

  for (uint32_t addr = 0; addr < total_size; addr += len_chunk) {
    uint32_t current_read = std::min(len_chunk, total_size - addr);

    std::vector<uint8_t> data = {EgisBootrom::kCmdMemRead};
    uint32_t flash_addr = EgisBootrom::kFlashBaseAddr + addr;
    AppendLittleEndian(data, flash_addr);
    data.push_back(EgisBootrom::kAccessSizeUint8);
    AppendLittleEndian(data, current_read);

    auto resp = SendBootromCommand(data, current_read, kFlashTimeout);
    if (!resp.has_value()) {
      std::println(stderr, "\n[HALT] Failed at address 0x{:x}", addr);
      return std::unexpected(resp.error());
    }

    size_t received = resp->size();

    if (received < current_read) {
      std::println(stderr,
                   "\n[WARN] Incomplete payload received, only got {} bytes",
                   received);
      return std::unexpected(BootromError::kResponseTruncated);
    }
    outfile.write(reinterpret_cast<const char *>(resp->data()), received);

    std::print("\rBackup Progress: {} / {} ({:.1f}%)", addr + current_read,
               total_size, (100.0 * (addr + current_read) / total_size));
    std::fflush(stdout);
  }

  outfile.close();
  std::println("\nFlash Backup Completed");
  return {};
}

std::expected<std::vector<uint8_t>, BootromError> BootromComm::ReadFlash(
    uint32_t offset, uint32_t length) {
  std::vector<uint8_t> buffer;
  const uint32_t len_chunk = 1024;

  for (uint32_t addr_offset = 0; addr_offset < length;
       addr_offset += len_chunk) {
    uint32_t current_read = std::min(len_chunk, length - addr_offset);
    uint32_t addr = offset + addr_offset;

    std::vector<uint8_t> cmd = {EgisBootrom::kCmdMemRead};
    uint32_t flash_addr = EgisBootrom::kFlashBaseAddr + addr;
    AppendLittleEndian(cmd, flash_addr);
    cmd.push_back(EgisBootrom::kAccessSizeUint8);
    AppendLittleEndian(cmd, current_read);

    auto resp = SendBootromCommand(cmd, current_read, kFlashTimeout);
    if (!resp.has_value()) {
      std::println(stderr, "[ERROR] read_flash failed at addr 0x{:x}: {}",
                   flash_addr, ToString(resp.error()));
      return std::unexpected(resp.error());
    }
    if (resp->size() < current_read) {
      std::println(stderr,
                   "[ERROR] read_flash failed at addr 0x{:x} (size mismatch: "
                   "got {}, expected {})",
                   flash_addr, resp->size(), current_read);
      return std::unexpected(BootromError::kResponseTruncated);
    }

    buffer.insert(buffer.end(), resp->begin(), resp->end());
  }
  return buffer;
}

std::expected<void, BootromError> BootromComm::EraseFlash(uint32_t offset,
                                                          uint32_t size) {
  std::println("--- Executing CMD_FLASH_ERASE (0x22) ---");

  std::vector<uint8_t> data = {EgisBootrom::kCmdFlashErase};
  uint32_t addr = EgisBootrom::kFlashBaseAddr + offset;
  AppendLittleEndian(data, addr);
  AppendLittleEndian(data, size);

  auto resp = SendBootromCommand(data, 0, kFlashTimeout);

  if (resp.has_value()) {
    std::println("Erase Success: {} bytes at 0x{:08x}", size, addr);
    return {};
  } else {
    std::println(stderr, "Erase Failed!");
    return std::unexpected(resp.error());
  }
}

std::expected<void, BootromError> BootromComm::WriteFlash(
    uint32_t offset, std::span<const uint8_t> data) {
  if (data.size() > kMaxFlashWriteSize) {
    std::println(stderr,
                 "[ERROR] Payload size {} exceeds max chunk limit of {}",
                 data.size(), kMaxFlashWriteSize);
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }

  std::vector<uint8_t> cmd;
  cmd.reserve(10 + data.size());

  uint32_t addr = EgisBootrom::kFlashBaseAddr + offset;
  cmd.push_back(EgisBootrom::kCmdMemWrite);
  AppendLittleEndian(cmd, addr);
  cmd.push_back(EgisBootrom::kAccessSizeUint8);
  if (data.size() > std::numeric_limits<uint32_t>::max()) {
    std::println(stderr, "[ERROR] Payload size exceeds uint32_t max: {}",
                 data.size());
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }
  AppendLittleEndian(cmd, static_cast<uint32_t>(data.size()));
  cmd.insert(cmd.end(), data.begin(), data.end());

  auto resp = SendBootromCommand(cmd, 0, kFlashTimeout);

  if (!resp.has_value()) {
    std::println(stderr, "\n[HALT] USB/Bulk transmission failed at 0x{:x}: {}",
                 addr, ToString(resp.error()));
    return std::unexpected(resp.error());
  }

  if (auto res = WaitForFlashReady(); !res) {
    std::println(
        stderr,
        "\n[HALT] Timeout waiting for flash write to complete at 0x{:x}", addr);
    return std::unexpected(res.error());
  }
  return {};
}

std::expected<void, BootromError> BootromComm::DisableWatchdog() {
  std::println("--- Disabling Watchdog ---");
  if (auto res =
          WriteReg(EgisBootrom::kRegPwmCtrl, EgisBootrom::kPwmCtrlDisableMask);
      !res)
    return res;
  if (auto res = WriteReg(EgisBootrom::kRegPwmReload,
                          EgisBootrom::kPwmReloadTimeoutVal);
      !res)
    return res;
  if (auto res =
          WriteReg(EgisBootrom::kRegPwmChnen, EgisBootrom::kPwmChnenDisableVal);
      !res)
    return res;
  if (auto res =
          WriteReg(EgisBootrom::kRegPwmMux, EgisBootrom::kPwmMuxDisableVal);
      !res)
    return res;
  return {};
}

std::expected<void, BootromError> BootromComm::DisableWp() {
  auto sr1_old_res = GetSr1();
  if (!sr1_old_res) {
    std::println(stderr, "Failed to read initial SR1 value");
    return std::unexpected(sr1_old_res.error());
  }
  std::println("Old SR1: 0x{:02x}", sr1_old_res.value());

  if (auto res = SetSr1(0); !res) {
    return res;
  }

  auto sr1_new_res = GetSr1();
  if (!sr1_new_res) {
    std::println(stderr, "Failed to read SR1 after attempting to clear WP");
    return std::unexpected(sr1_new_res.error());
  }
  std::println("New SR1: 0x{:02x}", sr1_new_res.value());

  if (sr1_new_res.value() != 0) {
    std::println(stderr, "Failed to clear WP bits");
    return std::unexpected(BootromError::kFlashWpClearFailed);
  }
  return {};
}

std::expected<void, BootromError> BootromComm::GetBootromInfo() {
  std::vector<uint8_t> cmd = {EgisBootrom::kCmdGetBootInfo};
  auto resp = SendBootromCommand(cmd, 12);
  if (!resp) return std::unexpected(resp.error());

  if (resp->size() >= 12) {
    std::span<const uint8_t> payload = resp.value();
    auto version = ReadLittleEndian<uint32_t>(payload);
    auto max_size = ReadLittleEndian<uint32_t>(payload.subspan(4));
    auto max_fw_upgrade_size = ReadLittleEndian<uint32_t>(payload.subspan(8));

    if (!version || !max_size || !max_fw_upgrade_size) {
      std::println(stderr, "[ERROR] Truncated response in GetBootromInfo");
      return std::unexpected(BootromError::kResponseTruncated);
    }
    std::println("Bootrom Version: 0x{:x}", version.value());
    std::println("Max Command Size: {} bytes", max_size.value());
    std::println("Max FW Upgrade Size: {} bytes", max_fw_upgrade_size.value());
  }
  return {};
}

std::expected<void, BootromError> BootromComm::GetFlashInfo() {
  std::vector<uint8_t> cmd = {EgisBootrom::kCmdFlashInfo};

  std::println("--- Fetching Flash Information ---");

  auto resp = SendBootromCommand(cmd, 40);
  if (!resp.has_value()) {
    std::println(stderr, "[ERROR] Unable to retrieve Flash info.");
    return std::unexpected(resp.error());
  }

  if (resp->size() < 40) {
    std::println(stderr, "[ERROR] Response length insufficient: {}",
                 resp->size());
    return std::unexpected(BootromError::kResponseTruncated);
  }

  std::span<const uint8_t> payload = resp.value();
  auto flash_size = ReadLittleEndian<uint32_t>(payload);
  if (!flash_size) {
    std::println(stderr, "[ERROR] Truncated response in GetFlashInfo");
    return std::unexpected(BootromError::kResponseTruncated);
  }

  std::array<char, 9> prod_name{};
  std::memcpy(prod_name.data(), resp->data() + 8, 8);

  std::println("[SUCCESS] Flash Data Received:");
  std::println("  Product Name  : {}", prod_name.data());
  std::println("  Flash Size  : {} bytes ({} MB)", flash_size.value(),
               flash_size.value() / 1024 / 1024);
  return {};
}

std::expected<void, BootromError> BootromComm::SystemReset() {
  std::println("--- Executing System Reset (0x12) ---");

  std::vector<uint8_t> cmd = {EgisBootrom::kCmdSystemReset, 0x01};

  auto resp = SendBootromCommand(cmd, 0, kResetTimeout);

  if (resp.has_value()) {
    std::println("[SUCCESS] Reset command acknowledged.");
    return {};
  } else {
    std::println("[INFO] Command sent, device is resetting...");
    return {};
  }
}
