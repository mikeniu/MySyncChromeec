// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TPM command encoding/decoding library.

use cxx::{let_cxx_string, UniquePtr};
use std::fmt::{self, Display, Formatter, Write};
use std::num::NonZeroU32;

pub use trunks::*;

#[allow(clippy::too_many_arguments)]
#[cxx::bridge(namespace = "trunks")]
pub mod trunks {
    unsafe extern "C++" {
        include!("authorization_delegate.h");

        type AuthorizationDelegate;

        include!("tpm_generated.h");

        type TPM2B_CREATION_DATA;
        type TPM2B_DATA;
        type TPM2B_DIGEST;
        type TPM2B_PUBLIC;
        type TPM2B_SENSITIVE_CREATE;
        type TPMT_SIG_SCHEME;
        type TPML_PCR_SELECTION;
        type TPMT_TK_CREATION;

        include!("ffi.h");

        /// Encrypts data for an attestation CA. The CA's public key is passed
        /// in as an input. The output values correspond to the EncryptedData
        /// protobuf in attestation_ca.proto. Returns true on success and false
        /// on failure.
        fn EncryptDataForCa(
            data: &CxxString,
            public_key_hex: &CxxString,
            key_id: &CxxString,
            wrapped_key: Pin<&mut CxxString>,
            iv: Pin<&mut CxxString>,
            mac: Pin<&mut CxxString>,
            encrypted_data: Pin<&mut CxxString>,
            wrapping_key_id: Pin<&mut CxxString>,
        ) -> bool;

        /// Creates a new HmacAuthorizationDelegate. On error, logs an error
        /// message and returns a null unique_ptr.
        fn HmacAuthorizationDelegate_New(
            session_handle: u32,
            tpm_nonce: &CxxString,
            caller_nonce: &CxxString,
            salt: &CxxString,
            bind_auth_value: &CxxString,
            enable_parameter_encryption: bool,
        ) -> UniquePtr<AuthorizationDelegate>;

        /// Constructs a new PasswordAuthorizationDelegate with the given
        /// password.
        fn PasswordAuthorizationDelegate_New(
            password: &CxxString,
        ) -> UniquePtr<AuthorizationDelegate>;

        /// Wraps Tpm::SerializeCommand_ActivateCredential. Serializes the
        /// TPM2_ActivateCredential command.
        fn SerializeCommand_ActivateCredential(
            activate_handle: &u32,
            activate_handle_name: &CxxString,
            key_handle: &u32,
            key_handle_name: &CxxString,
            credential_mac: &CxxString,
            wrapped_key: &CxxString,
            secret: &CxxString,
            serialized_command: Pin<&mut CxxString>,
            key_authorization: Pin<&mut AuthorizationDelegate>,
        ) -> u32;

        /// Wraps Tpm::ParseResponse_ActivateCredential. Parses the response of
        /// a TPM2_ActivateCredential command.
        fn ParseResponse_ActivateCredential(
            response: &CxxString,
            cert_info: Pin<&mut CxxString>,
            key_authorization: Pin<&mut AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::SerializeCommand_Create for docs.
        fn SerializeCommand_Create(
            parent_handle: &u32,
            parent_handle_name: &CxxString,
            in_sensitive: &TPM2B_SENSITIVE_CREATE,
            in_public: &TPM2B_PUBLIC,
            outside_info: &TPM2B_DATA,
            creation_pcr: &TPML_PCR_SELECTION,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::ParseResponse_Create for docs.
        fn ParseResponse_Create(
            response: &CxxString,
            out_private: Pin<&mut CxxString>,
            out_public: Pin<&mut CxxString>,
            creation_data: Pin<&mut TPM2B_CREATION_DATA>,
            creation_hash: Pin<&mut TPM2B_DIGEST>,
            creation_ticket: Pin<&mut TPMT_TK_CREATION>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::SerializeCommand_CreatePrimary for docs.
        fn SerializeCommand_CreatePrimary(
            primary_handle: &u32,
            primary_handle_name: &CxxString,
            in_sensitive: &TPM2B_SENSITIVE_CREATE,
            in_public: &TPM2B_PUBLIC,
            outside_info: &TPM2B_DATA,
            creation_pcr: &TPML_PCR_SELECTION,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::ParseResponse_CreatePrimary for docs.
        fn ParseResponse_CreatePrimary(
            response: &CxxString,
            object_handle: Pin<&mut u32>,
            out_public: Pin<&mut TPM2B_PUBLIC>,
            creation_data: Pin<&mut TPM2B_CREATION_DATA>,
            creation_hash: Pin<&mut TPM2B_DIGEST>,
            creation_ticket: Pin<&mut TPMT_TK_CREATION>,
            name: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::SerializeCommand_Load for docs.
        fn SerializeCommand_Load(
            parent_handle: &u32,
            parent_handle_name: &CxxString,
            in_private: &CxxString,
            in_public: &CxxString,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::ParseResponse_Load for docs.
        fn ParseResponse_Load(
            response: &CxxString,
            object_handle: Pin<&mut u32>,
            name: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::SerializeCommand_NV_Certify for docs.
        fn SerializeCommand_NV_Certify(
            sign_handle: &u32,
            sign_handle_name: &CxxString,
            auth_handle: &u32,
            auth_handle_name: &CxxString,
            nv_index: &u32,
            nv_index_name: &CxxString,
            qualifying_data: &TPM2B_DATA,
            in_scheme: &TPMT_SIG_SCHEME,
            size: &u16,
            offset: &u16,
            serialized_command: Pin<&mut CxxString>,
        ) -> u32;

        /// See Tpm::ParseResponse_NV_Certify for docs.
        fn ParseResponse_NV_Certify(
            response: &CxxString,
            certify_info: Pin<&mut CxxString>,
            signature: Pin<&mut CxxString>,
        ) -> u32;

        /// See Tpm::SerializeCommand_NV_Read for docs.
        fn SerializeCommand_NV_Read(
            auth_handle: &u32,
            auth_handle_name: &CxxString,
            nv_index: &u32,
            nv_index_name: &CxxString,
            size: &u16,
            offset: &u16,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::ParseResponse_NV_Read for docs.
        fn ParseResponse_NV_Read(
            response: &CxxString,
            data: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::SerializeCommand_NV_ReadPublic for docs.
        fn SerializeCommand_NV_ReadPublic(
            nv_index: &u32,
            nv_index_name: &CxxString,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::ParseResponse_NV_ReadPublic for docs.
        fn ParseResponse_NV_ReadPublic(
            response: &CxxString,
            nv_public_data_size: &mut u16,
            nv_name: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::SerializeCommand_PolicySecret for docs.
        fn SerializeCommand_PolicySecret(
            auth_handle: &u32,
            auth_handle_name: &CxxString,
            policy_session: &u32,
            policy_session_name: &CxxString,
            nonce_tpm: &CxxString,
            cp_hash_a: &CxxString,
            policy_ref: &CxxString,
            expiration: &u32,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: Pin<&mut UniquePtr<AuthorizationDelegate>>,
        ) -> u32;

        /// Wraps Tpm::ParseResponse_PolicySecret. Parses the response from a
        /// TPM2_PolicySecret command.
        /// authorization_delegate is nullable.
        fn ParseResponse_PolicySecret(
            response: &CxxString,
            timeout: Pin<&mut CxxString>,
            policy_ticket_tag: &mut u16,
            policy_ticket_hierarchy: &mut u32,
            policy_ticket_digest: Pin<&mut CxxString>,
            authorization_delegate: Pin<&mut UniquePtr<AuthorizationDelegate>>,
        ) -> u32;

        /// See Tpm::SerializeCommand_Quote for docs.
        fn SerializeCommand_Quote(
            sign_handle: &u32,
            sign_handle_name: &CxxString,
            qualifying_data: &TPM2B_DATA,
            in_scheme: &TPMT_SIG_SCHEME,
            pcrselect: &TPML_PCR_SELECTION,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::ParseResponse_Quote for docs.
        fn ParseResponse_Quote(
            response: &CxxString,
            quoted: Pin<&mut CxxString>,
            signature: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::SerializeCommand_PCR_Read for docs.
        fn SerializeCommand_PCR_Read(
            pcr_selection_id: &TPML_PCR_SELECTION,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::ParseResponse_PCR_Read for docs.
        fn ParseResponse_PCR_Read(
            response: &CxxString,
            pcr_update_counter: &mut u32,
            pcr_selection_out: Pin<&mut TPML_PCR_SELECTION>,
            pcr_values: Pin<&mut CxxString>,
            authorization_delegate: &UniquePtr<AuthorizationDelegate>,
        ) -> u32;

        /// See Tpm::SerializeCommand_StartAuthSession for docs.
        fn SerializeCommand_StartAuthSession(
            tpm_key: &u32,
            tpm_key_name: &CxxString,
            bind: &u32,
            bind_name: &CxxString,
            nonce_caller: &CxxString,
            encrypted_salt: &CxxString,
            session_type: &u8,
            auth_hash: &u16,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: Pin<&mut UniquePtr<AuthorizationDelegate>>,
        ) -> u32;

        /// See Tpm::ParseResponse_StartAuthSession for docs.
        fn ParseResponse_StartAuthSession(
            response: &CxxString,
            session_handle: &mut u32,
            nonce_tpm: Pin<&mut CxxString>,
            authorization_delegate: Pin<&mut UniquePtr<AuthorizationDelegate>>,
        ) -> u32;

        /// See Tpm::SerializeCommand_FlushContext for docs.
        fn SerializeCommand_FlushContext(
            handle: &u32,
            serialized_command: Pin<&mut CxxString>,
            authorization_delegate: Pin<&mut UniquePtr<AuthorizationDelegate>>,
        ) -> u32;

        /// See Tpm::ParseResponse_FlushContext for docs.
        fn ParseResponse_FlushContext(
            response: &CxxString,
            authorization_delegate: Pin<&mut UniquePtr<AuthorizationDelegate>>,
        ) -> u32;

        /// Returns a serialized representation of the unmodified handle. This
        /// is useful for predefined handle values, like TPM_RH_OWNER. For
        /// details on what types of handles use this name formula see Table 3
        /// in the TPM 2.0 Library Spec Part 1 (Section 16 - Names).
        fn NameFromHandle(handle: &u32) -> UniquePtr<CxxString>;

        /// Creates a new empty TPM2B_CREATION_DATA.
        fn TPM2B_CREATION_DATA_New() -> UniquePtr<TPM2B_CREATION_DATA>;

        /// Creates a TPM2B_DATA with the given data.
        fn TPM2B_DATA_New(bytes: &CxxString) -> UniquePtr<TPM2B_DATA>;

        /// Creates a new empty TPM2B_DIGEST.
        fn TPM2B_DIGEST_New() -> UniquePtr<TPM2B_DIGEST>;

        /// Returns the public area template for the Attestation Identity Key.
        fn AttestationIdentityKeyTemplate() -> UniquePtr<TPM2B_PUBLIC>;

        /// Returns the public area template for the Endorsement Key.
        fn EndorsementKeyTemplate() -> UniquePtr<TPM2B_PUBLIC>;

        /// Returns the public area template for the Storage Root Key.
        fn StorageRootKeyTemplate() -> UniquePtr<TPM2B_PUBLIC>;

        /// Converts a serialized TPM2B_PUBLIC (as returned by
        /// ParseResponse_Create) into a serialized TPMT_PUBLIC (as required by
        /// the attestation CA).
        fn Tpm2bPublicToTpmtPublic(
            tpm2b_public: &CxxString,
            tpmt_public: Pin<&mut CxxString>,
        ) -> u32;

        /// Creates a new TPM2B_SENSITIVE_CREATE with the given auth and data
        /// values.
        fn TPM2B_SENSITIVE_CREATE_New(
            user_auth: &CxxString,
            data: &CxxString,
        ) -> UniquePtr<TPM2B_SENSITIVE_CREATE>;

        /// Returns an empty PCR selection list.
        fn EmptyPcrSelection() -> UniquePtr<TPML_PCR_SELECTION>;

        /// Returns a PCR selection list that selects a single PCR.
        fn SinglePcrSelection(pcr: u8) -> UniquePtr<TPML_PCR_SELECTION>;

        /// Creates a TPMT_SIGN_SCHEME with hash algorithm SHA-256 and signature
        /// algorithm ECDSA.
        fn Sha256EcdsaSigScheme() -> UniquePtr<TPMT_SIG_SCHEME>;

        /// Makes an empty TPMT_TK_CREATION;
        fn TPMT_TK_CREATION_New() -> UniquePtr<TPMT_TK_CREATION>;
    }
}

/// An error code returned by a Tpm method.
#[derive(Debug)]
pub struct TpmError {
    return_code: NonZeroU32,
}

impl TpmError {
    /// Creates a TpmError for the given return code, or None if this is a
    /// successful code.
    pub fn from_tpm_return_code(return_code: u32) -> Option<TpmError> {
        Some(TpmError { return_code: NonZeroU32::new(return_code)? })
    }
}

impl Display for TpmError {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "{:#x}", self.return_code)
    }
}

/// Converts the given byte array into a hex string (intended for debug prints).
pub fn bytes_to_hex(bytes: &[u8]) -> String {
    let expected_len = 2 * bytes.len();
    let mut out = String::with_capacity(expected_len);
    for b in bytes {
        write!(out, "{:02x}", b).unwrap();
    }
    out
}
