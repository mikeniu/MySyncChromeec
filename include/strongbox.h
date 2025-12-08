/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __INCLUDE_STRONGBOX_CMDS_H
#define __INCLUDE_STRONGBOX_CMDS_H

#include "common.h" /* For __packed. */
#include "extension.h"

/**
 * Command names and values are taken from declare_req_rsp_enums! in:
 * https://cs.android.com/android/platform/superproject/main/+/main:system/keymint/wire/src/types.rs
 */
enum strongbox_cmd_cc {
	SB_DeviceGetHardwareInfo = 0x11,
	SB_DeviceAddRngEntropy = 0x12,
	SB_DeviceGenerateKey = 0x13,
	SB_DeviceImportKey = 0x14,
	SB_DeviceImportWrappedKey = 0x15,
	SB_DeviceUpgradeKey = 0x16,
	SB_DeviceDeleteKey = 0x17,
	SB_DeviceDeleteAllKeys = 0x18,
	SB_DeviceDestroyAttestationIds = 0x19,
	SB_DeviceBegin = 0x1a,
	SB_DeviceEarlyBootEnded = 0x1c,
	SB_DeviceConvertStorageKeyToEphemeral = 0x1d,
	SB_DeviceGetKeyCharacteristics = 0x1e,
	SB_OperationUpdateAad = 0x31,
	SB_OperationUpdate = 0x32,
	SB_OperationFinish = 0x33,
	SB_OperationAbort = 0x34,
	SB_RpcGetHardwareInfo = 0x41,
	SB_RpcGenerateEcdsaP256KeyPair = 0x42,
	SB_RpcGenerateCertificateRequest = 0x43,
	SB_RpcGenerateCertificateV2Request = 0x44,
	SB_SharedSecretGetSharedSecretParameters = 0x51,
	SB_SharedSecretComputeSharedSecret = 0x52,
	SB_SecureClockGenerateTimeStamp = 0x61,
	SB_GetRootOfTrustChallenge = 0x71,
	SB_GetRootOfTrust = 0x72,
	SB_SendRootOfTrust = 0x73,
	SB_SetHalInfo = 0x81,
	SB_SetBootInfo = 0x82,
	SB_SetAttestationIds = 0x83,
	SB_SetHalVersion = 0x84,
	SB_SetAdditionalAttestationInfo = 0x91,

	/* SBS specific commands */
	SB_GetDiceChain = 0xa0,
	SB_SetHalBootInfo = 0xa1,
};

/**
 * Error names and values are taken from ErrorCode enum in:
 * https://cs.android.com/android/platform/superproject/main/+/main:system/keymint/wire/src/keymint.rs
 */
