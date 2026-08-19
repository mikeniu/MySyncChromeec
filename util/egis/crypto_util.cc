/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto_util.h"

#include <cstdlib>

#include <openssl/aead.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace egis::crypto {

std::expected<std::array<uint8_t, kAesGcmIvSize>, CryptoError> GenerateGcmIv() {
  std::array<uint8_t, kAesGcmIvSize> iv;
  if (RAND_bytes(iv.data(), iv.size()) != 1) {
    return std::unexpected(CryptoError::kRngFailed);
  }
  return iv;
}

std::expected<void, CryptoError> AesGcmEncrypt(
    std::span<const uint8_t, kAes256KeySize> key,
    std::span<const uint8_t, kAesGcmIvSize> iv, std::span<const uint8_t> aad,
    std::span<const uint8_t> plaintext, std::span<uint8_t> ciphertext,
    std::span<uint8_t, kAesGcmTagSize> tag) {
  bssl::ScopedEVP_AEAD_CTX ctx;
  if (!EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_256_gcm(), key.data(),
                         key.size(), tag.size(), nullptr)) {
    return std::unexpected(CryptoError::kInitFailed);
  }

  size_t out_tag_len = 0;

  if (!EVP_AEAD_CTX_seal_scatter(
          ctx.get(), ciphertext.data(), tag.data(), &out_tag_len, tag.size(),
          iv.data(), iv.size(), plaintext.data(), plaintext.size(),
          /*extra_in=*/nullptr, 0, aad.data(), aad.size())) {
    return std::unexpected(CryptoError::kUpdateFailed);
  }

  return {};
}

std::expected<void, CryptoError> AesGcmDecrypt(
    std::span<const uint8_t, kAes256KeySize> key,
    std::span<const uint8_t, kAesGcmIvSize> iv, std::span<const uint8_t> aad,
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t, kAesGcmTagSize> tag,
    std::span<uint8_t> plaintext) {
  bssl::ScopedEVP_AEAD_CTX ctx;
  if (!EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_256_gcm(), key.data(),
                         key.size(), tag.size(), nullptr)) {
    return std::unexpected(CryptoError::kInitFailed);
  }

  if (!EVP_AEAD_CTX_open_gather(
          ctx.get(), plaintext.data(), iv.data(), iv.size(), ciphertext.data(),
          ciphertext.size(), tag.data(), tag.size(), aad.data(), aad.size())) {
    return std::unexpected(CryptoError::kTagMismatch);
  }

  return {};
}

std::array<uint8_t, kSha256DigestSize> Sha256(std::span<const uint8_t> data) {
  std::array<uint8_t, kSha256DigestSize> hash{};
  ::SHA256(data.data(), data.size(), hash.data());
  return hash;
}

void Cleanse(std::span<uint8_t> data) {
  OPENSSL_cleanse(data.data(), data.size());
}

}  // namespace egis::crypto
