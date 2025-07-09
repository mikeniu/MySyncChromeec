// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ffi.h"

#include <android-base/logging.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/cipher.h>
#include <openssl/ecdsa.h>
#include <openssl/hmac.h>
#include <openssl/mem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include "hmac_authorization_delegate.h"
#include "multiple_authorization_delegate.h"
#include "password_authorization_delegate.h"

namespace trunks {

namespace {

constexpr TPMA_OBJECT kFixedTPM = 1U << 1;
constexpr TPMA_OBJECT kFixedParent = 1U << 4;
constexpr TPMA_OBJECT kSensitiveDataOrigin = 1U << 5;
constexpr TPMA_OBJECT kUserWithAuth = 1U << 6;
constexpr TPMA_OBJECT kAdminWithPolicy = 1U << 7;
constexpr TPMA_OBJECT kNoDA = 1U << 10;
constexpr TPMA_OBJECT kRestricted = 1U << 16;
constexpr TPMA_OBJECT kDecrypt = 1U << 17;
constexpr TPMA_OBJECT kSign = 1U << 18;

// Auth policy used in RSA and ECC templates for EK keys generation.
// From TCG Credential Profile EK 2.0. Section 2.1.5.
constexpr char kEKTemplateAuthPolicy[] = {
    '\x83', '\x71', '\x97', '\x67', '\x44', '\x84', '\xB3', '\xF8',
    '\x1A', '\x90', '\xCC', '\x8D', '\x46', '\xA5', '\xD7', '\x24',
    '\xFD', '\x52', '\xD7', '\x6E', '\x06', '\x52', '\x0B', '\x64',
    '\xF2', '\xA1', '\xDA', '\x1B', '\x33', '\x14', '\x69', '\xAA',
};

// Returns a general public area for our keys. This default may be further
// manipulated to produce the public area for specific keys (such as SRK or
// AIK).
TPMT_PUBLIC DefaultPublicArea() {
  TPMT_PUBLIC public_area;
  memset(&public_area, 0, sizeof(public_area));
  public_area.type = TPM_ALG_ECC;
  public_area.name_alg = TPM_ALG_SHA256;
  public_area.auth_policy = Make_TPM2B_DIGEST("");
  public_area.object_attributes = kFixedTPM | kFixedParent;
  public_area.parameters.ecc_detail.scheme.scheme = TPM_ALG_NULL;
  public_area.parameters.ecc_detail.symmetric.algorithm = TPM_ALG_NULL;
  public_area.parameters.ecc_detail.curve_id = TPM_ECC_NIST_P256;
  public_area.parameters.ecc_detail.kdf.scheme = TPM_ALG_NULL;
  public_area.unique.ecc.x = Make_TPM2B_ECC_PARAMETER("");
  public_area.unique.ecc.y = Make_TPM2B_ECC_PARAMETER("");
  return public_area;
}

std::string GetOpenSSLError() {
  BIO* bio = BIO_new(BIO_s_mem());
  ERR_print_errors(bio);
  char* data = nullptr;
  int data_len = BIO_get_mem_data(bio, &data);
  std::string error_string(data, data_len);
  BIO_free(bio);
  return error_string;
}

unsigned char* StringAsOpenSSLBuffer(std::string* s) {
  return reinterpret_cast<unsigned char*>(std::data(*s));
}

// Converts a TPMT_SIGNATURE into a DER-encoded ECDSA signature.
TPM_RC TpmSignatureToString(TPMT_SIGNATURE signature, std::string* encoded) {
  std::string r =
      StringFrom_TPM2B_ECC_PARAMETER(signature.signature.ecdsa.signature_r);
  std::string s =
      StringFrom_TPM2B_ECC_PARAMETER(signature.signature.ecdsa.signature_s);
  BIGNUM* r_bn =
      BN_bin2bn(reinterpret_cast<const uint8_t*>(r.data()), r.length(), NULL);
  BIGNUM* s_bn =
      BN_bin2bn(reinterpret_cast<const uint8_t*>(s.data()), s.length(), NULL);
  ECDSA_SIG* sig = ECDSA_SIG_new();
  if (r_bn == NULL || s_bn == NULL || sig == NULL) {
    LOG(ERROR) << "BoringSSL allocation failure";
    return TPM_RC_FAILURE;
  }
  // Note: if successful, this transfers ownership of r_bn and s_bin to sig.
  if (ECDSA_SIG_set0(sig, r_bn, s_bn) != 1) {
    LOG(ERROR) << "ECDSA_SIG_set0 failed";
    ECDSA_SIG_free(sig);
    BN_free(r_bn);
    BN_free(s_bn);
    return TPM_RC_FAILURE;
  }
  unsigned char* openssl_buffer = nullptr;
  int size = i2d_ECDSA_SIG(sig, &openssl_buffer);
  ECDSA_SIG_free(sig);
  if (size < 0 || openssl_buffer == nullptr) {
    LOG(ERROR) << "i2d_ECDSA_SIG failed";
    return TPM_RC_FAILURE;
  }
  encoded->assign(reinterpret_cast<const char*>(openssl_buffer), size);
  OPENSSL_free(openssl_buffer);
  return TPM_RC_SUCCESS;
}

}  // namespace

bool EncryptDataForCa(const std::string& data,
                      const std::string& public_key_hex,
                      const std::string& key_id, std::string& wrapped_key,
                      std::string& iv, std::string& mac,
                      std::string& encrypted_data,
                      std::string& wrapping_key_id) {
  const size_t kAesKeySize = 32;
  const size_t kAesBlockSize = 16;
  // The exponent of the attestation CA key pairs.
  const unsigned int kWellKnownExponent = 65537;
  RSA* rsa = nullptr;
  BIGNUM* e = nullptr;
  BIGNUM* n = nullptr;
  EVP_CIPHER_CTX* encryption_context = nullptr;
  // This lambda returns early in case of error. The values it allocates are
  // cleaned up after it returns regardless of outcome.
  bool out = [&]() {
    rsa = RSA_new();
    e = BN_new();
    n = BN_new();
    if (!rsa || !e || !n) {
      LOG(ERROR) << "Failed to allocate RSA or BIGNUMs";
      return false;
    }
    if (!BN_set_word(e, kWellKnownExponent)) {
      LOG(ERROR) << "Failed to generate exponent";
      return false;
    }
    if (!BN_hex2bn(&n, public_key_hex.c_str())) {
      LOG(ERROR) << "Failed to generate modulus";
      return false;
    }
    if (!RSA_set0_key(rsa, n, e, nullptr)) {
      LOG(ERROR) << "Failed to set exponent or modulus";
      return false;
    }
    // RSA_set0_key succeeded, so ownership of n and e are transferred into rsa.
    // Reset e and n to avoid double-BN_free.
    e = nullptr;
    n = nullptr;
    std::string key;
    key.resize(kAesKeySize);
    if (RAND_bytes(StringAsOpenSSLBuffer(&key), kAesKeySize) != 1) {
      LOG(ERROR) << "RAND_bytes for key failed";
      return false;
    }
    iv.resize(kAesBlockSize);
    if (RAND_bytes(StringAsOpenSSLBuffer(&iv), kAesBlockSize) != 1) {
      LOG(ERROR) << "RAND_bytes for iv failed";
      return false;
    }
    // Allocate enough space for the output including padding.
    encrypted_data.resize(data.size() + kAesBlockSize -
                          (data.size() % kAesBlockSize));
    encryption_context = EVP_CIPHER_CTX_new();
    if (!encryption_context) {
      LOG(ERROR) << "Failed to allocate EVP_CIPHER_CTX: " << GetOpenSSLError();
      return false;
    }
    if (!EVP_EncryptInit_ex(encryption_context, EVP_aes_256_cbc(), nullptr,
                            StringAsOpenSSLBuffer(&key),
                            StringAsOpenSSLBuffer(&iv))) {
      LOG(ERROR) << "EVP_EncryptInit_ex failed: " << GetOpenSSLError();
      return false;
    }
    unsigned char* output_buffer = StringAsOpenSSLBuffer(&encrypted_data);
    int update_size = 0;
    const uint8_t* input_buffer =
        reinterpret_cast<const uint8_t*>(std::data(data));
    if (!EVP_EncryptUpdate(encryption_context, output_buffer, &update_size,
                           input_buffer, data.size())) {
      LOG(ERROR) << "EVP_EncryptUpdate failed: " << GetOpenSSLError();
      return false;
    }
    output_buffer += update_size;
    int final_size = 0;
    if (!EVP_EncryptFinal_ex(encryption_context, output_buffer, &final_size)) {
      LOG(ERROR) << "EVP_EncryptFinal_ex failed: " << GetOpenSSLError();
      return false;
    }
    encrypted_data.resize(update_size + final_size);
    mac.resize(SHA512_DIGEST_LENGTH);
    std::string hmac_data = iv + encrypted_data;
    HMAC(EVP_sha512(), key.data(), key.size(),
         StringAsOpenSSLBuffer(&hmac_data), hmac_data.size(),
         StringAsOpenSSLBuffer(&mac), nullptr);
    wrapped_key.resize(RSA_size(rsa));
    int length = RSA_public_encrypt(
        key.size(), reinterpret_cast<const unsigned char*>(key.data()),
        StringAsOpenSSLBuffer(&wrapped_key), rsa, RSA_PKCS1_OAEP_PADDING);
    if (length < 0) {
      LOG(ERROR) << "RSA_public_encrypt failed: " << GetOpenSSLError();
      return false;
    }
    wrapping_key_id = key_id;
    return true;
  }();
  if (rsa) {
    RSA_free(rsa);
  }
  if (e) {
    BN_free(e);
  }
  if (n) {
    BN_free(n);
  }
  if (encryption_context) {
    EVP_CIPHER_CTX_free(encryption_context);
  }
  return out;
}

std::unique_ptr<AuthorizationDelegate> HmacAuthorizationDelegate_New(
    TPM_HANDLE session_handle, const std::string& tpm_nonce,
    const std::string& caller_nonce, const std::string& salt,
    const std::string& bind_auth_value, bool enable_parameter_encryption) {
  std::unique_ptr<HmacAuthorizationDelegate> delegate =
      std::make_unique<HmacAuthorizationDelegate>();
  if (!delegate->InitSession(session_handle, Make_TPM2B_DIGEST(tpm_nonce),
                             Make_TPM2B_DIGEST(caller_nonce), salt,
                             bind_auth_value, enable_parameter_encryption)) {
    LOG(ERROR) << "HmacAuthorizationDelegate::InitSession failed";
    return nullptr;
  }
  return delegate;
}

std::unique_ptr<AuthorizationDelegate> PasswordAuthorizationDelegate_New(
    const std::string& password) {
  return std::make_unique<PasswordAuthorizationDelegate>(password);
}

TPM_RC SerializeCommand_ActivateCredential(
    const TPMI_DH_OBJECT& activate_handle,
    const std::string& activate_handle_name, const TPMI_DH_OBJECT& key_handle,
    const std::string& key_handle_name, const std::string& credential_mac,
    const std::string& wrapped_key, const std::string& secret,
    std::string& serialized_command, AuthorizationDelegate& key_authorization) {
  std::string credential_blob;
  TPM_RC rc = Serialize_TPM2B_DIGEST(Make_TPM2B_DIGEST(credential_mac),
                                     &credential_blob);
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  credential_blob += wrapped_key;
  MultipleAuthorizations authorizations;
  PasswordAuthorizationDelegate password("");
  authorizations.AddAuthorizationDelegate(&password);
  authorizations.AddAuthorizationDelegate(&key_authorization);
  return Tpm::SerializeCommand_ActivateCredential(
      activate_handle, activate_handle_name, key_handle, key_handle_name,
      Make_TPM2B_ID_OBJECT(credential_blob),
      Make_TPM2B_ENCRYPTED_SECRET(secret), &serialized_command,
      &authorizations);
}

TPM_RC ParseResponse_ActivateCredential(
    const std::string& response, std::string& cert_info,
    AuthorizationDelegate& key_authorization) {
  TPM2B_DIGEST typed_cert_info;
  MultipleAuthorizations authorizations;
  PasswordAuthorizationDelegate password("");
  authorizations.AddAuthorizationDelegate(&password);
  authorizations.AddAuthorizationDelegate(&key_authorization);
  TPM_RC rc = Tpm::ParseResponse_ActivateCredential(response, &typed_cert_info,
                                                    &authorizations);
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  cert_info = StringFrom_TPM2B_DIGEST(typed_cert_info);
  return TPM_RC_SUCCESS;
}

TPM_RC SerializeCommand_Create(
    const TPMI_DH_OBJECT& parent_handle, const std::string& parent_handle_name,
    const TPM2B_SENSITIVE_CREATE& in_sensitive, const TPM2B_PUBLIC& in_public,
    const TPM2B_DATA& outside_info, const TPML_PCR_SELECTION& creation_pcr,
    std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::SerializeCommand_Create(
      parent_handle, parent_handle_name, in_sensitive, in_public, outside_info,
      creation_pcr, &serialized_command, authorization_delegate.get());
}

TPM_RC ParseResponse_Create(
    const std::string& response, std::string& out_private,
    std::string& out_public, TPM2B_CREATION_DATA& creation_data,
    TPM2B_DIGEST& creation_hash, TPMT_TK_CREATION& creation_ticket,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPM2B_PRIVATE typed_private;
  TPM2B_PUBLIC typed_public;
  TPM_RC rc = Tpm::ParseResponse_Create(
      response, &typed_private, &typed_public, &creation_data, &creation_hash,
      &creation_ticket, authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  out_private = StringFrom_TPM2B_PRIVATE(typed_private);
  return Serialize_TPM2B_PUBLIC(typed_public, &out_public);
}

TPM_RC SerializeCommand_CreatePrimary(
    const TPMI_RH_HIERARCHY& primary_handle,
    const std::string& primary_handle_name,
    const TPM2B_SENSITIVE_CREATE& in_sensitive, const TPM2B_PUBLIC& in_public,
    const TPM2B_DATA& outside_info, const TPML_PCR_SELECTION& creation_pcr,
    std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::SerializeCommand_CreatePrimary(
      primary_handle, primary_handle_name, in_sensitive, in_public,
      outside_info, creation_pcr, &serialized_command,
      authorization_delegate.get());
}

TPM_RC ParseResponse_CreatePrimary(
    const std::string& response, TPM_HANDLE& object_handle,
    TPM2B_PUBLIC& out_public, TPM2B_CREATION_DATA& creation_data,
    TPM2B_DIGEST& creation_hash, TPMT_TK_CREATION& creation_ticket,
    std::string& name,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPM2B_NAME tpm2b_name;
  TPM_RC rc = Tpm::ParseResponse_CreatePrimary(
      response, &object_handle, &out_public, &creation_data, &creation_hash,
      &creation_ticket, &tpm2b_name, authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  name = StringFrom_TPM2B_NAME(tpm2b_name);
  return TPM_RC_SUCCESS;
}

TPM_RC SerializeCommand_Load(
    const TPMI_DH_OBJECT& parent_handle, const std::string& parent_handle_name,
    const std::string& in_private, const std::string& in_public,
    std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  std::string buffer_public = in_public;
  TPM2B_PUBLIC typed_public;
  TPM_RC rc = Parse_TPM2B_PUBLIC(&buffer_public, &typed_public, nullptr);
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  return Tpm::SerializeCommand_Load(
      parent_handle, parent_handle_name, Make_TPM2B_PRIVATE(in_private),
      typed_public, &serialized_command, authorization_delegate.get());
}

TPM_RC ParseResponse_Load(
    const std::string& response, TPM_HANDLE& object_handle, std::string& name,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPM2B_NAME typed_name;
  TPM_RC rc = Tpm::ParseResponse_Load(response, &object_handle, &typed_name,
                                      authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  name = StringFrom_TPM2B_NAME(typed_name);
  return TPM_RC_SUCCESS;
}

TPM_RC SerializeCommand_NV_Certify(
    const TPMI_DH_OBJECT& sign_handle, const std::string& sign_handle_name,
    const TPMI_RH_NV_AUTH& auth_handle, const std::string& auth_handle_name,
    const TPMI_RH_NV_INDEX& nv_index, const std::string& nv_index_name,
    const TPM2B_DATA& qualifying_data, const TPMT_SIG_SCHEME& in_scheme,
    const UINT16& size, const UINT16& offset, std::string& serialized_command) {
  PasswordAuthorizationDelegate password("");
  MultipleAuthorizations authorizations;
  authorizations.AddAuthorizationDelegate(&password);
  authorizations.AddAuthorizationDelegate(&password);
  return Tpm::SerializeCommand_NV_Certify(
      sign_handle, sign_handle_name, auth_handle, auth_handle_name, nv_index,
      nv_index_name, qualifying_data, in_scheme, size, offset,
      &serialized_command, &authorizations);
}

TPM_RC ParseResponse_NV_Certify(const std::string& response,
                                std::string& certify_info,
                                std::string& signature) {
  TPM2B_ATTEST certify_info_typed;
  TPMT_SIGNATURE signature_typed;
  PasswordAuthorizationDelegate password("");
  MultipleAuthorizations authorizations;
  authorizations.AddAuthorizationDelegate(&password);
  authorizations.AddAuthorizationDelegate(&password);
  TPM_RC rc = Tpm::ParseResponse_NV_Certify(response, &certify_info_typed,
                                            &signature_typed, &authorizations);
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  certify_info = StringFrom_TPM2B_ATTEST(certify_info_typed);
  return TpmSignatureToString(signature_typed, &signature);
}

TPM_RC SerializeCommand_NV_Read(
    const TPMI_RH_NV_AUTH& auth_handle, const std::string& auth_handle_name,
    const TPMI_RH_NV_INDEX& nv_index, const std::string& nv_index_name,
    const UINT16& size, const UINT16& offset, std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::SerializeCommand_NV_Read(
      auth_handle, auth_handle_name, nv_index, nv_index_name, size, offset,
      &serialized_command, authorization_delegate.get());
}

TPM_RC ParseResponse_NV_Read(
    const std::string& response, std::string& data,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPM2B_MAX_NV_BUFFER buffer;
  TPM_RC rc = Tpm::ParseResponse_NV_Read(response, &buffer,
                                         authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  data = StringFrom_TPM2B_MAX_NV_BUFFER(buffer);
  return TPM_RC_SUCCESS;
}

TPM_RC SerializeCommand_NV_ReadPublic(
    const TPMI_RH_NV_INDEX& nv_index, const std::string& nv_index_name,
    std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::SerializeCommand_NV_ReadPublic(nv_index, nv_index_name,
                                             &serialized_command,
                                             authorization_delegate.get());
}

TPM_RC ParseResponse_NV_ReadPublic(
    const std::string& response, uint16_t& nv_public_data_size,
    std::string& nv_name,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPM2B_NV_PUBLIC nv_public;
  TPM2B_NAME nv_name_typed;
  TPM_RC rc = Tpm::ParseResponse_NV_ReadPublic(
      response, &nv_public, &nv_name_typed, authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  nv_public_data_size = nv_public.nv_public.data_size;
  nv_name = StringFrom_TPM2B_NAME(nv_name_typed);
  return TPM_RC_SUCCESS;
}

TPM_RC SerializeCommand_PolicySecret(
    const TPMI_DH_ENTITY& auth_handle, const std::string& auth_handle_name,
    const TPMI_SH_POLICY& policy_session,
    const std::string& policy_session_name, const std::string& nonce_tpm,
    const std::string& cp_hash_a, const std::string& policy_ref,
    const uint32_t& expiration, std::string& serialized_command,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::SerializeCommand_PolicySecret(
      auth_handle, auth_handle_name, policy_session, policy_session_name,
      Make_TPM2B_DIGEST(nonce_tpm), Make_TPM2B_DIGEST(cp_hash_a),
      Make_TPM2B_DIGEST(policy_ref), expiration, &serialized_command,
      authorization_delegate.get());
}

TPM_RC ParseResponse_PolicySecret(
    const std::string& response, std::string& timeout,
    uint16_t& policy_ticket_tag, uint32_t& policy_ticket_hierarchy,
    std::string& policy_ticket_digest,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPM2B_TIMEOUT typed_timeout;
  TPMT_TK_AUTH policy_ticket;
  TPM_RC rc = Tpm::ParseResponse_PolicySecret(
      response, &typed_timeout, &policy_ticket, authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  timeout = StringFrom_TPM2B_TIMEOUT(typed_timeout);
  policy_ticket_tag = policy_ticket.tag;
  policy_ticket_hierarchy = policy_ticket.hierarchy;
  policy_ticket_digest = StringFrom_TPM2B_DIGEST(policy_ticket.digest);
  return TPM_RC_SUCCESS;
}

TPM_RC SerializeCommand_Quote(
    const TPMI_DH_OBJECT& sign_handle, const std::string& sign_handle_name,
    const TPM2B_DATA& qualifying_data, const TPMT_SIG_SCHEME& in_scheme,
    const TPML_PCR_SELECTION& pcrselect, std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::SerializeCommand_Quote(
      sign_handle, sign_handle_name, qualifying_data, in_scheme, pcrselect,
      &serialized_command, authorization_delegate.get());
}

TPM_RC ParseResponse_Quote(
    const std::string& response, std::string& quoted, std::string& signature,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPM2B_ATTEST quoted_typed;
  TPMT_SIGNATURE signature_typed;
  TPM_RC rc = Tpm::ParseResponse_Quote(
      response, &quoted_typed, &signature_typed, authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  quoted = StringFrom_TPM2B_ATTEST(quoted_typed);
  return TpmSignatureToString(signature_typed, &signature);
}

TPM_RC SerializeCommand_PCR_Read(
    const TPML_PCR_SELECTION& pcr_selection_in, std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::SerializeCommand_PCR_Read(pcr_selection_in, &serialized_command,
                                        authorization_delegate.get());
}

TPM_RC ParseResponse_PCR_Read(
    const std::string& response, UINT32& pcr_update_counter,
    TPML_PCR_SELECTION& pcr_selection_out, std::string& pcr_values,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPML_DIGEST pcr_values_typed;
  TPM_RC rc = Tpm::ParseResponse_PCR_Read(response, &pcr_update_counter,
                                          &pcr_selection_out, &pcr_values_typed,
                                          authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  if (pcr_values_typed.count != 1) {
    LOG(ERROR) << "Unexpected PCR count " << pcr_values_typed.count
               << " in TPM2_PCR_Read reply.";
    return TPM_RC_FAILURE;
  }
  pcr_values = StringFrom_TPM2B_DIGEST(pcr_values_typed.digests[0]);
  return TPM_RC_SUCCESS;
}

TPM_RC SerializeCommand_StartAuthSession(
    const TPMI_DH_OBJECT& tpm_key, const std::string& tpm_key_name,
    const TPMI_DH_ENTITY& bind, const std::string& bind_name,
    const std::string& nonce_caller, const std::string& encrypted_salt,
    const uint8_t& session_type, const uint16_t& auth_hash,
    std::string& serialized_command,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPMT_SYM_DEF symmetric;
  symmetric.algorithm = TPM_ALG_NULL;
  return Tpm::SerializeCommand_StartAuthSession(
      tpm_key, tpm_key_name, bind, bind_name, Make_TPM2B_DIGEST(nonce_caller),
      Make_TPM2B_ENCRYPTED_SECRET(encrypted_salt), session_type, symmetric,
      auth_hash, &serialized_command, authorization_delegate.get());
}

TPM_RC ParseResponse_StartAuthSession(
    const std::string& response, TPMI_SH_AUTH_SESSION& session_handle,
    std::string& nonce_tpm,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  TPM2B_NONCE nonce;
  TPM_RC rc = Tpm::ParseResponse_StartAuthSession(
      response, &session_handle, &nonce, authorization_delegate.get());
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  nonce_tpm = StringFrom_TPM2B_DIGEST(nonce);
  return TPM_RC_SUCCESS;
}

TPM_RC SerializeCommand_FlushContext(
    const TPMI_DH_OBJECT& activate_handle,
    std::string& serialized_command,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::SerializeCommand_FlushContext(
      activate_handle, &serialized_command, authorization_delegate.get());
}

TPM_RC ParseResponse_FlushContext(
    const std::string& response,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate) {
  return Tpm::ParseResponse_FlushContext(
      response, authorization_delegate.get());
}

std::unique_ptr<std::string> NameFromHandle(const TPM_HANDLE& handle) {
  std::string name;
  Serialize_TPM_HANDLE(handle, &name);
  return std::make_unique<std::string>(std::move(name));
}

std::unique_ptr<TPM2B_CREATION_DATA> TPM2B_CREATION_DATA_New() {
  return std::make_unique<TPM2B_CREATION_DATA>();
}

std::unique_ptr<TPM2B_DATA> TPM2B_DATA_New(const std::string& bytes) {
  return std::make_unique<TPM2B_DATA>(Make_TPM2B_DATA(bytes));
}

std::unique_ptr<TPM2B_DIGEST> TPM2B_DIGEST_New() {
  return std::make_unique<TPM2B_DIGEST>();
}

std::unique_ptr<TPM2B_PUBLIC> AttestationIdentityKeyTemplate() {
  TPMT_PUBLIC public_area = DefaultPublicArea();
  public_area.object_attributes |=
      (kSensitiveDataOrigin | kUserWithAuth | kNoDA | kRestricted | kSign);
  public_area.parameters.ecc_detail.scheme.scheme = TPM_ALG_ECDSA;
  public_area.parameters.ecc_detail.scheme.details.ecdsa.hash_alg =
      TPM_ALG_SHA256;
  return std::make_unique<TPM2B_PUBLIC>(Make_TPM2B_PUBLIC(public_area));
}

std::unique_ptr<TPM2B_PUBLIC> EndorsementKeyTemplate() {
  TPMT_PUBLIC public_area = DefaultPublicArea();
  public_area.object_attributes = kFixedTPM | kFixedParent |
                                  kSensitiveDataOrigin | kAdminWithPolicy |
                                  kRestricted | kDecrypt;
  public_area.auth_policy = Make_TPM2B_DIGEST(
      std::string(kEKTemplateAuthPolicy, std::size(kEKTemplateAuthPolicy)));
  public_area.parameters.ecc_detail.symmetric.algorithm = TPM_ALG_AES;
  public_area.parameters.ecc_detail.symmetric.key_bits.aes = 128;
  public_area.parameters.ecc_detail.symmetric.mode.aes = TPM_ALG_CFB;
  public_area.parameters.ecc_detail.scheme.scheme = TPM_ALG_NULL;
  public_area.parameters.ecc_detail.curve_id = TPM_ECC_NIST_P256;
  public_area.parameters.ecc_detail.kdf.scheme = TPM_ALG_NULL;
  public_area.unique.ecc.x = Make_TPM2B_ECC_PARAMETER(std::string(32, 0));
  public_area.unique.ecc.y = Make_TPM2B_ECC_PARAMETER(std::string(32, 0));
  return std::make_unique<TPM2B_PUBLIC>(Make_TPM2B_PUBLIC(public_area));
}

std::unique_ptr<TPM2B_PUBLIC> StorageRootKeyTemplate() {
  TPMT_PUBLIC public_area = DefaultPublicArea();
  public_area.object_attributes |=
      (kSensitiveDataOrigin | kUserWithAuth | kNoDA | kRestricted | kDecrypt);
  public_area.parameters.asym_detail.symmetric.algorithm = TPM_ALG_AES;
  public_area.parameters.asym_detail.symmetric.key_bits.aes = 128;
  public_area.parameters.asym_detail.symmetric.mode.aes = TPM_ALG_CFB;
  return std::make_unique<TPM2B_PUBLIC>(Make_TPM2B_PUBLIC(public_area));
}

TPM_RC Tpm2bPublicToTpmtPublic(const std::string& tpm2b_public,
                               std::string& tpmt_public) {
  std::string buffer_public = tpm2b_public;
  TPM2B_PUBLIC typed_public;
  TPM_RC rc = Parse_TPM2B_PUBLIC(&buffer_public, &typed_public, nullptr);
  if (rc != TPM_RC_SUCCESS) {
    return rc;
  }
  return Serialize_TPMT_PUBLIC(typed_public.public_area, &tpmt_public);
}

std::unique_ptr<TPM2B_SENSITIVE_CREATE> TPM2B_SENSITIVE_CREATE_New(
    const std::string& user_auth, const std::string& data) {
  TPMS_SENSITIVE_CREATE sensitive;
  sensitive.user_auth = Make_TPM2B_DIGEST(user_auth);
  sensitive.data = Make_TPM2B_SENSITIVE_DATA(data);
  return std::make_unique<TPM2B_SENSITIVE_CREATE>(
      Make_TPM2B_SENSITIVE_CREATE(sensitive));
}

std::unique_ptr<TPML_PCR_SELECTION> EmptyPcrSelection() {
  TPML_PCR_SELECTION creation_pcrs = {};
  creation_pcrs.count = 0;
  return std::make_unique<TPML_PCR_SELECTION>(creation_pcrs);
}

std::unique_ptr<TPML_PCR_SELECTION> SinglePcrSelection(uint8_t pcr) {
  TPML_PCR_SELECTION pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = TPM_ALG_SHA256;
  pcr_select.pcr_selections[0].sizeof_select = PCR_SELECT_MIN;
  memset(pcr_select.pcr_selections[0].pcr_select, 0, PCR_SELECT_MIN);
  if (pcr / 8 >= PCR_SELECT_MIN) {
    LOG(ERROR) << "Invalid PCR number " << pcr;
    return nullptr;
  }
  pcr_select.pcr_selections[0].pcr_select[pcr / 8] = 1u << (pcr % 8);
  return std::make_unique<TPML_PCR_SELECTION>(pcr_select);
}

std::unique_ptr<TPMT_SIG_SCHEME> Sha256EcdsaSigScheme() {
  TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = TPM_ALG_SHA256;
  scheme.scheme = TPM_ALG_ECDSA;
  return std::make_unique<TPMT_SIG_SCHEME>(scheme);
}

std::unique_ptr<TPMT_TK_CREATION> TPMT_TK_CREATION_New() {
  return std::make_unique<TPMT_TK_CREATION>();
}

}  // namespace trunks