enum strongbox_error {
	SB_OK = 0,
	SBERR_RootOfTrustAlreadySet = -1,
	SBERR_UnsupportedPurpose = -2,
	SBERR_IncompatiblePurpose = -3,
	SBERR_UnsupportedAlgorithm = -4,
	SBERR_IncompatibleAlgorithm = -5,
	SBERR_UnsupportedKeySize = -6,
	SBERR_UnsupportedBlockMode = -7,
	SBERR_IncompatibleBlockMode = -8,
	SBERR_UnsupportedMacLength = -9,
	SBERR_UnsupportedPaddingMode = -10,
	SBERR_IncompatiblePaddingMode = -11,
	SBERR_UnsupportedDigest = -12,
	SBERR_IncompatibleDigest = -13,
	SBERR_InvalidExpirationTime = -14,
	SBERR_InvalidUserId = -15,
	SBERR_InvalidAuthorizationTimeout = -16,
	SBERR_UnsupportedKeyFormat = -17,
	SBERR_IncompatibleKeyFormat = -18,
	SBERR_UnsupportedKeyEncryptionAlgorithm = -19,
	SBERR_UnsupportedKeyVerificationAlgorithm = -20,
	SBERR_InvalidInputLength = -21,
	SBERR_KeyExportOptionsInvalid = -22,
	SBERR_DelegationNotAllowed = -23,
	SBERR_KeyNotYetValid = -24,
	SBERR_KeyExpired = -25,
	SBERR_KeyUserNotAuthenticated = -26,
	SBERR_OutputParameterNull = -27,
	SBERR_InvalidOperationHandle = -28,
	SBERR_InsufficientBufferSpace = -29,
	SBERR_VerificationFailed = -30,
	SBERR_TooManyOperations = -31,
	SBERR_UnexpectedNullPointer = -32,
	SBERR_InvalidKeyBlob = -33,
	SBERR_ImportedKeyNotEncrypted = -34,
	SBERR_ImportedKeyDecryptionFailed = -35,
	SBERR_ImportedKeyNotSigned = -36,
	SBERR_ImportedKeyVerificationFailed = -37,
	SBERR_InvalidArgument = -38,
	SBERR_UnsupportedTag = -39,
	SBERR_InvalidTag = -40,
	SBERR_MemoryAllocationFailed = -41,
	SBERR_ImportParameterMismatch = -44,
	SBERR_SecureHwAccessDenied = -45,
	SBERR_OperationCancelled = -46,
	SBERR_ConcurrentAccessConflict = -47,
	SBERR_SecureHwBusy = -48,
	SBERR_SecureHwCommunicationFailed = -49,
	SBERR_UnsupportedEcField = -50,
	SBERR_MissingNonce = -51,
	SBERR_InvalidNonce = -52,
	SBERR_MissingMacLength = -53,
	SBERR_KeyRateLimitExceeded = -54,
	SBERR_CallerNonceProhibited = -55,
	SBERR_KeyMaxOpsExceeded = -56,
	SBERR_InvalidMacLength = -57,
	SBERR_MissingMinMacLength = -58,
	SBERR_UnsupportedMinMacLength = -59,
	SBERR_UnsupportedKdf = -60,
	SBERR_UnsupportedEcCurve = -61,
	SBERR_KeyRequiresUpgrade = -62,
	SBERR_AttestationChallengeMissing = -63,
	SBERR_KeymintNotConfigured = -64,
	SBERR_AttestationApplicationIdMissing = -65,
	SBERR_CannotAttestIds = -66,
	SBERR_RollbackResistanceUnavailable = -67,
	SBERR_HardwareTypeUnavailable = -68,
	SBERR_ProofOfPresenceRequired = -69,
	SBERR_ConcurrentProofOfPresenceRequested = -70,
	SBERR_NoUserConfirmation = -71,
	SBERR_DeviceLocked = -72,
	SBERR_EarlyBootEnded = -73,
	SBERR_AttestationKeysNotProvisioned = -74,
	SBERR_AttestationIdsNotProvisioned = -75,
	SBERR_InvalidOperation = -76,
	SBERR_StorageKeyUnsupported = -77,
	SBERR_IncompatibleMgfDigest = -78,
	SBERR_UnsupportedMgfDigest = -79,
	SBERR_MissingNotBefore = -80,
	SBERR_MissingNotAfter = -81,
	SBERR_MissingIssuerSubject = -82,
	SBERR_InvalidIssuerSubject = -83,
	SBERR_BootLevelExceeded = -84,
	SBERR_HardwareNotYetAvailable = -85,
	SBERR_ModuleHashAlreadySet = -86,
	SBERR_Unimplemented = -100,
	SBERR_VersionMismatch = -101,
	/* RKP specific errors,  */
	SBERR_RKP_STATUS_FAILED = -201, /* 1 */
	SBERR_RKP_STATUS_INVALID_MAC = -202, /* 2 */
	SBERR_RKP_STATUS_PRODUCTION_KEY_IN_TEST_REQUEST = -203, /* 3 */
	SBERR_RKP_STATUS_TEST_KEY_IN_PRODUCTION_REQUEST = -204, /* 4 */
	SBERR_RKP_STATUS_INVALID_EEK = -205, /* 5 */
	SBERR_RKP_STATUS_REMOVED = -206, /* 6 */

	/* Note, Keymint use -1000 for UnknownError u*/
	SBERR_UnknownError = -255,
};

/* Set the range for the TPM_RC for the STRONGBOX commands.
 * Use same as Vendor commands
 */
#define STRONGBOX_RC_ERR VENDOR_RC_ERR

/* Forward declaration of the internal Keymint state struct */
struct km;

/* Type of function handling extension commands. */
typedef enum strongbox_error (*strongbox_handler)(
	struct vendor_cmd_params *params);

/* Pointer table */
struct strongbox_command {
	uint16_t command_code;

