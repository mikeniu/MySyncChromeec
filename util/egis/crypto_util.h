/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_CRYPTO_UTIL_H_
#define UTIL_EGIS_CRYPTO_UTIL_H_

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace egis::crypto {

enum class CryptoError {
  kContextAllocationFailed,
  kInitFailed,
  kUpdateFailed,
  kFinalizeFailed,
  kTagMismatch,
  kRngFailed,
};

constexpr std::string_view ToString(CryptoError err) {
  switch (err) {
    case CryptoError::kContextAllocationFailed:
      return "Failed to allocate EVP context";
    case CryptoError::kInitFailed:
      return "Cipher initialization failed";
    case CryptoError::kUpdateFailed:
      return "Cipher update failed";
    case CryptoError::kFinalizeFailed:
      return "Cipher finalization failed";
    case CryptoError::kTagMismatch:
      return "Authentication tag mismatch";
    case CryptoError::kRngFailed:
      return "Secure RNG failed";
    default:
      return "Unknown crypto error";
  }
}

std::expected<void, CryptoError> GenerateGcmIv(std::span<uint8_t> iv_buf);

std::expected<void, CryptoError> AesGcmEncrypt(
    std::span<const uint8_t> key, std::span<const uint8_t> iv,
    std::span<const uint8_t> aad, std::span<const uint8_t> plaintext,
    std::span<uint8_t> ciphertext, std::span<uint8_t> tag);

std::expected<void, CryptoError> AesGcmDecrypt(
    std::span<const uint8_t> key, std::span<const uint8_t> iv,
    std::span<const uint8_t> aad, std::span<const uint8_t> ciphertext,
    std::span<const uint8_t> tag, std::span<uint8_t> plaintext);

// Helpers to completely isolate OpenSSL dependencies
void Sha256(std::span<const uint8_t> data, std::span<uint8_t> hash_out);
void Cleanse(std::span<uint8_t> data);

}  // namespace egis::crypto

#endif  // UTIL_EGIS_CRYPTO_UTIL_H_