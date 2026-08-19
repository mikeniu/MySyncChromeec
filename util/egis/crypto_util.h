/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_CRYPTO_UTIL_H_
#define UTIL_EGIS_CRYPTO_UTIL_H_

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace egis::crypto {

inline constexpr size_t kAes256KeySize = 32;
inline constexpr size_t kAesGcmIvSize = 12;
inline constexpr size_t kAesGcmTagSize = 16;
inline constexpr size_t kSha256DigestSize = 32;

enum class CryptoError {
  kContextAllocationFailed,
  kInitFailed,
  kUpdateFailed,
  kFinalizeFailed,
  kTagMismatch,
  kRngFailed,
  kInvalidParam,
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
    case CryptoError::kInvalidParam:
      return "Invalid cryptographic parameters or span sizes";
  }
  return "Unknown crypto error";
}

std::expected<std::array<uint8_t, kAesGcmIvSize>, CryptoError> GenerateGcmIv();

std::expected<void, CryptoError> AesGcmEncrypt(
    std::span<const uint8_t, kAes256KeySize> key,
    std::span<const uint8_t, kAesGcmIvSize> iv, std::span<const uint8_t> aad,
    std::span<const uint8_t> plaintext, std::span<uint8_t> ciphertext,
    std::span<uint8_t, kAesGcmTagSize> tag);

std::expected<void, CryptoError> AesGcmDecrypt(
    std::span<const uint8_t, kAes256KeySize> key,
    std::span<const uint8_t, kAesGcmIvSize> iv, std::span<const uint8_t> aad,
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t, kAesGcmTagSize> tag, std::span<uint8_t> plaintext);

// Helpers to completely isolate boringssl dependencies
std::array<uint8_t, kSha256DigestSize> Sha256(std::span<const uint8_t> data);
void Cleanse(std::span<uint8_t> data);

}  // namespace egis::crypto

#endif  // UTIL_EGIS_CRYPTO_UTIL_H_