	/**
	 * @param km KeyMint context.
	 * @param buf Input/Output buffer.
	 * @param buf_size_words Size of the I/O buffer in 32-bit words.
	 * @param req_len_words Size of the input data in 32-bit words.
	 * @param out_len_bytes On success, the number of bytes written to the
	 * buffer.
	 * @return SB_OK on success, or an error code on failure.
	 */
	enum strongbox_error (*handler)(struct km *km, uint32_t *buf,
					size_t buf_size_words,
					size_t req_len_words,
					size_t *out_len_bytes);
} __packed;

/* Vendor command which takes params as struct */
#define DECLARE_STRONGBOX_COMMAND(cmd_code, func)                     \
	const struct strongbox_command __keep __no_sanitize_address   \
		__vendor_cmd_##cmd_code                               \
		__attribute__((section(".rodata.strongboxcmds"))) = { \
			.command_code = cmd_code, .handler = func     \
		}

/**
 * Find handler for an StrongBox extension command.
 *
 * Use the interface specific function call in order to check the policies for
 * handling the commands on that interface.
 *
 * @param p		Parameters for the command
 * @return The return code from processing the command.
 */
uint32_t extension_route_strongbox_command(struct vendor_cmd_params *p);

/**
 * @brief Bitfield positions and masks for a 32-bit Keymint tag. It is following
 * with AIDL definition in
 * https://source.corp.google.com/h/android/platform/superproject/main/+/main:hardware/interfaces/security/keymint/aidl/android/hardware/security/keymint/Tag.aidl
 * But extended with 12 bit length in the middle:
 * - [31:28] - enum km_tag_mask (4 bits)
 * - [27:16] - Data Length for blobs (12 bits), set for BYTES/BIGNUM type
 * - [15:0]  - Tag ID (16 bits)
 */
#define KM_TAG_TYPE_SHIFT     28
#define KM_TAG_TYPE_BIT_SIZE  4
#define KM_TAG_TYPE_MASK_BITS ((1U << KM_TAG_TYPE_BIT_SIZE) - 1)
#define KM_TAG_TYPE_MASK      (KM_TAG_TYPE_MASK_BITS << KM_TAG_TYPE_SHIFT)

#define KM_TAG_ID_SHIFT	   0
#define KM_TAG_ID_BIT_SIZE 16
#define KM_TAG_ID_SHIFT	   0
#define KM_TAG_ID_MASK	   (((1U << KM_TAG_ID_BIT_SIZE) - 1) << KM_TAG_ID_SHIFT)

#define KM_TAG_LEN_BITS	     12
#define KM_TAG_LEN_SHIFT     16
#define KM_TAG_LEN_MASK_BITS ((1U << KM_TAG_LEN_BITS) - 1)
#define KM_TAG_LEN_MASK	     (KM_TAG_LEN_MASK_BITS << KM_TAG_LEN_SHIFT)

/**
 * @brief Defines values for the 4-bit field encoding enum km_tag_mask.
 */
enum km_tag_type {
	KM_TAG_TYPE_INVALID = 0,
	KM_TAG_TYPE_ENUM = 1,
	KM_TAG_TYPE_ENUM_REP = 2,
	KM_TAG_TYPE_UINT = 3,
	KM_TAG_TYPE_UINT_REP = 4,
	KM_TAG_TYPE_ULONG = 5,
	KM_TAG_TYPE_DATE = 6,
	KM_TAG_TYPE_BOOL = 7,
	KM_TAG_TYPE_BIGNUM = 8,
	KM_TAG_TYPE_BYTES = 9,
	KM_TAG_TYPE_ULONG_REP = 10,
};

/**
 * @brief Pre-shifted enum km_tag_type values for easy combination with tag
 * IDs.
 */
