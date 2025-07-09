// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_GENERATED_FFI_H_
#define TPM_GENERATED_FFI_H_

// The cxx Rust library cannot invoke all C++ methods -- for example, it cannot
// invoke static methods, and there are many types it cannot pass by value.
// These functions provide cxx-compatible access to the functionality of other
// code in this directory.
//
// Because this entire library (libtpmgenerated) is a temporary measure
// (long-term, we want to replace it with tpm-rs), these bindings are added
// as-needed and do not always expose all the functionality that they could.

#include <memory>
#include <string>

#include "authorization_delegate.h"
#include "tpm_generated.h"

namespace trunks {

// Organization: each subsection is a type, ordered alphabetically.

// -----------------------------------------------------------------------------
// EncryptedData (referring to the CA protobuf's EncryptedData message type)
// -----------------------------------------------------------------------------

// Encrypts data for an attestation CA. The CA's public key is passed in as an
// input. The output values correspond to the EncryptedData protobuf in
// attestation_ca.proto. Returns true on success and false on failure.
bool EncryptDataForCa(const std::string& data,
                      const std::string& public_key_hex,
                      const std::string& key_id, std::string& wrapped_key,
                      std::string& iv, std::string& mac,
                      std::string& encrypted_data,
                      std::string& wrapping_key_id);

// -----------------------------------------------------------------------------
// HmacAuthorizationDelegate
// -----------------------------------------------------------------------------

// Creates a new HmacAuthorizationDelegate. On error, logs an error message and
// returns a null unique_ptr. See PasswordAuthorizationDelegate for why this
// returns an AuthorizationDelegate rather than a HmacAuthorizationDelegate.
std::unique_ptr<AuthorizationDelegate> HmacAuthorizationDelegate_New(
    TPM_HANDLE session_handle, const std::string& tpm_nonce,
    const std::string& caller_nonce, const std::string& salt,
    const std::string& bind_auth_value, bool enable_parameter_encryption);

// -----------------------------------------------------------------------------
// PasswordAuthorizationDelegate
// -----------------------------------------------------------------------------

// Wraps the PasswordAuthorizationDelegate constructor. Returns an
// AuthorizationDelegate pointer rather than a PasswordAuthorizationDelegate
// pointer because Rust code doesn't know how to convert a
// PasswordAuthorizationDelegate pointer into an AuthorizationDelegate pointer.
std::unique_ptr<AuthorizationDelegate> PasswordAuthorizationDelegate_New(
    const std::string& password);

// -----------------------------------------------------------------------------
// Tpm
// -----------------------------------------------------------------------------

// Wraps Tpm::SerializeCommand_ActivateCredential. Serializes the
// TPM2_ActivateCredential command.
TPM_RC SerializeCommand_ActivateCredential(
    const TPMI_DH_OBJECT& activate_handle,
    const std::string& activate_handle_name, const TPMI_DH_OBJECT& key_handle,
    const std::string& key_handle_name, const std::string& credential_mac,
    const std::string& wrapped_key, const std::string& secret,
    std::string& serialized_command, AuthorizationDelegate& key_authorization);

// Wraps Tpm::ParseResponse_ActivateCredential. Parses the response of a
// TPM2_ActivateCredential command.
TPM_RC ParseResponse_ActivateCredential(
    const std::string& response, std::string& cert_info,
    AuthorizationDelegate& key_authorization);

// Wraps Tpm::SerializeCommand_Create. Serializes the TPM2_Create command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_Create(
    const TPMI_DH_OBJECT& parent_handle, const std::string& parent_handle_name,
    const TPM2B_SENSITIVE_CREATE& in_sensitive, const TPM2B_PUBLIC& in_public,
    const TPM2B_DATA& outside_info, const TPML_PCR_SELECTION& creation_pcr,
    std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_Create. Parses the response from a TPM2_Create
// command.
// out_public is in serialized form (because there is no
// StringFrom_TPM2B_PUBLIC in tpm_generated).
// authorization_delegate is nullable.
TPM_RC ParseResponse_Create(
    const std::string& response, std::string& out_private,
    std::string& out_public, TPM2B_CREATION_DATA& creation_data,
    TPM2B_DIGEST& creation_hash, TPMT_TK_CREATION& creation_ticket,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_CreatePrimary. Serializes the TPM2_CreatePrimary
// command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_CreatePrimary(
    const TPMI_RH_HIERARCHY& primary_handle,
    const std::string& primary_handle_name,
    const TPM2B_SENSITIVE_CREATE& in_sensitive, const TPM2B_PUBLIC& in_public,
    const TPM2B_DATA& outside_info, const TPML_PCR_SELECTION& creation_pcr,
    std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_CreatePrimary. Parses the response from a
// TPM2_CreatePrimary command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_CreatePrimary(
    const std::string& response, TPM_HANDLE& object_handle,
    TPM2B_PUBLIC& out_public, TPM2B_CREATION_DATA& creation_data,
    TPM2B_DIGEST& creation_hash, TPMT_TK_CREATION& creation_ticket,
    std::string& name,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_Load. Serializes the TPM2_Load command.
// in_public should be in serialized form (as there is no direct string ->
// TPM2B_PUBLIC conversion in tpm_generated other than parsing).
// authorization_delegate is nullable.
TPM_RC SerializeCommand_Load(
    const TPMI_DH_OBJECT& parent_handle, const std::string& parent_handle_name,
    const std::string& in_private, const std::string& in_public,
    std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_Load. Parses the response from a TPM2_Load command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_Load(
    const std::string& response, TPM_HANDLE& object_handle, std::string& name,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_NV_Certify. Serializes the TPM2_NV_Certify
// command.
// The authorization_delegate argument was removed because NV_Certify requires
// two authorizations, and adding MultipleAuthorizations to the CXX bridge would
// require putting a lifetime argument on AuthorizationDelegate, which would
// propagate everywhere. Instead, this is hardcoded to use empty password
// authorization.
TPM_RC SerializeCommand_NV_Certify(
    const TPMI_DH_OBJECT& sign_handle, const std::string& sign_handle_name,
    const TPMI_RH_NV_AUTH& auth_handle, const std::string& auth_handle_name,
    const TPMI_RH_NV_INDEX& nv_index, const std::string& nv_index_name,
    const TPM2B_DATA& qualifying_data, const TPMT_SIG_SCHEME& in_scheme,
    const UINT16& size, const UINT16& offset, std::string& serialized_command);

// Wraps Tpm::ParseResponse_NV_Certify. Parses the response from a
// TPM2_NV_Certify command.
// authorization_delegate was omitted for the same reason as
// SerializeCommand_NV_Certify.
TPM_RC ParseResponse_NV_Certify(const std::string& response,
                                std::string& certify_info,
                                std::string& signature);

// Wraps Tpm::SerializeCommand_NV_Read. Serializes the TPM2_NV_Read command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_NV_Read(
    const TPMI_RH_NV_AUTH& auth_handle, const std::string& auth_handle_name,
    const TPMI_RH_NV_INDEX& nv_index, const std::string& nv_index_name,
    const UINT16& size, const UINT16& offset, std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_NV_Read. Parses the response of a TPM2_NV_Read
// command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_NV_Read(
    const std::string& response, std::string& data,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_NV_ReadPublic. Serializes the TPM2_NV_ReadPublic
// command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_NV_ReadPublic(
    const TPMI_RH_NV_INDEX& nv_index, const std::string& nv_index_name,
    std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_NV_ReadPublic. Parses the response from a
// TPM2_NV_ReadPublic command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_NV_ReadPublic(
    const std::string& response, uint16_t& nv_public_data_size,
    std::string& nv_name,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_PolicySecret. Serializes a TPM2_PolicySecret
// command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_PolicySecret(
    const TPMI_DH_ENTITY& auth_handle, const std::string& auth_handle_name,
    const TPMI_SH_POLICY& policy_session,
    const std::string& policy_session_name, const std::string& nonce_tpm,
    const std::string& cp_hash_a, const std::string& policy_ref,
    const uint32_t& expiration, std::string& serialized_command,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_PolicySecret. Parses the response from a
// TPM2_PolicySecret command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_PolicySecret(
    const std::string& response, std::string& timeout,
    uint16_t& policy_ticket_tag, uint32_t& policy_ticket_hierarchy,
    std::string& policy_ticket_digest,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_Quote. Serializes the TPM2_Quote command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_Quote(
    const TPMI_DH_OBJECT& sign_handle, const std::string& sign_handle_name,
    const TPM2B_DATA& qualifying_data, const TPMT_SIG_SCHEME& in_scheme,
    const TPML_PCR_SELECTION& pcrselect, std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_Quote. Parses the response from a TPM2_Quote
// command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_Quote(
    const std::string& response, std::string& quoted, std::string& signature,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_PCR_Read. Serializes the TPM2_PCR_Read command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_PCR_Read(
    const TPML_PCR_SELECTION& pcr_selection_in, std::string& serialized_command,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_PCR_Read. Parses the response from a TPM2_PCR_Read
// command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_PCR_Read(
    const std::string& response, UINT32& pcr_update_counter,
    TPML_PCR_SELECTION& pcr_selection_out, std::string& pcr_values,
    const std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_StartAuthSession. Serializes the
// TPM2_StartAuthSession command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_StartAuthSession(
    const TPMI_DH_OBJECT& tpm_key, const std::string& tpm_key_name,
    const TPMI_DH_ENTITY& bind, const std::string& bind_name,
    const std::string& nonce_caller, const std::string& encrypted_salt,
    const uint8_t& session_type, const uint16_t& auth_hash,
    std::string& serialized_command,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_StartAuthSession. Parses the response from a
// TPM2_StartAuthSession command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_StartAuthSession(
    const std::string& response, TPMI_SH_AUTH_SESSION& session_handle,
    std::string& nonce_tpm,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::SerializeCommand_FlushContext. Serializes the TPM2_FlushContext
// command.
// authorization_delegate is nullable.
TPM_RC SerializeCommand_FlushContext(
    const TPMI_DH_OBJECT& activate_handle,
    std::string& serialized_command,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// Wraps Tpm::ParseResponse_FlushContext. Parses the response from a
// TPM2_FlushContext command.
// authorization_delegate is nullable.
TPM_RC ParseResponse_FlushContext(
    const std::string& response,
    std::unique_ptr<AuthorizationDelegate>& authorization_delegate);

// -----------------------------------------------------------------------------
// TPM_HANDLE
// -----------------------------------------------------------------------------

// Returns a serialized representation of the unmodified handle. This is useful
// for predefined handle values, like TPM_RH_OWNER. For details on what types of
// handles use this name formula see Table 3 in the TPM 2.0 Library Spec Part 1
// (Section 16 - Names).
std::unique_ptr<std::string> NameFromHandle(const TPM_HANDLE& handle);

// -----------------------------------------------------------------------------
// TPM2B_CREATION_DATA
// -----------------------------------------------------------------------------

// Creates a new empty TPM2B_CREATION_DATA.
std::unique_ptr<TPM2B_CREATION_DATA> TPM2B_CREATION_DATA_New();

// -----------------------------------------------------------------------------
// TPM2B_DATA
// -----------------------------------------------------------------------------

// Creates a TPM2B_DATA with the given data.
std::unique_ptr<TPM2B_DATA> TPM2B_DATA_New(const std::string& bytes);

// -----------------------------------------------------------------------------
// TPM2B_DIGEST
// -----------------------------------------------------------------------------

// Creates a new empty TPM2B_DIGEST.
std::unique_ptr<TPM2B_DIGEST> TPM2B_DIGEST_New();

// -----------------------------------------------------------------------------
// TPM2B_PUBLIC
// -----------------------------------------------------------------------------

// Returns the public area template for the Attestation Identity Key.
std::unique_ptr<TPM2B_PUBLIC> AttestationIdentityKeyTemplate();

// Returns the public area template for the Endorsement Key.
std::unique_ptr<TPM2B_PUBLIC> EndorsementKeyTemplate();

// Returns the public area template for the Storage Root Key.
std::unique_ptr<TPM2B_PUBLIC> StorageRootKeyTemplate();

// Converts a serialized TPM2B_PUBLIC (as returned by ParseResponse_Create) into
// a serialized TPMT_PUBLIC (as required by the attestation CA).
TPM_RC Tpm2bPublicToTpmtPublic(const std::string& tpm2b_public,
                               std::string& tpmt_public);

// -----------------------------------------------------------------------------
// TPM2B_SENSITIVE_CREATE
// -----------------------------------------------------------------------------

// Creates a TPM2B_SENSITIVE_CREATE with the given auth and data values.
std::unique_ptr<TPM2B_SENSITIVE_CREATE> TPM2B_SENSITIVE_CREATE_New(
    const std::string& user_auth, const std::string& data);

// -----------------------------------------------------------------------------
// TPML_PCR_SELECTION
// -----------------------------------------------------------------------------

// Returns an empty PCR selection list.
std::unique_ptr<TPML_PCR_SELECTION> EmptyPcrSelection();

// Returns a PCR selection list that selects a single PCR, or nullptr if the pcr
// number is too large.
std::unique_ptr<TPML_PCR_SELECTION> SinglePcrSelection(uint8_t pcr);

// -----------------------------------------------------------------------------
// TPMT_SIG_SCHEME
// -----------------------------------------------------------------------------

// Creates a TPMT_SIGN_SCHEME with hash algorithm SHA-256 and signature
// algorithm ECDSA.
std::unique_ptr<TPMT_SIG_SCHEME> Sha256EcdsaSigScheme();

// -----------------------------------------------------------------------------
// TPMT_TK_CREATION
// -----------------------------------------------------------------------------

// Creates a new, empty TPMT_TK_CREATION.
std::unique_ptr<TPMT_TK_CREATION> TPMT_TK_CREATION_New();

}  // namespace trunks

#endif  // TPM_GENERATED_FFI_H_
