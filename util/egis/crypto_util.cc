/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto_util.h"

#include <cstdlib>
#include <memory>

#include <openssl/aes.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace egis::crypto {

using EvpCipherCtxPtr =
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;

std::expected<void, CryptoError> GenerateGcmIv(
    std::span<uint8_t, kAesGcmIvSize> iv_buf) {
  if (RAND_bytes(iv_buf.data(), iv_buf.size()) != 1) {
    return std::unexpected(CryptoError::kRngFailed);
  }
  return {};
}

std::expected<void, CryptoError> AesGcmEncrypt(
    std::span<const uint8_t, kAes256KeySize> key,
    std::span<const uint8_t, kAesGcmIvSize> iv, std::span<const uint8_t> aad,
    std::span<const uint8_t> plaintext, std::span<uint8_t> ciphertext,
    std::span<uint8_t, kAesGcmTagSize> tag) {
  EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &::EVP_CIPHER_CTX_free);
  if (!ctx) return std::unexpected(CryptoError::kContextAllocationFailed);

  int len;

  if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv.size(), NULL) !=
          1 ||
      EVP_EncryptInit_ex(ctx.get(), NULL, NULL, key.data(), iv.data()) != 1) {
    return std::unexpected(CryptoError::kInitFailed);
  }

  if (!aad.empty()) {
    if (EVP_EncryptUpdate(ctx.get(), NULL, &len, aad.data(), aad.size()) != 1) {
      return std::unexpected(CryptoError::kUpdateFailed);
    }
  }

  if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &len, plaintext.data(),
                        plaintext.size()) != 1) {
    return std::unexpected(CryptoError::kUpdateFailed);
  }

  if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + len, &len) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, tag.size(),
                          tag.data()) != 1) {
    return std::unexpected(CryptoError::kFinalizeFailed);
  }

  return {};
}

std::expected<void, CryptoError> AesGcmDecrypt(
    std::span<const uint8_t, kAes256KeySize> key,
    std::span<const uint8_t, kAesGcmIvSize> iv, std::span<const uint8_t> aad,
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t, kAesGcmTagSize> tag,
    std::span<uint8_t> plaintext) {
  EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &::EVP_CIPHER_CTX_free);
  if (!ctx) return std::unexpected(CryptoError::kContextAllocationFailed);

  int len;

  if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv.size(), NULL) !=
          1 ||
      EVP_DecryptInit_ex(ctx.get(), NULL, NULL, key.data(), iv.data()) != 1) {
    return std::unexpected(CryptoError::kInitFailed);
  }

  if (!aad.empty()) {
    if (EVP_DecryptUpdate(ctx.get(), NULL, &len, aad.data(), aad.size()) != 1) {
      return std::unexpected(CryptoError::kUpdateFailed);
    }
  }

  if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, ciphertext.data(),
                        ciphertext.size()) != 1) {
    return std::unexpected(CryptoError::kUpdateFailed);
  }

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag.size(),
                          const_cast<uint8_t *>(tag.data())) != 1) {
    return std::unexpected(CryptoError::kFinalizeFailed);
  }

  if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len) <= 0) {
    return std::unexpected(CryptoError::kTagMismatch);
  }

  return {};
}

void Sha256(std::span<const uint8_t> data, std::span<uint8_t> hash_out) {
  if (hash_out.size() < SHA256_DIGEST_LENGTH) {
    // This is a programming error (a contract violation) and should not fail
    // silently.
    std::abort();
  }
  ::SHA256(data.data(), data.size(), hash_out.data());
}

void Cleanse(std::span<uint8_t> data) {
  OPENSSL_cleanse(data.data(), data.size());
}

}  // namespace egis::crypto