enum km_tag_mask {
	KM_TYPE_INVALID = (KM_TAG_TYPE_INVALID << KM_TAG_TYPE_SHIFT),
	KM_TYPE_ENUM = (KM_TAG_TYPE_ENUM << KM_TAG_TYPE_SHIFT),
	KM_TYPE_ENUM_REP = (KM_TAG_TYPE_ENUM_REP << KM_TAG_TYPE_SHIFT),
	KM_TYPE_UINT = (KM_TAG_TYPE_UINT << KM_TAG_TYPE_SHIFT),
	KM_TYPE_UINT_REP = (KM_TAG_TYPE_UINT_REP << KM_TAG_TYPE_SHIFT),
	KM_TYPE_ULONG = (KM_TAG_TYPE_ULONG << KM_TAG_TYPE_SHIFT),
	KM_TYPE_DATE = (KM_TAG_TYPE_DATE << KM_TAG_TYPE_SHIFT),
	KM_TYPE_BOOL = (KM_TAG_TYPE_BOOL << KM_TAG_TYPE_SHIFT),
	KM_TYPE_BIGNUM = (KM_TAG_TYPE_BIGNUM << KM_TAG_TYPE_SHIFT),
	KM_TYPE_BYTES = (KM_TAG_TYPE_BYTES << KM_TAG_TYPE_SHIFT),
	KM_TYPE_ULONG_REP = (KM_TAG_TYPE_ULONG_REP << KM_TAG_TYPE_SHIFT),
};

/**
 * @brief Defines known tags used by Keymint.
 * Tags are serialized as 32-bit little-endian unsigned integers.
 * The value is a combination of a shifted TagType and a tag ID.
 */
enum km_tag {
	KM_TAG_INVALID = 0,

	/* Hardware-enforced tags */
	KM_TAG_PURPOSE = (KM_TYPE_ENUM_REP | 1),
	KM_TAG_ALGORITHM = (KM_TYPE_ENUM | 2),
	KM_TAG_KEY_SIZE = (KM_TYPE_UINT | 3),
	KM_TAG_BLOCK_MODE = (KM_TYPE_ENUM_REP | 4),
	KM_TAG_DIGEST = (KM_TYPE_ENUM_REP | 5),
	KM_TAG_PADDING = (KM_TYPE_ENUM_REP | 6),
	KM_TAG_CALLER_NONCE = (KM_TYPE_BOOL | 7),
	KM_TAG_MIN_MAC_LENGTH = (KM_TYPE_UINT | 8),
	KM_TAG_EC_CURVE = (KM_TYPE_ENUM | 10),
	KM_TAG_RSA_PUBLIC_EXPONENT = (KM_TYPE_ULONG | 200),
	KM_TAG_INCLUDE_UNIQUE_ID = (KM_TYPE_BOOL | 202),
	KM_TAG_RSA_OAEP_MGF_DIGEST = (KM_TYPE_ENUM_REP | 203),
	KM_TAG_BOOTLOADER_ONLY = (KM_TYPE_BOOL | 302),
	KM_TAG_ROLLBACK_RESISTANCE = (KM_TYPE_BOOL | 303),
	KM_TAG_HARDWARE_TYPE = (KM_TYPE_ENUM | 304),
	KM_TAG_EARLY_BOOT_ONLY = (KM_TYPE_BOOL | 305),
	KM_TAG_MAX_USES_PER_BOOT = (KM_TYPE_UINT | 404),
	KM_TAG_USAGE_COUNT_LIMIT = (KM_TYPE_UINT | 405),
	KM_TAG_USER_SECURE_ID = (KM_TYPE_ULONG_REP | 502),
	KM_TAG_NO_AUTH_REQUIRED = (KM_TYPE_BOOL | 503),
	KM_TAG_USER_AUTH_TYPE = (KM_TYPE_ENUM | 504),
	KM_TAG_AUTH_TIMEOUT = (KM_TYPE_UINT | 505),
	KM_TAG_TRUSTED_USER_PRESENCE_REQUIRED = (KM_TYPE_BOOL | 507),
	KM_TAG_TRUSTED_CONFIRMATION_REQUIRED = (KM_TYPE_BOOL | 508),
	KM_TAG_UNLOCKED_DEVICE_REQUIRED = (KM_TYPE_BOOL | 509),
	KM_TAG_ORIGIN = (KM_TYPE_ENUM | 702),
	KM_TAG_OS_VERSION = (KM_TYPE_UINT | 705),
	KM_TAG_OS_PATCHLEVEL = (KM_TYPE_UINT | 706),
	KM_TAG_UNIQUE_ID = (KM_TYPE_BYTES | 707),
	KM_TAG_VENDOR_PATCHLEVEL = (KM_TYPE_UINT | 718),
	KM_TAG_BOOT_PATCHLEVEL = (KM_TYPE_UINT | 719),
	KM_TAG_DEVICE_UNIQUE_ATTESTATION = (KM_TYPE_BOOL | 720),
	KM_TAG_IDENTITY_CREDENTIAL_KEY = (KM_TYPE_BOOL | 721),
	KM_TAG_STORAGE_KEY = (KM_TYPE_BOOL | 722),
	KM_TAG_MAC_LENGTH = (KM_TYPE_UINT | 1003),
	KM_TAG_MAX_BOOT_LEVEL = (KM_TYPE_UINT | 1010),

	/* Software-enforced tags */
	KM_TAG_ACTIVE_DATETIME = (KM_TYPE_DATE | 400),
	KM_TAG_ORIGINATION_EXPIRE_DATETIME = (KM_TYPE_DATE | 401),
	KM_TAG_USAGE_EXPIRE_DATETIME = (KM_TYPE_DATE | 402),
	KM_TAG_USER_ID = (KM_TYPE_UINT | 501),
	KM_TAG_ALLOW_WHILE_ON_BODY = (KM_TYPE_BOOL | 506),
	KM_TAG_CREATION_DATETIME = (KM_TYPE_DATE | 701),
	KM_TAG_ATTESTATION_APPLICATION_ID = (KM_TYPE_BYTES | 709),

	/* Input-only / special tags */
	KM_TAG_MIN_SECONDS_BETWEEN_OPS = (KM_TYPE_UINT | 403),
	KM_TAG_APPLICATION_ID = (KM_TYPE_BYTES | 601),
	KM_TAG_APPLICATION_DATA = (KM_TYPE_BYTES | 700),
	KM_TAG_ROOT_OF_TRUST = (KM_TYPE_BYTES | 704),
	KM_TAG_ATTESTATION_CHALLENGE = (KM_TYPE_BYTES | 708),
	KM_TAG_ATTESTATION_ID_BRAND = (KM_TYPE_BYTES | 710),
	KM_TAG_ATTESTATION_ID_DEVICE = (KM_TYPE_BYTES | 711),
	KM_TAG_ATTESTATION_ID_PRODUCT = (KM_TYPE_BYTES | 712),
	KM_TAG_ATTESTATION_ID_SERIAL = (KM_TYPE_BYTES | 713),
	KM_TAG_ATTESTATION_ID_IMEI = (KM_TYPE_BYTES | 714),
	KM_TAG_ATTESTATION_ID_MEID = (KM_TYPE_BYTES | 715),
	KM_TAG_ATTESTATION_ID_MANUFACTURER = (KM_TYPE_BYTES | 716),
	KM_TAG_ATTESTATION_ID_MODEL = (KM_TYPE_BYTES | 717),
	KM_TAG_ATTESTATION_ID_SECOND_IMEI = (KM_TYPE_BYTES | 723),
	KM_TAG_MODULE_HASH = (KM_TYPE_BYTES | 724),
	KM_TAG_ASSOCIATED_DATA = (KM_TYPE_BYTES | 1000),
	KM_TAG_NONCE = (KM_TYPE_BYTES | 1001),
	KM_TAG_RESET_SINCE_ID_ROTATION = (KM_TYPE_BOOL | 1004),
	KM_TAG_CONFIRMATION_TOKEN = (KM_TYPE_BYTES | 1005),
	KM_TAG_CERTIFICATE_SERIAL = (KM_TYPE_BIGNUM | 1006),
	KM_TAG_CERTIFICATE_SUBJECT = (KM_TYPE_BYTES | 1007),
	KM_TAG_CERTIFICATE_NOT_BEFORE = (KM_TYPE_DATE | 1008),
	KM_TAG_CERTIFICATE_NOT_AFTER = (KM_TYPE_DATE | 1009),
};

/* Statically construct Tag prefix with data length */
#define KM_TAG(tag, data_length) \
	(tag | ((data_length & KM_TAG_LEN_MASK_BITS) << KM_TAG_LEN_SHIFT))

/* Returns the `enum km_tag_mask` of this tag. */
static inline enum km_tag_mask tag_get_type(enum km_tag tag)
{
	return (enum km_tag_mask)(tag & KM_TAG_TYPE_MASK);
}

/* Returns the un-shifted `enum km_tag_type` of this tag. */
static inline enum km_tag_type tag_get_type_enum(enum km_tag tag)
{
	return (enum km_tag_type)(tag_get_type(tag) >> KM_TAG_TYPE_SHIFT);
}

/**
 * @brief Extracts the tag type from a serialized tag word.
 */
static inline enum km_tag_type prefix_get_tag_type(uint32_t tag_word)
{
	return (enum km_tag_type)((tag_word & KM_TAG_TYPE_MASK) >>
				  KM_TAG_TYPE_SHIFT);
}

/**
 * @brief Recovers the canonical tag value from a serialized tag word.
 */
static inline enum km_tag prefix_get_tag(uint32_t tag_word)
{
	return (enum km_tag)((tag_word & KM_TAG_TYPE_MASK) |
			     (tag_word & KM_TAG_ID_MASK));
}

/**
 * @brief Gets the data length in bytes for a serialized parameter.
 */
static inline size_t prefix_get_data_len_bytes(uint32_t tag_word)
{
	switch (prefix_get_tag_type(tag_word)) {
	case KM_TAG_TYPE_ENUM:
	case KM_TAG_TYPE_ENUM_REP:
	case KM_TAG_TYPE_UINT:
	case KM_TAG_TYPE_UINT_REP:
		return sizeof(uint32_t);
	case KM_TAG_TYPE_DATE:
	case KM_TAG_TYPE_ULONG:
	case KM_TAG_TYPE_ULONG_REP:
		return sizeof(uint64_t);
	case KM_TAG_TYPE_BYTES:
	case KM_TAG_TYPE_BIGNUM:
		/* For variable length types, length is encoded in the tag
		 * word.
		 */
		return (tag_word & KM_TAG_LEN_MASK) >> KM_TAG_LEN_SHIFT;
	default:
		return 0;
	}
}

/**
 * @brief Gets total length of a serialized parameter in 32-bit words.
 */
static inline size_t prefix_get_word_len(uint32_t tag_word)
{
	size_t data_len = prefix_get_data_len_bytes(tag_word);
	/* +1 word for the tag itself + words for the data payload */
	return 1 + (data_len + sizeof(uint32_t) - 1) / sizeof(uint32_t);
}

/* Keymint key algorithm type */
enum km_algorithm {
	KM_ALG_NONE = 0,
	KM_ALG_RSA = 1,
	KM_ALG_EC = 3,
	KM_ALG_AES = 32,
	KM_ALG_TDES = 33,
	KM_ALG_HMAC = 128,
};

enum km_purpose {
	KM_PURPOSE_ENCRYPT = 0,
	KM_PURPOSE_DECRYPT = 1,
	KM_PURPOSE_SIGN = 2,
	KM_PURPOSE_VERIFY = 3,
	KM_PURPOSE_WRAP_KEY = 5,
	KM_PURPOSE_AGREE_KEY = 6,
	KM_PURPOSE_ATTEST_KEY = 7,
	KM_PURPOSE_LAST,
};

enum km_ec_curve {
	KM_EC_CURVE_P_224 = 0,
	KM_EC_CURVE_P_256 = 1,
	KM_EC_CURVE_P_384 = 2,
	KM_EC_CURVE_P_521 = 3,
};

enum km_security_level {
	KM_SECURITY_SOFTWARE = 0,
	KM_SECURITY_TRUSTED_ENVIRONMENT = 1,
	KM_SECURITY_STRONGBOX = 2,
	KM_SECURITY_KEYSTORE = 100,
};

/* Keymint's key origin. */
enum km_origin {
	KM_ORIGIN_GENERATED = 0,
	KM_ORIGIN_IMPORTED = 1,
	KM_ORIGIN_UNKNOWN = 2,
	KM_ORIGIN_SECURELY_IMPORTED = 3,
};

enum km_digest {
	KM_DIGEST_NONE = 0,
	KM_DIGEST_MD5 = 1,
	KM_DIGEST_SHA1 = 2,
	KM_DIGEST_SHA_2_224 = 3,
	KM_DIGEST_SHA_2_256 = 4,
	KM_DIGEST_SHA_2_384 = 5,
	KM_DIGEST_SHA_2_512 = 6,
};

enum km_vb_state {
	KM_VB_VERIFIED = 0,
	KM_VB_SELF_SIGNED = 1,
	KM_VB_UNVERIFIED = 2,
	KM_VB_FAILED = 3,
};

#endif /* __INCLUDE_STRONGBOX_CMDS_H */
