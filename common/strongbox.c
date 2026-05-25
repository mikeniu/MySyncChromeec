/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>

#include "console.h"
#include "endian.h"
#include "extension.h"
#include "link_defs.h"
#include "strongbox.h"
#include "dcrypto.h"
#include "internal.h"
#include "nvmem_vars.h"
#include "board_id_features.h"

#include "cbor_basic.h"
#include "cbor_boot_param.h"
#include "boot_param.h"
#include "ccd_config.h"

#define CPRINTS(format, args...) cprints(CC_EXTENSION, format, ##args)

struct slice {
	const void *p;
	size_t n;
};

#define SLICE(pp, nn) ((struct slice){ .p = (pp), .n = (nn) })
#define SLICE_OBJ(X)  SLICE(X, sizeof(X))
#define SLICE_STR(X)  SLICE(X, sizeof(X) - 1)

/* Configuration for key blob encryption and integrity. */
#define KM_AES_ENCRYPT_KEY_BITS	 256
#define KM_HMAC_SIGN_KEY_BITS	 256
#define KM_AES_IV_SIZE		 16
#define KM_AES_ENCRYPT_KEY_SIZE	 (KM_AES_ENCRYPT_KEY_BITS / CHAR_BIT)
#define KM_HMAC_SIGN_KEY_SIZE	 (KM_HMAC_SIGN_KEY_BITS / CHAR_BIT)
#define KM_AES_IV_WORDS		 (KM_AES_IV_SIZE / sizeof(uint32_t))
#define KM_AES_ENCRYPT_KEY_WORDS (KM_AES_ENCRYPT_KEY_SIZE / sizeof(uint32_t))
#define KM_HMAC_SIGN_KEY_WORDS	 (KM_HMAC_SIGN_KEY_SIZE / sizeof(uint32_t))

#define KM_MAX_CHALLENGE_LEN   64
#define KM_MAX_CHALLENGE_WORDS (KM_MAX_CHALLENGE_LEN / sizeof(uint32_t))

struct km_key_attr {
	uint64_t rsa_exponent;
	uint32_t purpose_flags;
	uint32_t algorithm;
	uint32_t key_size;
	uint32_t curve_id;
	uint32_t digest_flags;
	bool caller_nonce;
};

/* Corresponds to `KeyAttributes` struct. Filled during tag parsing. */
struct km_key_params {
	struct km_key_attr attrs;
	struct slice application_id;
	struct slice application_data;
	struct slice certificate_subject;
	struct slice certificate_issuer;
	struct slice certificate_serial;
	/* TAG::ROOT_OF_TRUST */
	struct slice rootOfTrust;

	struct slice attestationChallenge;
	struct slice uniqueId;

	/* Tag::ATTESTATION_APPLICATION_ID [709] */
	struct slice attestationApplicationId;
	/* Tag::ATTESTATION_ID_BRAND [710] */
	struct slice attestationIdBrand;
	/* Tag::ATTESTATION_ID_DEVICE [711]*/
	struct slice attestationIdDevice;
	/* Tag::ATTESTATION_ID_PRODUCT [712] */
	struct slice attestationIdProduct;
	/* Tag::ATTESTATION_ID_SERIAL [713]*/
	struct slice attestationIdSerial;
	/* Tag::ATTESTATION_ID_IMEI [714] */
	struct slice attestationIdImei;
	/* Tag::ATTESTATION_ID_MEID [715] */
	struct slice attestationIdMeid;
	/* Tag::ATTESTATION_ID_MANUFACTURER [716] */
	struct slice attestationIdManufacturer;
	/* Tag::ATTESTATION_ID_MODEL [717] */
	struct slice attestationIdModel;
	/* Tag::MODULE_HASH [724] */
	struct slice moduleHash;

	/* Tag::CERTIFICATE_NOT_AFTER */
	uint64_t date_not_after;
	/* Tag::CERTIFICATE_NOT_BEFORE */
	uint64_t date_not_before;
	/* Tag::ACTIVE_DATETIME [400] */
	uint64_t activeDateTime;
	/* Tag::ORIGINATION_EXPIRE_DATETIME [401] */
	uint64_t originationExpireDateTime;

	/* Tag::CREATION_DATETIME [701] */
	uint64_t creationDateTime;
	/* Tag::OS_VERSION, [705] */
	uint32_t osVersion;
	/* Tag::PATCHLEVEL [706] */
	uint32_t osPatchLevel;
	/* Tag::VENDOR_PATCHLEVEL [718] */
	uint32_t vendorPatchLevel;
	/* Tag::BOOT_PATCHLEVEL [719] */
	uint32_t bootPatchLevel;
	/* TAG::ORIGIN [702] */
	enum km_origin origin;
	/* Tag::DEVICE_UNIQUE_ATTESTATION [720] */
	bool deviceUniqueAttestation;
	/* Tag::ROLLBACK_RESISTANCE [303] */
	bool rollbackResistance;
	/* Tag::EARLY_BOOT_ONLY [304] */
	bool earlyBootOnly;
	bool noAuthRequired;
	bool includeUniqueId;
};
struct tag_offset {
	enum km_tag tag;
	size_t offset;
};

#define TAG_OFFSET(T, F) \
	{ .tag = T, .offset = offsetof(struct km_key_params, F) }

static const struct tag_offset slice_tags[] = {
	TAG_OFFSET(KM_TAG_APPLICATION_ID, application_id),
	TAG_OFFSET(KM_TAG_APPLICATION_DATA, application_data),
	TAG_OFFSET(KM_TAG_ROOT_OF_TRUST, rootOfTrust),
	TAG_OFFSET(KM_TAG_ATTESTATION_APPLICATION_ID, attestationApplicationId),
	TAG_OFFSET(KM_TAG_ATTESTATION_CHALLENGE, attestationChallenge),
	TAG_OFFSET(KM_TAG_ATTESTATION_ID_BRAND, attestationIdBrand),
	TAG_OFFSET(KM_TAG_ATTESTATION_ID_DEVICE, attestationIdDevice),
	TAG_OFFSET(KM_TAG_ATTESTATION_ID_PRODUCT, attestationIdProduct),
	TAG_OFFSET(KM_TAG_ATTESTATION_ID_SERIAL, attestationIdSerial),
	TAG_OFFSET(KM_TAG_ATTESTATION_ID_IMEI, attestationIdImei),
	TAG_OFFSET(KM_TAG_ATTESTATION_ID_MEID, attestationIdMeid),
	TAG_OFFSET(KM_TAG_ATTESTATION_ID_MANUFACTURER,
		   attestationIdManufacturer),
	TAG_OFFSET(KM_TAG_ATTESTATION_ID_MODEL, attestationIdModel),
	/* Ignore KM_TAG_ATTESTATION_ID_SECOND_IMEI */
	TAG_OFFSET(KM_TAG_MODULE_HASH, moduleHash),
	TAG_OFFSET(KM_TAG_UNIQUE_ID, uniqueId),
	/* Ignore KM_TAG_ASSOCIATED_DATA */
	/* Ignore KM_TAG_NONCE */
	/* Ignore KM_TAG_CONFIRMATION_TOKEN */
	TAG_OFFSET(KM_TAG_CERTIFICATE_SERIAL, certificate_serial),
	TAG_OFFSET(KM_TAG_CERTIFICATE_SUBJECT, certificate_subject),
};
/* Starting index of the tag going into certificate.
 * Skip APPLICATION_ID and APPLICATION_DATA.
 */
#define KM_SLICE_TAG_CERT_START 2

static const struct tag_offset uint64t_tags[] = {
	TAG_OFFSET(KM_TAG_RSA_PUBLIC_EXPONENT, attrs.rsa_exponent),
	TAG_OFFSET(KM_TAG_CERTIFICATE_NOT_AFTER, date_not_after),
	TAG_OFFSET(KM_TAG_CERTIFICATE_NOT_BEFORE, date_not_before),
	TAG_OFFSET(KM_TAG_ACTIVE_DATETIME, activeDateTime),
	TAG_OFFSET(KM_TAG_ORIGINATION_EXPIRE_DATETIME,
		   originationExpireDateTime),
	TAG_OFFSET(KM_TAG_CREATION_DATETIME, creationDateTime),
};

static const struct tag_offset uint32t_tags[] = {
	TAG_OFFSET(KM_TAG_ALGORITHM, attrs.algorithm),
	TAG_OFFSET(KM_TAG_KEY_SIZE, attrs.key_size),
	TAG_OFFSET(KM_TAG_EC_CURVE, attrs.curve_id),
	TAG_OFFSET(KM_TAG_OS_VERSION, osVersion),
	TAG_OFFSET(KM_TAG_OS_PATCHLEVEL, osPatchLevel),
	TAG_OFFSET(KM_TAG_VENDOR_PATCHLEVEL, vendorPatchLevel),
	TAG_OFFSET(KM_TAG_BOOT_PATCHLEVEL, bootPatchLevel),
};

static const struct tag_offset bool_tags[] = {
	TAG_OFFSET(KM_TAG_CALLER_NONCE, attrs.caller_nonce),
	TAG_OFFSET(KM_TAG_DEVICE_UNIQUE_ATTESTATION, deviceUniqueAttestation),
	TAG_OFFSET(KM_TAG_ROLLBACK_RESISTANCE, rollbackResistance),
	TAG_OFFSET(KM_TAG_EARLY_BOOT_ONLY, earlyBootOnly),
	TAG_OFFSET(KM_TAG_NO_AUTH_REQUIRED, noAuthRequired),
	TAG_OFFSET(KM_TAG_INCLUDE_UNIQUE_ID, includeUniqueId),
};

/* Make DIGEST::NONE to use same space as SHA256 context */
struct digest_none_ctx {
	uint32_t update_size;
	uint32_t update_context[sizeof(struct sha512_ctx) / 4 - 1];
};

struct km_operation {
	uint32_t operation_id;
	/* Note, attr.digest is actual digest, not a flag! */
	struct km_key_attr attrs;
	enum km_purpose purpose;
	uint32_t key[8];
	union {
		struct digest_none_ctx none_ctx;
		struct sha256_ctx sha256_ctx;
		struct sha512_ctx sha512_ctx;
	};
};

#define KM_MAX_OPS 4

/* Persistent configuration parameters */
struct keymint_config {
	uint32_t hmac_tag_key[8];
	uint32_t drbg_seed_key[8];
};

/* Keymint context */
struct km {
	uint32_t os_version;
	uint32_t os_patchlevel;
	uint32_t vendor_patchlevel;
	uint32_t boot_patchlevel;
	struct keymint_config c;
	/* Bit mask for used slots in `ops`. */
	uint32_t used_slots;
	/* Public P256 for the last created key. */
	p256_int last_pk_x, last_pk_y;
	struct km_operation ops[KM_MAX_OPS];
	bool initialized;
};

/* Keymint context */
static struct km km;

static size_t SB_cert_name(const p256_int *d, const p256_int *pk_x,
			   const p256_int *pk_y,
			   const struct km_key_params *params, uint8_t *cert,
			   const size_t n);

static const uint8_t g_keymint_var_name[] = { 'K', 'M' };

/* Set the default subject:
 * SEQUENCE 0x30 <size = 0x1f>
 * SET 0x31 <size = 0x1d>
 * SEQUENCE 0x30 <size = 0x1b>
 * OBJECT 0x06 <size = 0x03> 0x55 0x04 0x03 (:commonName)
 * PRINTABLESTRING 0x13 <size = 0x14> "Android Keystore Key"
 */
static const char g_default_subject_data[] = {
	0x30, 0x1f, 0x31, 0x1d, 0x30, 0x1b, 0x06, 0x03, 0x55, 0x04, 0x03,
	0x13, 0x14, 0x41, 0x6e, 0x64, 0x72, 0x6f, 0x69, 0x64, 0x20, 0x4b,
	0x65, 0x79, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x20, 0x4b, 0x65, 0x79
};
static const struct slice g_default_subject =
	SLICE(g_default_subject_data, sizeof(g_default_subject_data));

static enum strongbox_error keymint_init(void)
{
	const struct tuple *var;

	memset(&km, 0, sizeof(km));

	var = getvar(g_keymint_var_name, sizeof(g_keymint_var_name));
	if (var) {
		if (var->val_len == sizeof(km.c)) {
			memcpy(&km.c, tuple_val(var), sizeof(km.c));
			freevar(var);
			km.initialized = true;
			return SB_OK;
		}
	} else
		freevar(var);

	/* No keymint data yet, let's create it. */
	if (!fips_trng_bytes(&km.c, sizeof(km.c)))
		return SBERR_HardwareTypeUnavailable;

	if (setvar(g_keymint_var_name, sizeof(g_keymint_var_name),
		   (uint8_t *)&km.c, sizeof(km.c)) != 0)
		return SBERR_SecureHwBusy;

	km.initialized = true;
	return SB_OK;
}

void keymint_deinit(void)
{
	km.initialized = false;
}

uint32_t extension_route_strongbox_command(struct vendor_cmd_params *p)
{
	const struct strongbox_command *cmd_p;
	const struct strongbox_command *end_p;
	uint32_t board_cfg;
	size_t buf_size_words = p->out_size / sizeof(uint32_t);

#ifdef DEBUG_EXTENSION
	CPRINTS("%s(%d,%s) is=%d os=%d", __func__, p->code,
		p->flags & VENDOR_CMD_FROM_USB ? "USB" : "AP", p->in_size,
		p->out_size);
#endif

	/* Set the response size to 0 for all error cases. */
	p->out_size = 0;

	/* Check that command came from valid interface in a valid state. */
	if ((p->flags & (VENDOR_CMD_FROM_USB | VENDOR_CMD_FROM_ALT_IF))
#ifdef CONFIG_BOARD_ID_SUPPORT
	    || board_id_is_mismatched()
#endif
	)
		return SBERR_HardwareNotYetAvailable;

	board_cfg = get_board_cfg();

	if (board_cfg & BOARD_CFG_SB_DISABLE_SET)
		return SBERR_HardwareNotYetAvailable;

	if (!(board_cfg & BOARD_CFG_SB_ENABLE_SET))
		return SBERR_HardwareNotYetAvailable;

	/* Find the command handler */
	cmd_p = (const struct strongbox_command *)&__strongbox_cmds;
	end_p = (const struct strongbox_command *)&__strongbox_cmds_end;
	while (cmd_p != end_p) {
		if (cmd_p->command_code == p->code) {
			/* Check that input size is aligned, which is convention
			 * for all SB commands.
			 */
			if (p->in_size & (sizeof(uint32_t) - 1))
				return SBERR_InvalidArgument;

			if (!km.initialized) {
				enum strongbox_error err = keymint_init();

				if (err != SB_OK)
					return err;
			}

			return cmd_p->handler(&km, p->buffer, buf_size_words,
					      p->in_size / sizeof(uint32_t),
					      &p->out_size);
		}
		cmd_p++;
	}

	/* Command not found or not allowed */
	return SBERR_Unimplemented;
}

/**
 * @brief Set HW Enforced Parameters
 *
 * Input:
 * - [ 1 byte ] Non-zero value enables Strongbox, zero disables.
 * No output produced.
 * @return VENDOR_RC_SUCCESS on success, or an error code on failure.
 */
static enum vendor_cmd_rc vc_SetStrongboxState(enum vendor_cmd_cc code,
					       void *buf, size_t input_size,
					       size_t *response_size)
{
	uint8_t *b = (uint8_t *)buf;
	uint32_t board_cfg = get_board_cfg();

	*response_size = 0;
	if (input_size != 1)
		return VENDOR_RC_BOGUS_ARGS;

	/* Don't allow to enable if was disabled previously. */
	if (b[0]) {
		if (board_cfg & BOARD_CFG_SB_DISABLE_SET)
			return VENDOR_RC_NOT_ALLOWED;
		store_board_id_features(board_cfg | BOARD_CFG_SB_ENABLE_SET);
	} else {
		/* Set SB_DISABLE, clear SB_ENABLE */
		store_board_id_features((board_cfg & ~BOARD_CFG_SB_ENABLE_SET) |
					BOARD_CFG_SB_DISABLE_SET);
	}
	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_SET_STRONGBOX_STATE, vc_SetStrongboxState);

/**
 * @brief Implements IKeyMintDevice::getHardwareInfo.
 *
 * Provides information about the KeyMint hardware.
 * Doesn't take any parameters.
 *
 * Output Buffer:
 * A 32-byte structure containing hardware details. The caller must provide an
 * output buffer of at least 32 bytes.
 * - [ 4 bytes ] uint32_t version
 * - [ 4 bytes ] enum km_security_level (Strongbox)
 * - [ 4 bytes ] uint32_t keymint_name_len (value: 4)
 * - [ 4 bytes ] char[] keymint_name ("CR50")
 * - [ 4 bytes ] uint32_t keymint_author_len (value: 6)
 * - [ 8 bytes ] char[] keymint_author ("GOOGLE\0\0")
 * - [ 4 bytes ] uint32_t timestamp_token_required (false)
 *
 * @param km KeyMint context.
 * @param buf Input/Output buffer.
 * @param buf_size_words Size of the I/O buffer in 32-bit words.
 * @param req_len_words Size of the input data in 32-bit words.
 * @param out_len_bytes On success, the number of bytes written to the buffer.
 * @return SB_OK on success, or an error code on failure.
 */
enum strongbox_error sb_GetHardwareInfo(struct km *km, uint32_t *buf,
					size_t buf_size_words,
					size_t req_len_words,
					size_t *out_len_bytes)
{
	/* All values are aligned to 32-bit */
	static const uint8_t r[32] = { /* version */
				       0x00, 0x00, 0x00, 0x00,
				       /* enum km_security_level Strongbox */
				       0x00, 0x00, 0x00, 0x02,
				       /* Keymint name, length 0x00000004 */
				       0x00, 0x00, 0x00, 0x04, 'C', 'R', '5',
				       '0',
				       /* Key mint author, data length
					* 0x00000006, actual length 8 bytes
					*/
				       0x00, 0x00, 0x00, 0x06, 'G', 'O', 'O',
				       'G', 'L', 'E', 0x00, 0x00,
				       /* timestamp_token_required = false */
				       0x00, 0x00, 0x00, 0x00
	};
	(void)km;

	if (buf_size_words < sizeof(r) / sizeof(uint32_t))
		return SBERR_InvalidArgument;
	/* No arguments are expected for the command. */
	if (req_len_words)
		return SBERR_InvalidArgument;

	*out_len_bytes = sizeof(r);
	memcpy(buf, &r, sizeof(r));
	return SB_OK;
}
DECLARE_STRONGBOX_COMMAND(SB_DeviceGetHardwareInfo, sb_GetHardwareInfo);

/**
 * @brief Set HW Enforced Parameters
 *
 * Input:
 * - [ 4 bytes ] OS Version
 * - [ 4 bytes ] OS Patch level
 * - [ 4 bytes ] Boot patch level
 * - [ 4 bytes ] Vendor patch level
 *
 * No output produced.
 *
 * @param km KeyMint context.
 * @param buf Input/Output buffer.
 * @param buf_size_words Size of the I/O buffer in 32-bit words.
 * @param req_len_words Size of the input data in 32-bit words.
 * @param out_len_bytes On success, the number of bytes written to the buffer.
 * @return SB_OK on success, or an error code on failure.
 */
enum strongbox_error sb_SetHalBootInfo(struct km *km, uint32_t *buf,
				       size_t buf_size_words,
				       size_t req_len_words,
				       size_t *out_len_bytes)
{
	/* 4 32-bit word arguments are expected for the command. */
	if (req_len_words != 4)
		return SBERR_InvalidArgument;

	km->os_version = buf[0];
	km->os_patchlevel = buf[1];
	km->boot_patchlevel = buf[2];
	km->vendor_patchlevel = buf[3];

	return SB_OK;
}
DECLARE_STRONGBOX_COMMAND(SB_SetHalBootInfo, sb_SetHalBootInfo);

/**
 * @brief Checks if a tag is disallowed during key generation/import.
 */
static bool tag_is_gen_disallowed(enum km_tag tag)
{
	switch (tag) {
	case KM_TAG_INVALID:
	case KM_TAG_MAX_USES_PER_BOOT:
	case KM_TAG_MIN_SECONDS_BETWEEN_OPS:
	case KM_TAG_ORIGIN:
	case KM_TAG_ROOT_OF_TRUST:
	case KM_TAG_OS_VERSION:
	case KM_TAG_OS_PATCHLEVEL:
	case KM_TAG_BOOT_PATCHLEVEL:
	case KM_TAG_VENDOR_PATCHLEVEL:
	case KM_TAG_RESET_SINCE_ID_ROTATION:
		return true;
	default:
		return false;
	}
}

/**
 * @brief Checks if a tag is software-enforced.
 */
static bool tag_is_sw_enforced(enum km_tag tag)
{
	/* Any tag not HW-enforced is considered SW-enforced for this
	 * translation.
	 */
	switch (tag) {
	case KM_TAG_ACTIVE_DATETIME:
	case KM_TAG_ORIGINATION_EXPIRE_DATETIME:
	case KM_TAG_USAGE_EXPIRE_DATETIME:
	case KM_TAG_USER_ID:
	case KM_TAG_ALLOW_WHILE_ON_BODY:
	case KM_TAG_CREATION_DATETIME:
	case KM_TAG_MAX_BOOT_LEVEL:
	case KM_TAG_UNLOCKED_DEVICE_REQUIRED:
		return true;
	default:
		return false;
	}
}

/**
 * @brief Checks if a tag is hardware-enforced.
 */
static bool tag_is_hw_enforced(enum km_tag tag)
{
	static const enum km_tag hw_enforced_tags[] = {
		KM_TAG_USER_SECURE_ID,
		KM_TAG_ALGORITHM,
		KM_TAG_EC_CURVE,
		KM_TAG_USER_AUTH_TYPE,
		KM_TAG_ORIGIN,
		KM_TAG_PURPOSE,
		KM_TAG_BLOCK_MODE,
		KM_TAG_DIGEST,
		KM_TAG_PADDING,
		KM_TAG_RSA_OAEP_MGF_DIGEST,
		KM_TAG_KEY_SIZE,
		KM_TAG_MIN_MAC_LENGTH,
		KM_TAG_MAX_USES_PER_BOOT,
		KM_TAG_AUTH_TIMEOUT,
		KM_TAG_OS_VERSION,
		KM_TAG_OS_PATCHLEVEL,
		KM_TAG_VENDOR_PATCHLEVEL,
		KM_TAG_BOOT_PATCHLEVEL,
		KM_TAG_RSA_PUBLIC_EXPONENT,
		KM_TAG_CALLER_NONCE,
		KM_TAG_BOOTLOADER_ONLY,
		KM_TAG_ROLLBACK_RESISTANCE,
		KM_TAG_EARLY_BOOT_ONLY,
		KM_TAG_NO_AUTH_REQUIRED,
		KM_TAG_TRUSTED_USER_PRESENCE_REQUIRED,
		KM_TAG_TRUSTED_CONFIRMATION_REQUIRED,
		KM_TAG_STORAGE_KEY
	};
	for (size_t i = 0; i < ARRAY_SIZE(hw_enforced_tags); i++) {
		if (tag == hw_enforced_tags[i])
			return true;
	}
	return false;
}

static enum strongbox_error process_tag(enum km_tag tag,
					struct km_key_params *params,
					const uint32_t *p_tag)
{
	const struct tag_offset *t, *t_end;

	t = slice_tags;
	t_end = t + ARRAY_SIZE(slice_tags);
	do {
		if (t->tag == tag) {
			uint32_t tag_word = p_tag[0];
			struct slice *slice =
				(struct slice *)(((uint8_t *)params) +
						 t->offset);

			if (slice->n)
				return SBERR_InvalidTag;
			*slice = SLICE(&p_tag[1],
				       prefix_get_data_len_bytes(tag_word));
			return SB_OK;
		}
		t++;
	} while (t < t_end);

	t = uint64t_tags;
	t_end = t + ARRAY_SIZE(uint64t_tags);
	do {
		if (t->tag == tag) {
			uint64_t *val =
				(uint64_t *)(((uint8_t *)params) + t->offset);

			if (*val)
				return SBERR_InvalidTag;
			*val = make64(p_tag[2], p_tag[1]);
			return SB_OK;
		}
		t++;
	} while (t < t_end);

	t = uint32t_tags;
	t_end = t + ARRAY_SIZE(uint32t_tags);
	do {
		if (t->tag == tag) {
			uint32_t *val =
				(uint32_t *)(((uint8_t *)params) + t->offset);

			if (*val)
				return SBERR_InvalidTag;
			*val = p_tag[1];
			return SB_OK;
		}
		t++;
	} while (t < t_end);

	t = bool_tags;
	t_end = t + ARRAY_SIZE(bool_tags);
	do {
		if (t->tag == tag) {
			bool *val = (bool *)(((uint8_t *)params) + t->offset);

			if (*val)
				return SBERR_InvalidTag;
			*val = true;
			return SB_OK;
		}
		t++;
	} while (t < t_end);

	switch (tag) {
	case KM_TAG_ROLLBACK_RESISTANCE:
		return SBERR_RollbackResistanceUnavailable;
	case KM_TAG_PURPOSE: {
		uint32_t purpose = p_tag[1];

		if (purpose >= 32)
			return SBERR_InvalidTag;
		params->attrs.purpose_flags |= (1 << purpose);
		break;
	}
	case KM_TAG_DIGEST: {
		uint32_t digest = p_tag[1];

		if (digest > KM_DIGEST_SHA_2_512)
			return SBERR_InvalidTag;
		params->attrs.digest_flags |= (1 << digest);
		break;
	}

	default:
		break;
	}
	return SB_OK;
}

/* Fixed tags added to generated KeyCharacteristics */
#define KM_KEY_CHARACTERISTICS_WORDS 12
/**
 * @brief Parse Key parameters tags and produce KeyCharacteristics.
 *
 * This function parses a list of Keymint tags from an input buffer. It
 * validates the tags, extracts key attributes, and separates the tags into
 * hardware-enforced and software-enforced characteristics. The resulting
 * KeyCharacteristics are serialized into the output buffer.
 *
 * @param km The keymint context, containing device state like OS version.
 * @param params Output parameter. A struct to be filled with the parsed key
 *               attributes.
 * @param origin The origin of the key (e.g., generated, imported).
 * @param buf Input buffer containing the key parameters as a series of tags.
 * @param req_len_words The length of the input buffer in words.
 * @param out Output buffer where the serialized KeyCharacteristics will be
 *            written.
 * @param out_len_words The size of the output buffer in words.
 * @param out_written_words Output parameter. The number of words written to the
 *                          output buffer.
 * Input Buffer (buf) Format: [size in 32-bit words] [tags]
 *
 * The input buffer buf is expected to contain a sequence of Keymint tags. The
 * first word buf[0] indicates the total length of the tag list in 32-bit words.
 * Each tag is a 32-bit words with following data. Data is always aligned to
 * 32-bits. For BYTES/BIGNUM tags, the length of data is encoded in the middle
 * 12 bits, see `prefix_get_word_len()`.
 *
 * The function iterates through the tags in buf until it has processed
 * tags_len_words. The output buffer out stores the serialized
 * KeyCharacteristics, separated into hardware-enforced and software-enforced
 * sections. The format is as follows:
 *
 * Hardware-Enforced Section:
 * out[0]: Security Level (always KM_SECURITY_STRONGBOX)
 * out[1]: Length of the hardware-enforced section in words (excluding out[0]
 * and out[1]) Subsequent words: Keymint tags that are hardware-enforced. These
 * tags have the same format as in the input buffer (tag word followed by data
 * payload).
 * Software-Enforced Section (starts immediately after the hardware-enforced
 * section):
 * out[x]: Security Level (always KM_SECURITY_KEYSTORE)
 * out[x + 1]: Length of the software-enforced section in words (excluding
 * out[x] and out[x + 1])
 * Subsequent words: Keymint tags that are software-enforced, using the
 * same format as above.
 * @return SB_OK on success, or an error code on failure.
 */
static enum strongbox_error process_gen_import_tags(
	struct km *km, struct km_key_params *params, enum km_origin origin,
	const uint32_t *buf, size_t req_len_words, uint32_t *out,
	size_t out_len_words, size_t *out_written_words)
{
	size_t tags_len_words;
	size_t hw_tag_start;
	size_t sw_enforced_tag_size_words;
	size_t tag_pos;
	size_t sw_out_start;
	size_t sw_tag_write_pos;

	/* 14 words is the minimal size of the result. */
	if (req_len_words < 1 || out_len_words < 14)
		return SBERR_InvalidTag;

	memset(params, 0, sizeof(*params));

	tags_len_words = buf[0];
	if (tags_len_words > req_len_words - 1)
		return SBERR_InvalidTag;

	/* These tags go into TEE/Strongbox KeyCharacteristics */
	out[0] = (uint32_t)KM_SECURITY_STRONGBOX;
	/* out[1] is length, filled later */
	out[2] = (uint32_t)KM_TAG_ORIGIN;
	out[3] = origin;
	params->origin = origin;
	out[4] = (uint32_t)KM_TAG_OS_VERSION;
	out[5] = km->os_version;
	out[6] = (uint32_t)KM_TAG_OS_PATCHLEVEL;
	out[7] = km->os_patchlevel;
	out[8] = (uint32_t)KM_TAG_VENDOR_PATCHLEVEL;
	out[9] = km->vendor_patchlevel;
	out[10] = (uint32_t)KM_TAG_BOOT_PATCHLEVEL;
	out[11] = km->boot_patchlevel;

	hw_tag_start = KM_KEY_CHARACTERISTICS_WORDS;
	sw_enforced_tag_size_words = 0;
	tag_pos = 1; /* start after length word */

	/* Pass 1: Parse tags, copy HW tags, count SW tags, extract attributes
	 */
	while (tag_pos < tags_len_words + 1) {
		const uint32_t *p_tag = &buf[tag_pos];
		uint32_t tag_word = *p_tag;
		enum km_tag tag = prefix_get_tag(tag_word);
		size_t word_len = prefix_get_word_len(tag_word);
		enum strongbox_error err;

		if (tag_is_gen_disallowed(tag))
			return SBERR_InvalidTag;

		err = process_tag(tag, params, p_tag);
		if (err != SB_OK)
			return err;

		if (tag_is_hw_enforced(tag)) {
			if (hw_tag_start + word_len > out_len_words) {
				/* Not enough space */
				return SBERR_InvalidArgument;
			}
			memcpy(&out[hw_tag_start], p_tag,
			       word_len * sizeof(uint32_t));
			hw_tag_start += word_len;
		} else if (tag_is_sw_enforced(tag)) {
			sw_enforced_tag_size_words += word_len;
		}
		tag_pos += word_len;
	}

	if ((params->attrs.purpose_flags & (1 << KM_PURPOSE_ATTEST_KEY)) != 0 &&
	    (params->attrs.purpose_flags != (1 << KM_PURPOSE_ATTEST_KEY))) {
		return SBERR_IncompatiblePurpose;
	}

	if (params->attrs.algorithm == 0)
		return SBERR_InvalidArgument;

	/* Set length of HW-enforced tags */
	out[1] = (hw_tag_start - 2);

	if (out_len_words < hw_tag_start + 2)
		return SBERR_InvalidArgument;

	/* Mark start of SW-enforced tags */
	out[hw_tag_start] = KM_SECURITY_KEYSTORE;
	out[hw_tag_start + 1] = sw_enforced_tag_size_words;

	sw_out_start = hw_tag_start + 2;
	if (out_len_words < sw_out_start + sw_enforced_tag_size_words)
		return SBERR_InvalidArgument;

	/* Pass 2: Copy SW enforced tags */
	sw_tag_write_pos = 0;
	tag_pos = 1;
	while (tag_pos < tags_len_words + 1) {
		const uint32_t *p_tag = &buf[tag_pos];
		uint32_t tag_word = *p_tag;
		enum km_tag tag = prefix_get_tag(tag_word);
		size_t word_len = prefix_get_word_len(tag_word);

		if (tag_is_sw_enforced(tag)) {
			if (sw_out_start + sw_tag_write_pos + word_len >
			    out_len_words)
				return SBERR_InvalidArgument;

			memcpy(&out[sw_out_start + sw_tag_write_pos], p_tag,
			       word_len * sizeof(uint32_t));
			sw_tag_write_pos += word_len;
		}
		tag_pos += word_len;
	}

	*out_written_words = sw_out_start + sw_enforced_tag_size_words;
	return SB_OK;
}

/* Derive key encryption key and tag key. */
enum dcrypto_result cryptokey_derive_wrapping(
	const struct km *km, const void *tag_data_ptr, size_t tag_size_bytes,
	const struct slice application_id, const struct slice application_data,
	void *hmac_key, size_t hmac_key_len, void *aes_key, size_t aes_key_len)
{
	struct drbg_ctx drbg;
	enum dcrypto_result result = DCRYPTO_FAIL;

	hmac_drbg_init(&drbg, km->c.drbg_seed_key, sizeof(km->c.drbg_seed_key),
		       tag_data_ptr, tag_size_bytes, application_id.p,
		       application_id.n, 4);

	/* Mix in KM_TAG_APPLICATION_DATA ingredient */
	result = hmac_drbg_generate(&drbg, hmac_key, hmac_key_len,
				    application_data.p, application_data.n);
	if (result != DCRYPTO_OK)
		return result;
	result = hmac_drbg_generate(&drbg, aes_key, aes_key_len, NULL, 0);
	drbg_exit(&drbg);
	return result;
}

static enum dcrypto_result cryptokey_generate(struct km *km,
					      enum km_algorithm alg, void *key,
					      size_t key_len)
{
	enum dcrypto_result result;

	do {
		/* Generated keys are randomly created */
		if (!(fips_rand_bytes(key, key_len)))
			return DCRYPTO_FAIL;
		result = DCRYPTO_OK;
		/* ECC P256 require test of the key candidate to be in range */
		if (alg == KM_ALG_EC) {
			p256_int d;

			if (key_len != sizeof(d))
				return DCRYPTO_FAIL;
			/* Test P256 key candidate and save its public key. */
			result = DCRYPTO_p256_key_from_bytes(
				&km->last_pk_x, &km->last_pk_y, &d, key);
			memcpy(key, &d, sizeof(d));
			if (result != DCRYPTO_RETRY)
				break;
		}
	} while (result != DCRYPTO_OK);
	return result;
}

/**
 * @brief Generate key and export it into hw-bound blob
 *
 * @param km Keymint state
 * @param alg Key algorithm (only KM_ALG_EC supported)
 * @param tags_start Start of Keymint tags used for binding
 * @param tag_words Number of Keymint tags in words
 * @param application_id KM_TAG_APPLICATION_ID slice
 * @param application_data KM_TAG_APPLICATION_DATA slice
 * @param blob_size_words in: max size of output; out: actual size
 * @param out_buf output buffer for encrypted key blob
 * @param key output key buffer for the raw key generated
 * @return enum dcrypto_result
 */
static enum dcrypto_result cryptokey_export_bound(
	struct km *km, enum km_algorithm alg, uint32_t *tags_start,
	size_t tag_words, const struct slice application_id,
	const struct slice application_data, size_t *blob_size_words,
	uint32_t *out_buf, uint32_t *key)
{
	enum dcrypto_result result = DCRYPTO_FAIL;
	const struct sha256_digest *digest;
	struct {
		uint32_t aes_key[KM_AES_ENCRYPT_KEY_WORDS],
			hmac_key[KM_HMAC_SIGN_KEY_WORDS];
	} k;

	uint32_t *blob_size = out_buf;
	uint8_t *iv;
	struct hmac_sha256_ctx sha;

	/* Format of the blob:
	 * size 1x32 in bytes | iv 4x32 | encrypted key 8x32 | tag 8x32
	 */
	if (*blob_size_words <
	    (1 + KM_AES_IV_WORDS + P256_NDIGITS + SHA256_DIGEST_WORDS))
		return DCRYPTO_FAIL;

	/* Reserve 1 word for size field (in 32-bit words) */
	out_buf += 1;
	iv = (uint8_t *)out_buf;
	result = cryptokey_derive_wrapping(km, tags_start,
					   tag_words * sizeof(uint32_t),
					   application_id, application_data,
					   k.aes_key, sizeof(k.aes_key),
					   k.hmac_key, sizeof(k.hmac_key));
	if (result != DCRYPTO_OK)
		goto clean;

	result = DCRYPTO_FAIL;
	/* Create random IV for AES-CTR */
	if (!fips_rand_bytes(iv, KM_AES_IV_SIZE))
		goto clean;
	out_buf += KM_AES_IV_WORDS;

	/* So far hardcoded to 256-bit key */
	result = cryptokey_generate(km, alg, key, P256_NBYTES);
	if (result != DCRYPTO_OK)
		goto clean;

	/* Encrypt key */
	result = DCRYPTO_aes_ctr((uint8_t *)out_buf, (uint8_t *)k.aes_key,
				 KM_AES_ENCRYPT_KEY_BITS, iv, (uint8_t *)key,
				 P256_NBYTES);
	if (result != DCRYPTO_OK)
		goto clean;
	out_buf += P256_NBYTES / sizeof(uint32_t);

	/* Add an HMAC tag for integrity */
	result = DCRYPTO_hw_hmac_sha256_init(&sha, k.hmac_key,
					     sizeof(k.hmac_key));
	if (result != DCRYPTO_OK)
		goto clean;
	HMAC_SHA256_update(&sha, key, P256_NBYTES);
	digest = HMAC_SHA256_final(&sha);

	memcpy(out_buf, digest->b8, SHA256_DIGEST_SIZE);
	out_buf += SHA256_DIGEST_WORDS;
	*blob_size = (uint8_t *)out_buf - (uint8_t *)iv;
	/* Return total size of the blob including length field. */
	*blob_size_words = out_buf - blob_size;
	result = DCRYPTO_OK;
clean:
	always_memset(&k, 0xAA, sizeof(k));
	return result;
}

enum dcrypto_result cryptokey_import_bound(
	struct km *km, const uint32_t *tags_start, size_t tag_words,
	const struct slice application_id, const struct slice application_data,
	const uint32_t *blob, size_t blob_size_words, uint32_t *key)
{
	enum dcrypto_result result = DCRYPTO_FAIL;
	const struct sha256_digest *digest;
	struct {
		uint32_t aes_key[KM_AES_ENCRYPT_KEY_WORDS],
			hmac_key[KM_HMAC_SIGN_KEY_WORDS];
	} k;
	uint32_t blob_size = blob[0];
	uint8_t *iv;
	struct hmac_sha256_ctx sha;
	size_t key_size;

	/* Format of the blob:
	 * size 1x32 in bytes | iv 4x32 | encrypted key 8x32 | tag 8x32
	 * Unlike export which is hardcoded, only check for size of fixed sizes
	 */
	if (blob_size < 1 + KM_AES_IV_WORDS + SHA256_DIGEST_WORDS)
		return DCRYPTO_FAIL;

	key_size =
		(blob_size_words - 1 - KM_AES_IV_WORDS - SHA256_DIGEST_WORDS) *
		sizeof(uint32_t);

	/* Reserve 1 word for size field (in 32-bit words) */
	blob += 1;
	iv = (uint8_t *)blob;
	result = cryptokey_derive_wrapping(km, tags_start,
					   tag_words * sizeof(uint32_t),
					   application_id, application_data,
					   k.aes_key, sizeof(k.aes_key),
					   k.hmac_key, sizeof(k.hmac_key));
	if (result != DCRYPTO_OK)
		goto clean;

	result = DCRYPTO_FAIL;
	blob += KM_AES_IV_WORDS;

	/* Decrypt key */
	result = DCRYPTO_aes_ctr((uint8_t *)key, (uint8_t *)k.aes_key,
				 KM_AES_ENCRYPT_KEY_BITS, iv, (uint8_t *)blob,
				 key_size);

	if (result != DCRYPTO_OK)
		goto clean;
	blob += key_size / sizeof(uint32_t);

	/* Verify integrity using HMAC tag */
	result = DCRYPTO_hw_hmac_sha256_init(&sha, k.hmac_key,
					     sizeof(k.hmac_key));
	if (result != DCRYPTO_OK)
		goto clean;
	HMAC_SHA256_update(&sha, key, key_size);
	digest = HMAC_SHA256_final(&sha);

	if (memcmp(digest->b8, blob, SHA256_DIGEST_SIZE) != 0) {
		CPRINTS("blob tag doesn't match");
		result = DCRYPTO_FAIL;
	} else
		result = DCRYPTO_OK;
clean:
	always_memset(&k, 0xAA, sizeof(k));
	return result;
}

/* Common primitive to parse Keymint tags */
static enum strongbox_error parse_params(const uint32_t *buf, size_t buf_len,
					 struct km_key_params *params)
{
	uint32_t tag_pos = 0;

	while (tag_pos < buf_len) {
		const uint32_t *p_tag = &buf[tag_pos];
		uint32_t tag_word = *p_tag;
		enum km_tag tag = prefix_get_tag(tag_word);
		size_t word_len = prefix_get_word_len(tag_word);
		enum strongbox_error err;

		err = process_tag(tag, params, p_tag);
		if (err != SB_OK)
			return err;

		tag_pos += word_len;
	}
	return SB_OK;
}

/**
 * @brief Parses and decrypts a key blob.
 *
 * This function validates the structure of a provided key blob, which is
 * expected to contain serialized hardware and software-enforced key
 * characteristics, along with the encrypted key material. It extracts the
 * key parameters into the `params` struct and decrypts the key into the
 * `key` buffer.
 *
 * @param km The keymint context, used for deriving the decryption key.
 * @param buf Pointer to the buffer containing the key blob to import.
 * @param blob_words The size of the key blob buffer in 32-bit words.
 * @param param_tags Pointer to a buffer with additional parameters for the
 *                   import operation (e.g., application ID). Note, that some
 *                   parameters have special rules to combine.
 * @param param_tags_words The size of the `param_tags` buffer in 32-bit words.
 * @param params Output parameter. A struct to be filled with the parsed key
 *               attributes from the blob.
 * @param key Output parameter. A buffer where the decrypted key material will
 *            be stored.
 * @return SB_OK on successful import, or a strongbox_error code on failure
 *         (e.g., SBERR_InvalidKeyBlob if the blob is malformed).
 */
static enum strongbox_error import_blob(struct km *km, const uint32_t *buf,
					size_t blob_words,
					const uint32_t *param_tags,
					size_t param_tags_words,
					struct km_key_params *params,
					uint32_t *key)
{
	uint32_t hw_tag_words, sw_tag_words, tag_words, key_blob_size;
	enum dcrypto_result result;
	enum strongbox_error err;
	uint32_t digest_flags, purpose_flags, key_alg, key_size;

	/* Blobs have a fixed elements in structure.
	 * see `process_gen_import_tags`
	 */
	if (blob_words < KM_KEY_CHARACTERISTICS_WORDS + KM_AES_IV_WORDS +
				 SHA256_DIGEST_WORDS)
		return SBERR_InvalidKeyBlob;
	if (buf[0] != KM_SECURITY_STRONGBOX)
		return SBERR_InvalidKeyBlob;

	hw_tag_words = buf[1];
	/* Skip KM_SECURITY_STRONGBOX word + size word */
	tag_words = hw_tag_words + 2;
	/* Check we have enough to read sw_tag_words */
	if (tag_words + 1 >= blob_words)
		return SBERR_InvalidKeyBlob;

	if (buf[tag_words] != KM_SECURITY_KEYSTORE)
		return SBERR_InvalidKeyBlob;
	sw_tag_words = buf[tag_words + 1];

	/* Skip KM_SECURITY_KEYSTORE word + size word */
	tag_words += sw_tag_words + 2;
	/* Check we have enough to read key_blob_size */
	if (tag_words >= blob_words)
		return SBERR_InvalidKeyBlob;

	/* Actual encrypted key blob size */
	key_blob_size = buf[tag_words];

	if (key_blob_size / sizeof(uint32_t) + tag_words + 1 != blob_words)
		return SBERR_InvalidKeyBlob;

	/* Parse HW-enforced tags only */
	err = parse_params(buf + 2, hw_tag_words, params);
	if (err != SB_OK)
		return err;

	/* Additional parameters have special rules to combine. */
	digest_flags = params->attrs.digest_flags;
	params->attrs.digest_flags = 0;
	purpose_flags = params->attrs.purpose_flags;
	key_alg = params->attrs.algorithm;
	key_size = params->attrs.key_size;
	params->attrs.algorithm = 0;
	params->attrs.key_size = 0;

	/* Process additional parameters to get application id and data */
	err = parse_params(param_tags, param_tags_words, params);
	if (err != SB_OK)
		return err;

	/* Only allow bits that were allowed in HW enforced params.
	 * If no digest is specified, than use HW enforced digest.
	 */
	if (params->attrs.digest_flags)
		params->attrs.digest_flags &= digest_flags;
	else
		params->attrs.digest_flags = digest_flags;

	if (params->attrs.algorithm && params->attrs.algorithm != key_alg)
		return SBERR_IncompatibleAlgorithm;
	if (params->attrs.key_size && params->attrs.key_size != key_size)
		return SBERR_IncompatibleAlgorithm;
	params->attrs.algorithm = key_alg;
	params->attrs.key_size = key_size;

	/* Purpose is HW enforced, don't update it. */
	params->attrs.purpose_flags = purpose_flags;

	result = cryptokey_import_bound(km, buf, tag_words,
					params->application_id,
					params->application_data,
					buf + tag_words, blob_words - tag_words,
					key);
	if (result != DCRYPTO_OK)
		return SBERR_UnknownError;

	return SB_OK;
}

enum attest {
	ATTEST_FALSE,
	ATTEST_TRUE,
	ATTEST_NO_KEY,
};

/**
 * @brief Generates a key blob based on provided key parameters.
 *
 * This function takes a set of KeyMint tags, processes them to create
 * KeyCharacteristics, generates a new cryptographic key according to these
 * parameters, and then encrypts the key material. The final output is a
 * single "key blob" containing both the characteristics and the encrypted key,
 * which is written to the output buffer.
 *
 * @param km The keymint context, used for cryptographic operations.
 * @param tags A pointer to a buffer of KeyMint tags specifying the parameters
 *             for the key to be generated.
 * @param tag_words The length of the `tags` buffer in 32-bit words.
 * @param out_buf The output buffer where the generated key blob will be
 *                written. The blob includes a size prefix, key
 *                characteristics, and the encrypted key material.
 * @param out_words As an input, the size of `out_buf` in words. On successful
 *                  return, this is updated with the total number of words
 *                  written to `out_buf`.
 * @return SB_OK on success, or a strongbox_error code on failure (e.g.,
 *         SBERR_UnsupportedKeySize).
 */
static enum strongbox_error generate_key_blob(
	struct km *km, const uint32_t *tags, size_t tag_words,
	uint32_t *out_buf, size_t *out_words, enum attest attest)
{
	struct {
		struct km_key_params params;
		struct km_key_params sign_params;
		p256_int attest_key;
		uint32_t key[P256_NDIGITS];
	} k;
	uint32_t *key_blob_size = out_buf;
	size_t blob_start_words = 0;
	size_t blob_size_words;
	size_t total_words;
	size_t out_buf_words = *out_words;
	uint32_t *cert_size;

	enum dcrypto_result result;
	enum strongbox_error err;

	always_memset(&k, 0, sizeof(k));
	/* Reserve 1 word for size field (in 32-bit words) */
	out_buf += 1;
	out_buf_words -= 1;

	k.params.attrs.algorithm = -1;
	err = process_gen_import_tags(km, &k.params, KM_ORIGIN_GENERATED, tags,
				      tag_words, out_buf, out_buf_words,
				      &blob_start_words);
	if (err != SB_OK)
		return err;

	err = SBERR_Unimplemented;

	/* Return more specific error for unsupported RSA and AES keys.*/
	if (k.params.attrs.algorithm == KM_ALG_RSA) {
		if (k.params.attrs.key_size != 1024 &&
		    k.params.attrs.key_size != 2048)
			err = SBERR_UnsupportedKeySize;
		if (k.params.attrs.rsa_exponent != 3 &&
		    k.params.attrs.rsa_exponent != 65537)
			err = SBERR_InvalidArgument;
	}

	if (k.params.attrs.algorithm == KM_ALG_AES) {
		if ((k.params.attrs.key_size != 128 &&
		     k.params.attrs.key_size != 192 &&
		     k.params.attrs.key_size != 256))
			err = SBERR_UnsupportedKeySize;
	}
	if (k.params.attrs.algorithm != KM_ALG_EC)
		goto cleanup;

	/* Need at least one of the curve_id or
	 * key size to be specified.
	 */
	if (k.params.attrs.curve_id != KM_EC_CURVE_P_256 &&
	    k.params.attrs.key_size != 256)
		goto cleanup;

	err = SBERR_InsufficientBufferSpace;

	if (blob_start_words >= out_buf_words)
		goto cleanup;

	if (attest == ATTEST_TRUE) {
		size_t attest_key_space, attest_key_size = 0;
		size_t issuer_subject_start, issuer_subject_size = 0;
		/* Check below is already done, but keep it for safety */
		if (tags[0] > tag_words - 1)
			goto cleanup; /* return SBERR_UnknownError; */
		attest_key_space = tag_words - tags[0] - 1;
		if (attest_key_space >= 1) {
			/* peek into blob's total size */
			attest_key_size = tags[tags[0] + 1];
		};
		if (attest_key_size == 0) {
			/* Either we don't have attestation key at all, or its
			 * size is set to zero. In this case mark that we don't
			 * have attestation key, so will return an empty
			 * certificate.
			 */
			attest = ATTEST_NO_KEY;
		} else {
			/* Check if attest key blob is plausible */
			if (attest_key_size + 1 > attest_key_space) {
				err = SBERR_InvalidKeyBlob;
				goto cleanup;
			}
			/* Import attestation key, skipping length */
			err = import_blob(km, tags + tags[0] + 2,
					  attest_key_size, NULL, 0,
					  &k.sign_params, k.attest_key.a);
			if (err != SB_OK)
				goto cleanup;
			if ((k.sign_params.attrs.purpose_flags &
			     ((1 << KM_PURPOSE_SIGN) |
			      (1 << KM_PURPOSE_ATTEST_KEY))) == 0) {
				err = SBERR_IncompatiblePurpose;
				goto cleanup;
			}
			/* The remainder is the issuer_subject */
			attest_key_space -= attest_key_size + 1;
			if (attest_key_space < 1) {
				err = SBERR_MissingIssuerSubject;
				goto cleanup;
			}
			issuer_subject_start = tags[0] + 2 + attest_key_size;
			issuer_subject_size = tags[issuer_subject_start];
			if ((issuer_subject_size + 3) / 4 + 1 !=
			    attest_key_space) {
				err = SBERR_InvalidIssuerSubject;
				goto cleanup;
			}
			if (issuer_subject_size > 0)
				k.params.certificate_issuer =
					SLICE(tags + issuer_subject_start + 1,
					      issuer_subject_size);
		}
	}

	/* Set max size for the actual key blob */
	blob_size_words = out_buf_words - blob_start_words;

	/* Create a random key and place it into encrypted key blob with
	 * security tag. Encrypted blob is bound to all the tags and the
	 * Application ID and Application Data which are excluded from
	 * the key characteristics, but provided later with the `begin`
	 * operation.
	 */
	result = cryptokey_export_bound(
		km, k.params.attrs.algorithm, out_buf, blob_start_words,
		k.params.application_id, k.params.application_data,
		&blob_size_words, &out_buf[blob_start_words], k.key);
	if (result != DCRYPTO_OK) {
		err = SBERR_UnknownError;
		goto cleanup;
	}
	if (attest == ATTEST_NO_KEY &&
	    (k.params.attrs.purpose_flags &
	     ((1 << KM_PURPOSE_SIGN) | (1 << KM_PURPOSE_ATTEST_KEY)))) {
		/* If no attestation key is provided and generated key is SIGN
		 * or ATTEST, use self-signed certificate.
		 */
		memcpy(k.attest_key.a, k.key, sizeof(k.attest_key));
		k.sign_params = k.params;
		attest = ATTEST_TRUE;
		if (!k.params.certificate_subject.p)
			k.params.certificate_subject = g_default_subject;
		if (!k.params.certificate_issuer.p)
			k.params.certificate_issuer =
				k.params.certificate_subject;
	}
	total_words = blob_start_words + blob_size_words;
	/* Total size of key blob in 32-bit words */
	*key_blob_size = total_words;
	cert_size = out_buf + total_words;
	if (attest == ATTEST_TRUE) {
		/**
		 * Tag::CERTIFICATE_SUBJECT the certificate subject.  The value
		 * is a DER encoded SEQUENCE. This value is used when
		 * generating a self signed certificates.  This tag may be
		 * specified during generateKey and importKey. If not provided
		 * the subject name shall default to CN="Android Keystore Key".
		 */
		if (!k.params.certificate_subject.p)
			k.params.certificate_subject = g_default_subject;
		if (!k.params.certificate_issuer.p) {
			err = SBERR_MissingIssuerSubject;
			goto cleanup;
		}
		*cert_size = SB_cert_name(
			&k.attest_key, &km->last_pk_x, &km->last_pk_y,
			&k.params, (uint8_t *)(cert_size + 1),
			(out_buf_words - total_words - 1) * sizeof(uint32_t));
		/* Prepend certificate with 32-bit little-endian size field. */
		total_words += (*cert_size + 3 + sizeof(*cert_size)) /
			       sizeof(uint32_t);
	} else if (attest == ATTEST_NO_KEY) {
		/* Add word for cert_size of 0 */
		total_words += 1;
		*cert_size = 0;
	}
	*out_words = total_words + 1; /* + blob size */
	err = SB_OK;
cleanup:
	always_memset(&k, 0, sizeof(k));
	return err;
};

/**
 * @brief Implements the core logic for IKeyMintDevice.generateKey().
 *
 * Parses key generation parameters, creates a hardware-backed key, and returns
 * it as an encrypted, versioned, and integrity-protected key blob.
 *
 * Input Buffer (`buf`):
 * A serialized list of Key parameters(tags).
 * - [ 4 bytes ] uint32_t tags_len_words: The size of the tag list that
 * follows, in 32-bit words.
 * - [ n bytes ] Serialized KeyMint tags. Each tag consists of a 32-bit tag
 * word followed by an optional, 32-bit aligned payload.
 *
 * Output Buffer (`buf`):
 * A KeyCreationResult structure containing the key blob and an empty
 * certificate chain.
 * - [ 4 bytes ] uint32_t key_struct cbor_blobotal_words: Total size of the key
 * blob and characteristics that follow, in 32-bit words.
 * - [ n bytes ] Key blob, containing HW and SW enforced KeyCharacteristics.
 * Note: that key blob include KeyCharacteristics as its part, but in AIDL
 * KeyCharacteristics are returned separately as key blob format is opaque.
 *
 * Hardware-Enforced Section:
 * out[0]: Security Level (always KM_SECURITY_STRONGBOX)
 * out[1]: Length of the hardware-enforced section in words (excluding out[0]
 * and out[1]) Subsequent words: Keymint tags that are hardware-enforced. These
 * tags have the same format as in the input buffer (tag word followed by data
 * payload).
 * Software-Enforced Section (starts immediately after the hardware-enforced
 * section):
 * out[x]: Security Level (always KM_SECURITY_KEYSTORE)
 * out[x + 1]: Length of the software-enforced section in words (excluding
 * out[x] and out[x + 1])
 * Subsequent words: Keymint tags that are software-enforced, using the
 * same format as above.
 * - [ m bytes ] Encrypted key material, prefixed with its size in bytes.
 * - [ 4 bytes ] uint32_t cert_chain_len_bytes: The size of the certificate
 * chain in bytes.
 *
 * @param km KeyMint context.
 * @param buf Input/Output buffer.
 * @param buf_size_words Size of the I/O buffer in 32-bit words.
 * @param req_len_words Size of the input data in 32-bit words.
 * @param out_len_bytes On success, the number of bytes written to the buffer.
 * @return SB_OK on success, or an error code on failure.
 */
static enum strongbox_error sb_GenerateKey(struct km *km, uint32_t *buf,
					   size_t buf_size_words,
					   size_t req_len_words,
					   size_t *out_len_bytes)
{
	uint32_t out_buf[512];
	size_t total_words = ARRAY_SIZE(out_buf);
	enum strongbox_error err;

	always_memset(out_buf, 0, sizeof(out_buf));
	err = generate_key_blob(km, buf, req_len_words, out_buf, &total_words,
				true);
	if (err != SB_OK)
		return err;

	if (total_words > buf_size_words)
		return SBERR_UnknownError;

	memcpy(buf, out_buf, total_words * sizeof(uint32_t));

	*out_len_bytes = total_words * sizeof(uint32_t);
	return SB_OK;
}

DECLARE_STRONGBOX_COMMAND(SB_DeviceGenerateKey, sb_GenerateKey);

/**
 * @brief Implements the core logic for IKeyMintDevice::begin().
 *
 * Starts a cryptographic operation (e.g., signing) with the provided key.
 *
 * Input Buffer (`buf`):
 * - [ 4 bytes ] enum km_purpose: The purpose of the operation (e.g., SIGN).
 * - [ 4 bytes ] uint32_t key_blob_words: The size of the key blob that
 * follows, in 32-bit words.
 * - [ n bytes ] The key blob generated by `generateKey`.
 * - [ 4 bytes ] uint32_t additional_params_words: The size of the
 * additional parameters list that follows, in 32-bit words.
 * - [ m bytes ] Serialized KeyMint tags for additional parameters
 * (e.g., APPLICATION_ID).
 *
 * Output Buffer (`buf`):
 * A BeginResult structure.
 * - [ 8 bytes ] uint64_t challenge: Currently unused, set to 0.
 * - [ 4 bytes ] uint32_t key_params_size_bytes: Size of KeyParameters that
 * follow. Currently 0.
 * - [ n bytes ] KeyParameters (e.g., IV). Currently empty.
 * - [ 4 bytes ] uint32_t operation_id: A unique handle for this operation.
 *
 * @param km KeyMint context.
 * @param buf Input/Output buffer.
 * @param buf_size_words Size of the I/O buffer in 32-bit words.
 * @param req_len_words Size of the input data in 32-bit words.
 * @param out_len_bytes On success, the number of bytes written to the buffer.
 * @return SB_OK on success, or an error code on failure.
 */
static enum strongbox_error sb_Begin(struct km *km, uint32_t *buf,
				     size_t buf_size_words,
				     size_t req_len_words,
				     size_t *out_len_bytes)
{
	uint32_t purpose, blob_words, params_words;
	struct km_key_params params = { 0 };
	enum strongbox_error err;
	uint32_t slot;
	bool unique = true;
	uint32_t operation_id;

	/* Minimum size of the input parameters: purpose, blob, sizes */
	if (req_len_words < 3 + KM_KEY_CHARACTERISTICS_WORDS)
		return SBERR_InvalidArgument;

	/* Minimum size of the output: challenge, key params, operation_id */
	if (buf_size_words < 4)
		return SBERR_InvalidArgument;

	/* Check if we have free slots for the operation. */
	if (km->used_slots == ((1U << KM_MAX_OPS) - 1))
		return SBERR_TooManyOperations;
	/* Find free slot */
	slot = count_trailing_zeros(~km->used_slots);

	purpose = buf[0];

	blob_words = buf[1];
	if (blob_words + 2 >= req_len_words)
		return SBERR_InvalidKeyBlob;

	params_words = buf[blob_words + 2];
	if (params_words > req_len_words ||
	    (params_words + blob_words + 2) > req_len_words)
		return SBERR_InvalidArgument;

	err = import_blob(km, buf + 2, blob_words, buf + blob_words + 3,
			  params_words, &params, km->ops[slot].key);
	if (err != SB_OK)
		return err;

	/* Check that requested purpose is compatible with the key. */
	if ((params.attrs.purpose_flags & (1 << purpose)) == 0)
		return SBERR_IncompatiblePurpose;

	/* We only support P256 for now */
	if (params.attrs.algorithm != KM_ALG_EC)
		return SBERR_UnsupportedAlgorithm;

	/* TODO: EC keys can be Sign or Agree */
	if (purpose != KM_PURPOSE_SIGN)
		return SBERR_IncompatiblePurpose;

	/* We should have only single flag - SHA256, SHA512 or NONE. */
	if (params.attrs.digest_flags != KM_DIGESTFLAG_SHA_2_256 &&
	    params.attrs.digest_flags != KM_DIGESTFLAG_SHA_2_512 &&
	    params.attrs.digest_flags != KM_DIGESTFLAG_NONE)
		return SBERR_IncompatibleDigest;

	/* Generate random and unique operation id. */
	do {
		if (!fips_rand_bytes(&operation_id, sizeof(operation_id)))
			return SBERR_UnknownError;
		for (size_t i = 0; i < ARRAY_SIZE(km->ops); i++)
			if (km->ops[i].operation_id == operation_id) {
				unique = false;
				break;
			};
	} while (!unique);

	/* Mark slot as allocated. */
	km->used_slots |= 1U << slot;
	km->ops[slot].attrs = params.attrs;

	/* Initialize hash if requested. */
	km->ops[slot].none_ctx.update_size = 0;
	if (params.attrs.digest_flags == KM_DIGESTFLAG_SHA_2_256)
		SHA256_sw_init(&km->ops[slot].sha256_ctx);
	else if (params.attrs.digest_flags == KM_DIGESTFLAG_SHA_2_512)
		SHA512_sw_init(&km->ops[slot].sha512_ctx);

	km->ops[slot].operation_id = operation_id;
	km->ops[slot].purpose = purpose;
	/* Unused challenge */
	buf[0] = 0;
	buf[1] = 0;
	/* Size of unused KeyParameters (IV, etc) */
	buf[2] = 0;
	/* Keymint Operation ID */
	buf[3] = operation_id;
	*out_len_bytes = 4 * sizeof(buf[0]);
	return SB_OK;
}

DECLARE_STRONGBOX_COMMAND(SB_DeviceBegin, sb_Begin);

/**
 * @brief Implements the core logic for IKeyMintOperation::update().
 *
 * Provides data to an ongoing cryptographic operation. For signing operations,
 * this data is added to the internal digest calculation.
 *
 * Input Buffer (`buf`):
 * - [ 4 bytes ] uint32_t operation_id: The handle from `begin()`.
 * - [ 4 bytes ] uint32_t update_size_bytes: The size of the data to process.
 * - [ n bytes ] The input data to be added to the operation.
 *
 * Output Buffer (`buf`):
 * - Empty. `*out_len_bytes` is set to 0.
 *
 * @param km KeyMint context.
 * @param buf Input/Output buffer.
 * @param buf_size_words Size of the I/O buffer in 32-bit words.
 * @param req_len_words Size of the input data in 32-bit words.
 * @param out_len_bytes On success, the number of bytes written to the buffer.
 * @return SB_OK on success, or an error code on failure.
 */
static enum strongbox_error sb_Update(struct km *km, uint32_t *buf,
				      size_t buf_size_words,
				      size_t req_len_words,
				      size_t *out_len_bytes)
{
	size_t op_index, update_size;

	if (req_len_words < 2)
		return SBERR_InvalidArgument;

	update_size = buf[1];
	if (update_size > (req_len_words - 2) * 4)
		return SBERR_InvalidArgument;

	for (op_index = 0; op_index < ARRAY_SIZE(km->ops); op_index++)
		if (km->ops[op_index].operation_id == buf[0])
			break;
	if (op_index >= ARRAY_SIZE(km->ops))
		return SBERR_InvalidOperationHandle;

	if (km->ops[op_index].attrs.digest_flags == KM_DIGESTFLAG_SHA_2_256) {
		SHA256_sw_update(&km->ops[op_index].sha256_ctx,
				 (uint8_t *)(buf + 2), update_size);
	} else if (km->ops[op_index].attrs.digest_flags ==
		   KM_DIGESTFLAG_SHA_2_512) {
		SHA512_sw_update(&km->ops[op_index].sha512_ctx,
				 (uint8_t *)(buf + 2), update_size);
	} else if (km->ops[op_index].attrs.digest_flags == KM_DIGESTFLAG_NONE) {
		if (update_size + km->ops[op_index].none_ctx.update_size >
		    sizeof(km->ops[op_index].none_ctx.update_context))
			return SBERR_InvalidArgument;
		memcpy((uint8_t *)(km->ops[op_index].none_ctx.update_context) +
			       km->ops[op_index].none_ctx.update_size,
		       buf + 2, update_size);
		km->ops[op_index].none_ctx.update_size += update_size;
	} else
		return SBERR_UnsupportedAlgorithm;

	return SB_OK;
}
DECLARE_STRONGBOX_COMMAND(SB_OperationUpdate, sb_Update);

/**
 * @brief Implements the core logic for IKeyMintOperation::finish().
 *
 * Finalizes a cryptographic operation. For a signing operation, it processes
 * the last piece of data, computes the final digest, signs it, and returns
 * the signature.
 *
 * Input Buffer (`buf`):
 * - [ 4 bytes ] uint32_t operation_id: The handle from `begin()`.
 * - [ 4 bytes ] uint32_t update_size_bytes: The size of the final data.
 * - [ n bytes ] The final input data for the operation. Can be empty.
 *
 * Output Buffer (`buf`):
 * The result of the operation. Specifically for the Strongbox-subset it is:
 * - [ 64 bytes ] An ECDSA P-256 signature, composed of the 32-byte 'r'
 * value followed by the 32-byte 's' value.
 *
 * @param km KeyMint context.
 * @param buf Input/Output buffer.
 * @param buf_size_words Size of the I/O buffer in 32-bit words.
 * @param req_len_words Size of the input data in 32-bit words.
 * @param out_len_bytes On success, the number of bytes written to the buffer.
 * @return SB_OK on success, or an error code on failure.
 */
static enum strongbox_error sb_Finish(struct km *km, uint32_t *buf,
				      size_t buf_size_words,
				      size_t req_len_words,
				      size_t *out_len_bytes)
{
	size_t op_index, update_size;
	enum dcrypto_result result;
	p256_int p256_digest, p256_r, p256_s;
	const uint8_t *sign_data = NULL;

	/* Minimum 2 words - Operation Handler and Input Size */
	if (req_len_words < 2)
		return SBERR_InvalidArgument;
	update_size = buf[1];
	if (update_size > (req_len_words - 2) * 4)
		return SBERR_InvalidArgument;
	if (buf_size_words < ECDSA_SIG_BYTES / sizeof(uint32_t))
		return SBERR_InvalidArgument;

	for (op_index = 0; op_index < ARRAY_SIZE(km->ops); op_index++)
		if (km->ops[op_index].operation_id == buf[0])
			break;
	if (op_index >= ARRAY_SIZE(km->ops))
		return SBERR_InvalidOperationHandle;

	/* Cleanup slot if found, early, so it is freed on error too. */
	km->used_slots &= ~(1u << op_index);

	if (km->ops[op_index].attrs.algorithm != KM_ALG_EC)
		return SBERR_UnsupportedAlgorithm;

	if (km->ops[op_index].attrs.digest_flags == KM_DIGESTFLAG_SHA_2_256) {
		SHA256_sw_update(&km->ops[op_index].sha256_ctx,
				 (uint8_t *)(buf + 2), update_size);
		sign_data = SHA256_sw_final(&km->ops[op_index].sha256_ctx)->b8;
	} else if (km->ops[op_index].attrs.digest_flags ==
		   KM_DIGESTFLAG_SHA_2_512) {
		SHA512_sw_update(&km->ops[op_index].sha512_ctx,
				 (uint8_t *)(buf + 2), update_size);
		/* Truncate to 32 bytes implicitly */
		sign_data = SHA512_sw_final(&km->ops[op_index].sha512_ctx)->b8;
	} else if (km->ops[op_index].attrs.digest_flags == KM_DIGESTFLAG_NONE) {
		if (update_size + km->ops[op_index].none_ctx.update_size >
		    sizeof(km->ops[op_index].none_ctx.update_context))
			return SBERR_InvalidArgument;

		memcpy((uint8_t *)(km->ops[op_index].none_ctx.update_context) +
			       km->ops[op_index].none_ctx.update_size,
		       buf + 2, update_size);
		km->ops[op_index].none_ctx.update_size += update_size;
		sign_data =
			(uint8_t *)km->ops[op_index].none_ctx.update_context;

	} else
		return SBERR_UnsupportedAlgorithm;

	p256_from_bin(sign_data, &p256_digest);
	result = DCRYPTO_p256_ecdsa_sign((p256_int *)km->ops[op_index].key,
					 &p256_digest, &p256_r, &p256_s);
	p256_to_bin(&p256_r, (uint8_t *)&buf[0]);
	p256_to_bin(&p256_s, (uint8_t *)&buf[8]);

	if (result != DCRYPTO_OK)
		return SBERR_UnknownError;

	/* Clean up everything by operation id */
	always_memset(&km->ops[op_index].attrs, 0,
		      sizeof(km->ops[op_index]) -
			      offsetof(struct km_operation, attrs));
	*out_len_bytes = ECDSA_SIG_BYTES;
	return SB_OK;
}

DECLARE_STRONGBOX_COMMAND(SB_OperationFinish, sb_Finish);

#define U32(a, b, c, d) (((a) | (b << 8) | (c << 16) | (d << 24)))

/**
 *
 * Definition of the command arguments from AIDL:
 * @param out MacedPublicKey macedPublicKey contains the public key of the
 * generated key pair,    MACed so that generateCertificateRequest can easily
 * verify, without the privateKeyHandle, that the contained public key is for
 * remote certification.
 *
 * @return data representing a handle to the private key. The format is
 * implementation-defined,  but note that specific services may define a
 * required format.  KeyMint does.
 *
 * byte[] generateEcdsaP256KeyPair(in boolean testMode, out MacedPublicKey
 *  macedPublicKey);
 *
 * The data returned by this function is a concatenation of two parts:
 * 1. A handle to the private key, which is a key blob.
 * 2. The MacedPublicKey.
 *
 * The format is as follows, with all fields being 32-bit aligned:
 *
 * - [ variable size ] Key Blob: A handle to the private key. The first 4 bytes
 *   of the key blob contain its total size in 32-bit words.
 *
 * - [ 4 bytes ] MacedPublicKey Length: The size of the MacedPublicKey data in
 *   bytes.
 *
 * - [ variable size ] MacedPublicKey Data: The CBOR-encoded MacedPublicKey.
 */
static enum strongbox_error sb_GenerateKeyPair(struct km *km, uint32_t *buf,
					       size_t buf_size_words,
					       size_t req_len_words,
					       size_t *out_len_bytes)
{
	static const uint32_t attest_key_params[] = {
		17,
		KM_TAG(KM_TAG_PURPOSE, 0),
		KM_PURPOSE_ATTEST_KEY,
		KM_TAG(KM_TAG_ALGORITHM, 0),
		KM_ALG_EC,
		KM_TAG(KM_TAG_KEY_SIZE, 0),
		256,
		KM_TAG(KM_TAG_EC_CURVE, 0),
		KM_EC_CURVE_P_256,
		KM_TAG(KM_TAG_NO_AUTH_REQUIRED, 0),
		KM_TAG(KM_TAG_DIGEST, 0),
		KM_DIGEST_SHA_2_256,
		KM_TAG(KM_TAG_CERTIFICATE_NOT_BEFORE, 0),
		/* We use Unix epoch as the start date of an undefined
		 * certificate validity period.
		 */
		0,
		0,
		KM_TAG(KM_TAG_CERTIFICATE_NOT_AFTER, 0),
		/* Per RFC 5280 4.1.2.5, an undefined expiration (not-after)
		 * field should be set to 9999-12-31T23:59:59Z, which is
		 * 253402300799000 = 0xE677 D21FD818
		 */
		0xD21FD818,
		0xE677,
	};
	size_t total_words = buf_size_words;
	uint32_t *b32;
	uint8_t *b8;

	enum strongbox_error err;
	struct hmac_sha256_ctx sha;
	enum dcrypto_result result;
	const struct sha256_digest *digest;

	err = generate_key_blob(km, attest_key_params,
				ARRAY_SIZE(attest_key_params), buf,
				&total_words, false);
	if (err != SB_OK)
		return err;

	/*; - P256 is BE: https://www.secg.org/sec1-v2.pdf#page=19
	 * (section 2.3.7) PublicKey = {               ; COSE_Key [RFC9052 s7]
	 *     1 : 2,                  ; Key type : EC2
	 *     3 : -7,                 ; Algorithm : ES256
	 *    -1 : 1,                 ; Curve : P256
	 *    -2 : bstr,              ; X coordinate, big-endian
	 *    -3 : bstr,              ; Y coordinate, big-endian
	 * }
	 * Size of of the CBOR encoding of the `PublicKey`
	 */
#define CBOR_PUBLIC_KEY_LEN (13 + ECDSA_SIG_BYTES)
#define CBOR_PUBLIC_KEY_WORDS \
	((CBOR_PUBLIC_KEY_LEN + sizeof(uint32_t) - 1) / sizeof(uint32_t))

	/**
	 * MacedPublicKey = [                     ; COSE_Mac0 [RFC9052 s6.2]
	 *      protected: bstr .cbor { 1 : 5},    ; Algorithm : HMAC-256
	 *      unprotected: { },
	 *      payload : bstr .cbor PublicKey,
	 *      tag : bstr ; HMAC-256(K_mac, MAC_structure)
	 * ]
	 * Size of the CBOR encoding of the MacedPublicKey
	 */
#define CBOR_MACED_SIGNED_LEN (CBOR_PUBLIC_KEY_LEN + 10)
#define CBOR_MACED_KEY_LEN    (CBOR_MACED_SIGNED_LEN + SHA256_DIGEST_SIZE)
#define CBOR_MACED_KEY_WORDS \
	((CBOR_MACED_KEY_LEN + sizeof(uint32_t) - 1) / sizeof(uint32_t))

	/* After the key blob add MacedPublicKey, but check we have space. */
	if (total_words + 1 + CBOR_MACED_KEY_WORDS > buf_size_words)
		return SBERR_UnknownError;
	always_memset(buf + total_words, 0,
		      (1 + CBOR_MACED_KEY_WORDS) * sizeof(uint32_t));

	/* Place the length of the Mac'ed key in bytes.	 */
	buf[total_words] = CBOR_MACED_KEY_LEN;

	b32 = buf + total_words + 1;
	b8 = (uint8_t *)b32;
	b32[0] = U32(CBOR_HDR1(CBOR_MAJOR_ARR, 4),
		     CBOR_HDR1(CBOR_MAJOR_BSTR, 3),
		     CBOR_HDR1(CBOR_MAJOR_MAP, 1), CBOR_UINT0(1));
	b32[1] = U32(CBOR_UINT0(5),
		     /* unprotected: { } */
		     CBOR_HDR1(CBOR_MAJOR_MAP, 0),
		     CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1),
		     CBOR_PUBLIC_KEY_LEN);
	b32[2] = U32(/* Public key, Map header: 5 entries*/
		     CBOR_HDR1(CBOR_MAJOR_MAP, 5), COSE_KEY_LABEL_KTY,
		     CBOR_UINT0(2), /* EC2 */
		     COSE_KEY_LABEL_ALG);
	b32[3] = U32(CBOR_NINT0(-7), /* ECDSA w/SHA-256 */
		     CBOR_NINT0(-1), CBOR_UINT0(1) /* -1 : 1 P256 */,
		     CBOR_NINT0(-2));

	b32[4] = U32(CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1),
		     32 /* -2 : bstr(32)*/, 0, 0);

	p256_to_bin(&km->last_pk_x, b8 + 18);
	b8[50] = CBOR_NINT0(-3); /* Y coordinate */
	b8[51] = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1);
	b8[52] = 32;
	p256_to_bin(&km->last_pk_y, b8 + 53);
	/* tag */
	b8[85] = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1);
	b8[86] = SHA256_DIGEST_SIZE;

	/* Add an HMAC tag for integrity */
	result = DCRYPTO_hw_hmac_sha256_init(&sha, km->c.hmac_tag_key,
					     sizeof(km->c.hmac_tag_key));
	if (result != DCRYPTO_OK)
		return SBERR_UnknownError;

	HMAC_SHA256_update(&sha, b32, CBOR_MACED_SIGNED_LEN);
	digest = HMAC_SHA256_final(&sha);

	memcpy(b8 + CBOR_MACED_SIGNED_LEN, digest->b8, SHA256_DIGEST_SIZE);
	*out_len_bytes = /* key blob, 4-byte len, MACed Key */
		(total_words + 1 + CBOR_MACED_KEY_WORDS) * sizeof(uint32_t);
	return err;
}

DECLARE_STRONGBOX_COMMAND(SB_RpcGenerateEcdsaP256KeyPair, sb_GenerateKeyPair);

/**
 * Implementation of the GSC specific GetDiceChain command.
 *
 * Due to limits on the TPM command size and the fact that DICE chain is a
 * constant for given firmware/device, move it out of the
 * GenerateCertificateV2Request to separate command which result can be cached
 * by the Strongbox TA.
 * This command doesn't take any arguments and returns DICE chain in CBOR
 * encoding as is.
 *
 * @param km KeyMint context.
 * @param buf Input/Output buffer.
 * @param buf_size_words Size of the I/O buffer in 32-bit words.
 * @param req_len_words Size of the input data in 32-bit words.
 * @param out_len_bytes On success, the number of bytes written to the buffer.
 * @return SB_OK on success, or an error code on failure.
 */
enum strongbox_error sb_GetDiceChain(struct km *km, uint32_t *buf,
				     size_t buf_size_words,
				     size_t req_len_words,
				     size_t *out_len_bytes)
{
	/* No arguments are expected for the command. */
	if (req_len_words)
		return SBERR_InvalidArgument;

	/* DiceCertChain */
	*out_len_bytes = get_dice_chain_bytes_for_chain(
		(uint8_t *)buf, 0, buf_size_words * sizeof(uint32_t),
		BOOT_PARAM_DICE_CHAIN_GSC);
	return (*out_len_bytes) ? SB_OK : SBERR_UnknownError;
}
DECLARE_STRONGBOX_COMMAND(SB_GetDiceChain, sb_GetDiceChain);

#define DEVICE_INFO_MAX_FIELDS 14
#define MAX_FIELD_SIZE	       2048

struct cbor_blob {
	uint8_t len;
	uint8_t data[17]; /* Fits longest key/value in this schema */
};

/* Values: vb_state */
static const struct cbor_blob VAL_VB[] = {
	{ /* 0 - "green" */ 6,
	  { CBOR_HDR1(CBOR_MAJOR_TSTR, 5), 'g', 'r', 'e', 'e', 'n' } },
	{ /* 1 - "yellow" */ 7,
	  { CBOR_HDR1(CBOR_MAJOR_TSTR, 6), 'y', 'e', 'l', 'l', 'o', 'w' } },
	{ /* 2 - "orange" */ 7,
	  { CBOR_HDR1(CBOR_MAJOR_TSTR, 6), 'o', 'r', 'a', 'n', 'g', 'e' } }
};

/* Values: bootloader_state */
static const struct cbor_blob VAL_BOOT[] = {
	{ /* 0 - "locked" */ 7,
	  { CBOR_HDR1(CBOR_MAJOR_TSTR, 6), 'l', 'o', 'c', 'k', 'e', 'd' } },
	{ /* 1 - "unlocked" */ 9,
	  { CBOR_HDR1(CBOR_MAJOR_TSTR, 8), 'u', 'n', 'l', 'o', 'c', 'k', 'e',
	    'd' } }
};

/* Values: security_level */
static const struct cbor_blob VAL_SEC_SB = { 10,
					     { CBOR_HDR1(CBOR_MAJOR_TSTR, 9),
					       's', 't', 'r', 'o', 'n', 'g',
					       'b', 'o', 'x' } };

static const struct enforce_keys {
	struct cbor_blob key;
	size_t val_count;
	const struct cbor_blob *vals;
} ENFORCE_VALUES[] = {

	{ /* 0 - `vb_state` key */ { 9,
				     { CBOR_HDR1(CBOR_MAJOR_TSTR, 8), 'v', 'b',
				       '_', 's', 't', 'a', 't', 'e' } },
	  3, VAL_VB },

	{ /* 1 - `bootloader_state` key */ {
		  17,
		  { CBOR_HDR1(CBOR_MAJOR_TSTR, 16), 'b', 'o', 'o', 't', 'l',
		    'o', 'a', 'd', 'e', 'r', '_', 's', 't', 'a', 't', 'e' } },
	  2, VAL_BOOT },

	{ /* 2- `security_level` key */ { 15,
					  { CBOR_HDR1(CBOR_MAJOR_TSTR, 14), 's',
					    'e', 'c', 'u', 'r', 'i', 't', 'y',
					    '_', 'l', 'e', 'v', 'e', 'l' } },
	  1, &VAL_SEC_SB },
};

enum device_info_key {
	DEVICE_INFO_KEY_OTHER = 0,
	/* Values are indices in ENFORCE_VALUES[] + 1 */
	DEVICE_INFO_KEY_VB_STATE = 1,
	DEVICE_INFO_KEY_BOOTLOADER_STATE = 2,
	DEVICE_INFO_KEY_SECURITY_LEVEL = 3,
};

/* Parse CBOR type and length/value */
static size_t parse_head(const uint8_t *ptr, const uint8_t *end,
			 size_t *payload_len)
{
	size_t head_sz = 0, payload_sz = 0;
	uint8_t ai;

	if (ptr >= end)
		return head_sz;

	ai = *ptr & 0x1F;

	if (ai < 24) {
		payload_sz = ai;
		head_sz = 1;
	} else if (ai == 24 && ptr + 1 < end) {
		payload_sz = ptr[1];
		head_sz = 2;
	} else if (ai == 25 && ptr + 2 < end) {
		payload_sz = ((size_t)ptr[1] << 8) | ptr[2];
		head_sz = 3;
	} else if (ai == 26 && ptr + 4 < end) {
		/* For whatever reason we can get this encoding from AP... */
		payload_sz = ((size_t)ptr[1] << 24) | ((size_t)ptr[2] << 16) |
			     ((size_t)ptr[3] << 8) | ptr[4];
		head_sz = 5;
	}
	*payload_len = payload_sz;
	return head_sz;
}

/* Handles shifting tail and overwriting. Updates `len` and `end`. */
static bool patch_blob(uint8_t *dest, size_t current_sz,
		       const struct cbor_blob *new_blob, uint8_t *buf_start,
		       size_t *total_len, size_t cap, const uint8_t **end_ptr)
{
	int32_t diff = (int32_t)new_blob->len - (int32_t)current_sz;

	if (diff > 0 && (*total_len + diff) > cap)
		return false;

	if (diff != 0) {
		/* dest + new_blob->len is where the tail SHOULD start
		 * dest + current_sz is where the tail CURRENTLY starts
		 * We move the tail from CURRENT to SHOULD.
		 * Size of tail = (*end_ptr) - (dest + current_sz)
		 */
		memmove(dest + new_blob->len, dest + current_sz,
			(size_t)(*end_ptr - (dest + current_sz)));
		*total_len += diff;
		*end_ptr += diff;
	}
	/* Insert new value in-place */
	memcpy(dest, new_blob->data, new_blob->len);
	return true;
}

/**
 * @brief Validation of CBOR DeviceInfo struct
 *
 * @param buf [in/out] buffer with DeviceInfo
 * @param len [in] length of DeviceInfo
 * @param cap [in] max space in `buf` to edit
 * @param new_len [out] resulting length of DeviceInfo
 * @param hw_vb_idx [in] VB state (0 - green/1 -yellow/2-orange)
 * @param hw_boot_idx [in] Bootloader state (0 - locked/1-unlocked)
 * @return true on success, false if DeviceInfo is invalid
 */
static bool process_device_info(uint8_t *buf, size_t len, size_t cap,
				size_t *new_len, uint8_t hw_vb_idx,
				uint8_t hw_boot_idx)
{
	uint8_t *ptr = buf;
	uint8_t *map_head_ptr = ptr; /* Save for updating count later */

	const uint8_t *end = buf + len;
	size_t sz, head_sz, map_count;
	uint8_t found_keys = 0; /* Flags for found keys */

	/* Prepare HW reported levels to match known keys for lookup */
	uint8_t hw_lvls[ARRAY_SIZE(ENFORCE_VALUES)] = { hw_vb_idx, hw_boot_idx,
							0 };

	/* 1. Root Map Check
	 * Strict: Must be map, max 14 fields.
	 * We also check if adding 3 fields would overflow the 1-byte header
	 * limit (23). 14 + 3 = 17 <= 23, so header size is constant (1 byte).
	 */
	if ((*ptr & 0xE0) != CBOR_MAJOR_MAP)
		return false;
	map_count = *ptr & 0x1f;
	if (map_count > DEVICE_INFO_MAX_FIELDS)
		return false;

	ptr++;

	/* 2. Iterate Existing Fields */
	for (size_t i = 0; i < map_count; i++) {
		/* --- Key --- */
		uint8_t *key_start = ptr, *val_start, major;
		enum device_info_key target;
		size_t val_full_sz;

		head_sz = parse_head(ptr, end, &sz);
		if (head_sz == 0 || ptr + head_sz + sz > end)
			return false;

		/* Allowed Key Types: TSTR(3) or UINT(0) */
		if ((*ptr & 0xE0) != CBOR_MAJOR_TSTR &&
		    (*ptr & 0xE0) != CBOR_MAJOR_UINT)
			return false;

		ptr += head_sz + sz; /* Move to Value */

		/* Identify Key */
		target = DEVICE_INFO_KEY_OTHER;
		for (size_t j = 0; j < ARRAY_SIZE(ENFORCE_VALUES); j++) {
			const struct cbor_blob *key = &ENFORCE_VALUES[j].key;

			if (memcmp(key_start, key->data, key->len) == 0) {
				target = j + 1;
				break;
			}
		}

		/* --- Value --- */
		val_start = ptr;
		head_sz = parse_head(ptr, end, &sz);
		if (head_sz == 0)
			return false;

		major = *val_start & 0xE0;
		val_full_sz = head_sz;
		if (major == CBOR_MAJOR_BSTR || major == CBOR_MAJOR_TSTR)
			val_full_sz += sz;

		if (val_start + val_full_sz > end)
			return false;

		/* --- Logic: Ratchet & Correction --- */
		if (target != 0) {
			const struct cbor_blob *new_blob = NULL;
			int curr_idx = -1; /* unknown key */
			int limit = ENFORCE_VALUES[target - 1].val_count;
			const struct cbor_blob *table =
				ENFORCE_VALUES[target - 1].vals;
			int hw_lvl = hw_lvls[target - 1];

			found_keys |= 1U << (target - 1);

			/* Identify current level */
			for (int k = 0; k < limit; k++) {
				/* memcmp() will start with checking embedded
				 * length first, so we don't need to check
				 * length explicitly.
				 */
				if (memcmp(val_start, table[k].data,
					   val_full_sz) == 0) {
					curr_idx = k;
					break;
				}
			}

			/* Ratchet: If unknown, or claiming safer than HW ->
			 * Patch to HW.
			 */
			if (curr_idx < hw_lvl)
				new_blob = &table[hw_lvl];

			/* Execute Patch if needed */
			if (new_blob) {
				if (!patch_blob(val_start, val_full_sz,
						new_blob, buf, &len, cap, &end))
					return false;
				/* Update for pointer advancement */
				val_full_sz = new_blob->len;
			}
		}

		ptr = val_start + val_full_sz;
	}

	/* 3. Append Missing Fields. `ptr` is now at 'end'. */
	for (size_t j = 0; j < ARRAY_SIZE(ENFORCE_VALUES); j++) {
		const struct cbor_blob *key, *val;

		if (found_keys & (1U << j))
			continue;

		key = &ENFORCE_VALUES[j].key;
		val = &ENFORCE_VALUES[j].vals[hw_lvls[j]];

		/* Check if we have enough space to add. */
		if (len + key->len + val->len > cap)
			return false;

		memcpy(ptr, key->data, key->len);
		ptr += key->len;
		memcpy(ptr, val->data, val->len);
		ptr += val->len;
		len += (key->len + val->len);
		map_count++;
	}

	/* 4. Update Header
	 * Since map_count <= 14+3=17, it fits in low 5 bits.
	 * Header byte remains (Major 5 | count).
	 */
	*map_head_ptr = CBOR_MAJOR_MAP | (uint8_t)map_count;

	*new_len = len;
	return true;
}

/**
 * Implementation for GenerateCertificateV2Request command.
 *
 * @param km KeyMint context.
 * @param buf Input/Output buffer.
 * @param buf_size_words Size of the I/O buffer in 32-bit words.
 * @param req_len_words Size of the input data in 32-bit words.
 * @param out_len_bytes On success, the number of bytes written to the buffer.
 * @return SB_OK on success, or an error code on failure.
 *
 * generateCertificateRequestV2 creates a certificate signing request to be sent
 * to the provisioning server. Implements:
 * byte[] generateCertificateRequestV2(in MacedPublicKey[] keysToSign, in byte[]
 * challenge);
 *
 * Input Buffer (`buf`) Format:
 * The input buffer is a sequence of fields, all 32-bit aligned.
 * - [ 4 bytes ] uint32_t key_count: Number of MACed public keys.
 * @param in MacedPublicKey[] keysToSign contains the set of keys to certify.
 * The IRemotelyProvisionedComponent must validate the MACs on each key.  If any
 * entry in the array lacks a valid MAC, the method must return
 * STATUS_INVALID_MAC.  This method must not accept test keys. If any entry in
 * the array is a test key, the method must return
 * - [ n times ] Repeated for each key:
 *   - [ 4 bytes ] uint32_t maced_key_len_bytes: Length of the following
 *     MACed key.
 *   - [ m bytes ] uint8_t maced_key[]: The MACed public key data, padded
 *     to a 4-byte boundary.
 *        STATUS_TEST_KEY_IN_PRODUCTION_REQUEST.
 * @param in challenge contains a byte string from the provisioning server which
 * will be included in the signed data of the CSR structure. Different
 * provisioned backends may use different semantic data for this field, but the
 * supported sizes must be between 0 and 64 bytes, inclusive.
 * - [ 4 bytes ] uint32_t challenge_len_bytes: Length of the challenge.
 * - [ p bytes ] uint8_t challenge[]: The challenge data, padded to a 4-byte
 *   boundary.
 * Strongbox-subset specific:
 * - [ 4 bytes ] uint32_t device_info_len_bytes: Length of the DeviceInfo.
 * - [ q bytes ] uint8_t device_info[]: The CBOR-encoded DeviceInfo data.
 *   In the DeviceInfo map fields:
 *    - "security_level": "strongbox",
 *    - "vb_state": "green",
 *    - "bootloader_state": "locked"
 * have to be placed last, as future version would append these fields based
 * on the actual GSC state.
 * generateCertificateRequestV2 creates a SignedData<[challenge,
 * CSR]> part of the CSR to be sent to the provisioning server.
 * This is part need to be prepended with the
 * AuthenticatedRequest<CsrPayload> structure
 * [ version: 1, UdsCerts, DiceCertChain,  {SignedData<Data>} ]
 *
 * @return a CBOR Certificate Signing Request (Csr) Signed Data
 * serialized into a byte array.
 *         See generateCertificateRequestV2.cddl for CDDL definitions.
 *
 */
static inline struct slice_ref_s slice(const size_t size, const uint8_t *data)
{
	return (struct slice_ref_s){ .data = data, .size = size };
}
#define MAX_CSR_PUB_KEYS 20
static enum strongbox_error sb_GenerateCertificateReq(struct km *km,
						      uint32_t *buf,
						      size_t buf_size_words,
						      size_t req_len_words,
						      size_t *out_len_bytes)
{
	size_t key_count, maced_len, device_info_len;
	enum dcrypto_result result;
	const struct sha256_digest *digest;
	struct hmac_sha256_ctx sha;
	uint32_t public_key[MAX_CSR_PUB_KEYS][CBOR_PUBLIC_KEY_WORDS];
	uint32_t challenge[KM_MAX_CHALLENGE_WORDS], challenge_words,
		challenge_len;
	uint8_t *b8, *data_to_sign, *data_start;
	size_t index, data_len, data_len_csr, data_len_to_sign, prefix_bytes;
	static const uint8_t kSigStructFixedHdr[] = {
		/* Array header: 4 elements */
		CBOR_HDR1(CBOR_MAJOR_ARR, 4),
		/* 1. Context: tstr("Signature1") */
		CBOR_HDR1(CBOR_MAJOR_TSTR, CDI_SIG_STRUCT_CONTEXT_VALUE_LEN),
		'S',
		'i',
		'g',
		'n',
		'a',
		't',
		'u',
		'r',
		'e',
		'1',
		/* 2. Body protected: bstr(COSE param) */
		/* BSTR of size 3 - see the rest of the struct */
		CBOR_HDR1(CBOR_MAJOR_BSTR, 3),
		/* Map header: 1 elem */
		CBOR_HDR1(CBOR_MAJOR_MAP, 1),
		/* Alg: uint(1) => nint(-7) */
		COSE_PARAM_LABEL_ALG,
		CBOR_NINT0(-7), /* ECDSA w/ SHA-256 */
		/* 3. External AAD: Bstr(0 bytes) */
		CBOR_HDR1(CBOR_MAJOR_BSTR, 0),
	};

	/* We should have at least key count + challenge len + DeviceInfo len */
	if (req_len_words < 3)
		return SBERR_InvalidArgument;

	key_count = buf[0];
	if (key_count > MAX_CSR_PUB_KEYS)
		return SBERR_InvalidArgument;

	/* Req is big enough to contain key count, all keys and challenge len */
	if (1 + key_count * (CBOR_MACED_KEY_WORDS + 1) >= req_len_words)
		return SBERR_InvalidArgument;

	/* Check that we have at least space for all the keys */
	if (key_count * (CBOR_PUBLIC_KEY_WORDS + 1) > buf_size_words)
		return SBERR_InvalidArgument;

	index = 1;
	for (size_t i = 0; i < key_count; i++) {
		maced_len = buf[index];

		/* Only support MAC'ed keys we produce with fixed size */
		if (maced_len != CBOR_MACED_KEY_LEN)
			return SBERR_InvalidArgument;

		b8 = (uint8_t *)(buf + index + 1);

		/* Check HMAC tag for integrity */
		result = DCRYPTO_hw_hmac_sha256_init(
			&sha, km->c.hmac_tag_key, sizeof(km->c.hmac_tag_key));
		if (result != DCRYPTO_OK)
			return SBERR_UnknownError;

		HMAC_SHA256_update(&sha, b8, CBOR_MACED_SIGNED_LEN);
		digest = HMAC_SHA256_final(&sha);

		if (memcmp(digest->b8, b8 + CBOR_MACED_SIGNED_LEN,
			   SHA256_DIGEST_SIZE) != 0)
			return SBERR_RKP_STATUS_INVALID_MAC;

		/* Copy public key structure locally */
		memcpy(public_key[i], b8 + 8, CBOR_PUBLIC_KEY_LEN);
		index += CBOR_MACED_KEY_WORDS + 1;
	}

	/* Already checked that req is big enough to contain challenge len */
	challenge_len = buf[index];
	challenge_words =
		(challenge_len + sizeof(uint32_t) - 1) / sizeof(uint32_t);
	/* Req is big enough to contain challenge (w/ len) + DeviceInfo len */
	if (challenge_len > KM_MAX_CHALLENGE_LEN ||
	    (challenge_words + index + 1 >= req_len_words))
		return SBERR_InvalidArgument;

	memcpy(challenge, buf + index + 1, challenge_len);
	index += challenge_words + 1;

	/* Already checked that req is big enough to contain DeviceInfo len */
	device_info_len = buf[index];

	if (device_info_len / sizeof(uint32_t) + index > req_len_words)
		return SBERR_InvalidArgument;

	if (!process_device_info(
		    (uint8_t *)(buf + index + 1), device_info_len,
		    (buf_size_words - index - 1) * sizeof(uint32_t),
		    &device_info_len,
		    /* vb state */ get_boot_mode() == BOOT_MODE_NORMAL ? 0 : 2,
		    /* bootloader */ ccd_get_state() == CCD_STATE_LOCKED ? 0 :
									   1))
		return SBERR_RKP_STATUS_FAILED;

	/**
	 * Csr = AuthenticatedRequest<CsrPayload>
	 *
	 * AuthenticatedRequest<T> = [
	 *    version: 1,  ; The AuthenticatedRequest CDDL Schema version.
	 *    UdsCerts,
	 *    DiceCertChain,
	 *    --- we only provide SignedData part
	 *    SignedData<[
	 *       challenge: bstr .size (0..64),
	 *       bstr .cbor T,
	 *    ]>,
	 * ];
	 *
	 *  ; COSE_Sign1 (untagged) [RFC9052 s4.2]
	 * SignedData<Data> = [
	 *     protected: bstr .cbor { 1 : AlgorithmES256 },
	 *     unprotected: {},
	 *     payload: bstr .cbor Data / nil,
	 * ; ECDSA(CDI_Leaf_Priv, SignedDataSigStruct<Data>)
	 *     signature: bstr
	 * ]
	 */
	buf[0] = U32(
		/* SignedData<Data>, 4 elements array */
		CBOR_HDR1(CBOR_MAJOR_ARR, 4),
		/* bstr .cbor { 1 : AlgorithmES256 } 43 A10126 */
		CBOR_HDR1(CBOR_MAJOR_BSTR, 3), CBOR_HDR1(CBOR_MAJOR_MAP, 1),
		CBOR_HDR1(CBOR_MAJOR_UINT, 1));
	b8 = (uint8_t *)(buf + 1);
	*b8++ = CBOR_NINT0(-7);
	/* unprotected: {} */
	*b8++ = CBOR_HDR1(CBOR_MAJOR_MAP, 0);
	prefix_bytes = 6;

	/*
	 * CsrPayload = [
	 *   version: 3, CertificateType: tstr, ; "keymint" - 10 bytes
	 *   DeviceInfo - device_info_len
	 *   KeysToSign: [ *PublicKey] - 1 + key_count * CBOR_PUBLIC_KEY_LEN
	 * ]
	 */
	data_len_csr =
		10 + device_info_len + 1 + key_count * CBOR_PUBLIC_KEY_LEN;
	/* Calculate length of the payload for .bstr wrapping
	 * payload: bstr .cbor Data / nil,
	 * Data is array [challenge, .bstr CsrPayload]
	 * +1 byte for 2-value array, +2 for challenge .bstr len,
	 * +2 for CsrPayload .bstr (min)
	 */
	data_len = 1 + 2 + challenge_len + data_len_csr + 2;
	/* Account for the .bstr len in the CsrPayload*/
	if (data_len_csr > 255)
		data_len++;

	if (data_len > 255) {
		*b8++ = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES2);
		*b8++ = (uint8_t)(((data_len) & 0xFF00) >> 8);
		*b8++ = (uint8_t)((data_len) & 0x00FF);
		prefix_bytes += 3;
	} else {
		*b8++ = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1);
		*b8++ = data_len;
		prefix_bytes += 2;
	}
	data_to_sign = b8;
	/* Now temporarily put here prefix of SignedDataSigStruct<Data> */
	/* ; Sig_structure for SignedData [ RFC9052 s4.4]
	 * SignedDataSigStruct<Data> = [
	 *     context: "Signature1",
	 *     protected: bstr .cbor { 1 : AlgorithmES256 },
	 *     external_aad: bstr .size 0,
	 *     payload: bstr .cbor Data / nil,
	 * ]
	 */

	memcpy(b8, kSigStructFixedHdr, sizeof(kSigStructFixedHdr));
	b8 += sizeof(kSigStructFixedHdr);
	if (data_len > 255) {
		*b8++ = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES2);
		*b8++ = (uint8_t)(((data_len) & 0xFF00) >> 8);
		*b8++ = (uint8_t)((data_len) & 0x00FF);
		data_len_to_sign = data_len + sizeof(kSigStructFixedHdr) + 3;
	} else {
		*b8++ = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1);
		*b8++ = data_len;
		data_len_to_sign = data_len + sizeof(kSigStructFixedHdr) + 2;
	}
	data_start = b8;
	/* challenge: bstr .size (0..64) */
	*b8++ = CBOR_HDR1(CBOR_MAJOR_ARR, 2);
	*b8++ = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1);
	*b8++ = challenge_len;
	memcpy(b8, challenge, challenge_len);
	b8 += challenge_len;
	/* Wrap CsrPayload into .bstr */
	if (data_len_csr > 255) {
		*b8++ = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES2);
		*b8++ = (uint8_t)(((data_len_csr) & 0xFF00) >> 8);
		*b8++ = (uint8_t)((data_len_csr) & 0x00FF);
	} else {
		*b8++ = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1);
		*b8++ = data_len_csr;
	}

	/*
	 * CsrPayload = [
	 *   version: 3, CertificateType: tstr, ; "keymint"
	 *   DeviceInfo, KeysToSign,
	 * ]
	 */
	*b8++ = CBOR_HDR1(CBOR_MAJOR_ARR, 4);
	*b8++ = CBOR_HDR1(CBOR_MAJOR_UINT, 3);
	*b8++ = CBOR_HDR1(CBOR_MAJOR_TSTR, 7);
	*b8++ = 'k';
	*b8++ = 'e';
	*b8++ = 'y';
	*b8++ = 'm';
	*b8++ = 'i';
	*b8++ = 'n';
	*b8++ = 't';

	/*
	 * DeviceInfo = {
	 * "brand" : tstr,
	 * "manufacturer" : tstr,
	 *     "product" : tstr,
	 *     "model" : tstr,
	 *     "device" : tstr,
	 *   ; Taken from the AVB values
	 *     "vbmeta_digest": bstr,
	 *   ; Same as android.os.Build.VERSION.release. Not optional for TEE.
	 *     ? "os_version" : tstr,                         ;
	 *   ; YYYYMM, must match KeyMint OS_PATCHLEVEL
	 *     "system_patch_level" : uint,
	 *   ; YYYYMMDD, must match KeyMint BOOT_PATCHLEVEL
	 *     "boot_patch_level" : uint,
	 *   ; YYYYMMDD, must match KeyMint VENDOR_PATCHLEVEL
	 *     "vendor_patch_level" : uint,
	 *     "security_level" : "tee" / "strongbox",
	 *   ; 1 if secure boot is enforced for the processor that the IRPC
	 *     "fused": 1 / 0,
	 *     "vb_state" : "green" / "yellow" / "orange",
	 *     "bootloader_state" : "locked" / "unlocked",
	 * }
	 *
	 */

	/* Move DeviceInfo - we can make it in-place */
	memmove(b8, buf + index + 1, device_info_len);
	b8 += device_info_len;

	/* KeysToSign = [ * PublicKey ] */
	*b8++ = CBOR_HDR1(CBOR_MAJOR_ARR, key_count);
	for (size_t i = 0; i < key_count; i++) {
		memcpy(b8, public_key[i], CBOR_PUBLIC_KEY_LEN);
		b8 += CBOR_PUBLIC_KEY_LEN;
	}
	*b8++ = CBOR_HDR1(CBOR_MAJOR_BSTR, CBOR_BYTES1);
	*b8++ = ECDSA_SIG_BYTES;

	if (!sign_with_cdi_key(BOOT_PARAM_DICE_CHAIN_GSC,
			       slice(data_len_to_sign, data_to_sign), b8))
		return SBERR_UnknownError;

	/* Now remove signing prefix */
	memmove(data_to_sign, data_start, data_len + 2 + ECDSA_SIG_BYTES);
	*out_len_bytes = prefix_bytes + data_len + ECDSA_SIG_BYTES + 2;
	return SB_OK;
}

DECLARE_STRONGBOX_COMMAND(SB_RpcGenerateCertificateV2Request,
			  sb_GenerateCertificateReq);

/* Limit the size of long form encoded objects to < 64 kB. */
#define MAX_ASN1_OBJ_LEN_BYTES 3

/* Reserve space for TLV encoding */
#define SEQ_SMALL  2 /* < 128 bytes (1B type, 1B 7-bit length) */
#define SEQ_MEDIUM 3 /* < 256 bytes (1B type, 1B length size, 1B length) */
#define SEQ_LARGE  4 /* < 65536 bytes (1B type, 1B length size, 2B length) */

/* Tag related constants.
 * Bits 0..4 - number
 * Bit  5    - constructed (1) / primitive (0)
 * Bits 6..7 - Class
 *        00 - Universal,
 *        01 - Application
 *        10 - Context-specific
 *        11 - Private
 */
enum {
	V_ASN1_BOOL = 0x01,
	V_ASN1_INT = 0x02,
	V_ASN1_BIT_STRING = 0x03,
	V_ASN1_BYTES = 0x04, /* OCTET STRING*/
	V_ASN1_NULL = 0x05,
	V_ASN1_OBJ = 0x06,
	V_ASN1_OBJ_DESC = 0x07,
	V_ASN1_ENUM = 0x0a,
	V_ASN1_UTF8 = 0x0c,
	V_ASN1_SEQUENCE = 0x10,
	V_ASN1_SET = 0x11,
	V_ASN1_ASCII = 0x13,
	V_ASN1_TIME = 0x18,
	/* Tag number is greater than 30 */
	V_ASN1_HIGH_TAG = 0x1F,
	V_ASN1_CONSTRUCTED = 0x20,
	/* Bits 6-7, Tag class */
	V_ASN1_APPLICATION = 0x40,
	V_ASN1_CONTEXT_SPECIFIC = 0x80,
	V_ASN1_CONTEXT_PRIVATE = 0xC0,

	/* Short helpers */
	V_BITS = V_ASN1_BIT_STRING,
	V_SEQ = V_ASN1_CONSTRUCTED | V_ASN1_SEQUENCE,
	V_SET = V_ASN1_CONSTRUCTED | V_ASN1_SET,
	/* Context-specific, Constructed, High-tag (tag >=30)*/
	V_ASN1_CCH = V_ASN1_CONTEXT_SPECIFIC | V_ASN1_CONSTRUCTED |
		     V_ASN1_HIGH_TAG,
	/* Context-specific, Constructed, low-tag (tag <= 30) */
	V_ASN1_CCL = V_ASN1_CONTEXT_SPECIFIC | V_ASN1_CONSTRUCTED,
};

struct asn1 {
	uint8_t *p;
	size_t n;
};

#define SEQ_START(X, T, L)         \
	do {                       \
		int __old = (X).n; \
		uint8_t __t = (T); \
		int __l = (L);     \
		(X).n += __l;
#define SEQ_END(X)                                                       \
	(X).n = asn1_seq((X).p + __old, __t, __l, (X).n - __old - __l) + \
		__old;                                                   \
	}                                                                \
	while (0)

#define OID(X) SLICE(OID_##X, sizeof(OID_##X))

static const uint8_t OID_ecdsa_with_SHA256[8] = { 0x2A, 0x86, 0x48, 0xCE,
						  0x3D, 0x04, 0x03, 0x02 };

static const uint8_t OID_id_ecPublicKey[7] = { 0x2A, 0x86, 0x48, 0xCE,
					       0x3D, 0x02, 0x01 };
static const uint8_t OID_prime256v1[8] = { 0x2A, 0x86, 0x48, 0xCE,
					   0x3D, 0x03, 0x01, 0x07 };

static const uint8_t OID_keyUsage[3] = { 0x55, 0x1D, 0x0F };

static const uint8_t OID_keymint[10] = {
	0x2B, 0x06, 0x01, 0x04, 0x01, 0xD6, 0x79, 0x02, 0x01, 0x11,
};

/**
 * Encoding OID: 1.3.6.1.4.1.11129.2.1.30
 * The extension value consists of Concise Binary Object Representation (CBOR)
 * data that conforms to this Concise Data Definition Language (CDDL) schema:
 * {1 : int,       ; certificates issued
 *  4 : string,    ; validated attested entity (STRONG_BOX/TEE) }
 * The map is unversioned and new optional fields may be added.
 * certs_issued - An approximate number of certificates issued to the device in
 * the last 30 days. This value can be used as a signal for potential abuse if
 * the value is greater than average by some orders of magnitude.
 * validated_attested_entity -
 * The validated attested entity is a string that describes the type of device
 * that was confirmed by the provisioning server to be attested. For example,
 * STRONG_BOX or TEE.
 * https://source.android.com/docs/security/features/keystore/attestation#provisioninginfo_extension_schema
 */
static const uint8_t OID_strongbox[10] = {
	0x2B, 0x06, 0x01, 0x04, 0x01, 0xD6, 0x79, 0x02, 0x01, 0x1e,
};
static const uint8_t CBOR_strongbox[15] = { CBOR_HDR1(CBOR_MAJOR_MAP, 2),
					    CBOR_HDR1(CBOR_MAJOR_UINT, 1),
					    CBOR_HDR1(CBOR_MAJOR_UINT, 10),
					    CBOR_HDR1(CBOR_MAJOR_UINT, 4),
					    CBOR_HDR1(CBOR_MAJOR_TSTR, 10),
					    'S',
					    'T',
					    'R',
					    'O',
					    'N',
					    'G',
					    '_',
					    'B',
					    'O',
					    'X' };

/* start a tag and return write ptr */
static uint8_t *asn1_tag(struct asn1 *ctx, uint8_t tag)
{
	ctx->p[(ctx->n)++] = tag;
	return ctx->p + ctx->n;
}

static size_t asn1_len_len(size_t size)
{
	if (size < 128)
		return 1;
	if (size < 256)
		return 2;
	return 3;
}

/* DER encode length and return encoded size thereof */
static size_t asn1_len(uint8_t *p, size_t size)
{
	if (size < 128) {
		p[0] = size;
		return 1;
	} else if (size < 256) {
		p[0] = 0x81;
		p[1] = size;
		return 2;
	}
	p[0] = 0x82;
	p[1] = size >> 8;
	p[2] = size;
	return 3;
}

/*
 * close sequence and move encapsulated data if needed
 * return total length.
 */
static size_t asn1_seq(uint8_t *p, uint8_t tag, size_t l, size_t size)
{
	size_t tl;

	p[0] = tag;
	tl = asn1_len(p + 1, size) + 1;
	/* TODO: tl > l fail */
	if (tl < l)
		memmove(p + tl, p + l, size);

	return tl + size;
}

static void asn1_be_int(struct asn1 *ctx, uint8_t tag, const uint8_t *b,
			size_t n)
{
	uint8_t *p = asn1_tag(ctx, tag);
	size_t i;

	for (i = 0; i < n; ++i) {
		if (b[i] != 0)
			break;
	}
	if (i == n) {
		/* Special case for zero bytes.*/
		*p++ = 1;
		*p++ = 0;
	} else if (b[i] & 0x80) {
		*p++ = n - i + 1;
		*p++ = 0;
	} else {
		*p++ = n - i;
	}
	for (; i < n; ++i)
		*p++ = b[i];

	ctx->n = p - ctx->p;
}

/* DER encode (small positive) integer */
static void asn1_int(struct asn1 *ctx, uint32_t val)
{
	uint32_t be_val = htobe32(val);

	asn1_be_int(ctx, V_ASN1_INT, (const uint8_t *)&be_val, sizeof(be_val));
}

static void asn1_int64(struct asn1 *ctx, uint64_t val)
{
	uint32_t be_val[2];

	be_val[1] = htobe32((uint32_t)val);
	be_val[0] = htobe32((uint32_t)(val >> 32));
	asn1_be_int(ctx, V_ASN1_INT, (const uint8_t *)&be_val, sizeof(be_val));
}

static void asn1_enum(struct asn1 *ctx, uint32_t val)
{
	uint32_t be_val = htobe32(val);

	asn1_be_int(ctx, V_ASN1_ENUM, (const uint8_t *)&be_val, sizeof(be_val));
}

/* DER encode positive p256_int */
static void asn1_p256_int(struct asn1 *ctx, const p256_int *n)
{
	uint8_t bn[P256_NBYTES];

	p256_to_bin(n, bn);
	asn1_be_int(ctx, V_ASN1_INT, bn, P256_NBYTES);
}

/* DER encode p256 signature */
static void asn1_sig(struct asn1 *ctx, const p256_int *r, const p256_int *s)
{
	SEQ_START(*ctx, V_SEQ, SEQ_SMALL)
	{
		asn1_p256_int(ctx, r);
		asn1_p256_int(ctx, s);
	}
	SEQ_END(*ctx);
}

/* DER encode bytes */
static void asn1_bytes(struct asn1 *ctx, uint8_t tag, struct slice slice)
{
	const uint8_t *s = (const uint8_t *)slice.p;
	size_t n = slice.n;
	uint8_t *p = asn1_tag(ctx, tag);

	p += asn1_len(p, n);
	while (n--)
		*p++ = *s++;

	ctx->n = p - ctx->p;
}

/* DER encode printable string */
static void asn1_string(struct asn1 *ctx, uint8_t tag, const char *s)
{
	size_t n = strlen(s);

	asn1_bytes(ctx, tag, SLICE((const uint8_t *)s, n));
}

/* DER encode bytes */
static void asn1_object(struct asn1 *ctx, struct slice obj)
{
	uint8_t *p = asn1_tag(ctx, V_ASN1_OBJ);
	size_t n = obj.n;
	const uint8_t *b = (const uint8_t *)obj.p;

	p += asn1_len(p, n);
	while (n--)
		*p++ = *b++;

	ctx->n = p - ctx->p;
}

/* DER encode p256 pk */
static void asn1_pub(struct asn1 *ctx, const p256_int *x, const p256_int *y)
{
	uint8_t *p = asn1_tag(ctx, 4); /* uncompressed format */

	p256_to_bin(x, p);
	p += P256_NBYTES;
	p256_to_bin(y, p);
	p += P256_NBYTES;

	ctx->n = p - ctx->p;
}

/* Verify that the `seq` slice is an asn.1 DER SEQ with the valid length.
 * Add it to `ctx` if the verification was successful and return 1.
 * Otherwise, return 0.
 *
 * Only shotr and long length forms are allowed, with up to 4 length bytes,
 * indefinite form for the length is not supported.
 */
static int verify_and_add_seq(struct asn1 *ctx, const struct slice *seq)
{
	unsigned char *p = (char *)seq->p;

	/* 1. Verify */
	if (p == NULL || seq->n < 2)
		return 0;
	if (p[0] != V_SEQ)
		return 0;
	if (p[1] < 128) {
		/* short length form: 0x30 N <N bytes> */
		size_t len = p[1];

		if (seq->n != 2 + len)
			return 0;
	} else {
		/* long or indefinite length form:
		 * 0x30 0x80|M <M len bytes = N> <N bytes>
		 */
		size_t len_len = p[1] & 0x7f;
		size_t len;
		size_t i;

		if (len_len == 0 || len_len > 4)
			return 0;
		if (seq->n < 2 + len_len)
			return 0;
		len = p[2 + len_len];
		for (i = 1; i < len_len; i++) {
			len <<= 8;
			len |= p[2 + len_len + i];
		}
		if (seq->n != 2 + len_len + len)
			return 0;
	}

	/* 2. Add the verified sequence */
	memcpy(ctx->p + ctx->n, seq->p, seq->n);
	ctx->n += seq->n;
	return 1;
}

/*
 * Manually writes a context-specific, constructed tag.
 * Returns the number of bytes written (1-3).
 * Assumes tag_num is > 0.
 *
 * Example:
 * [706] -> BF 85 42 (constructed, context-specific, tag 706)
 * [1]   -> A1       (constructed, context-specific, tag 1)
 */
static int asn1_write_context_tag(struct asn1 *ctx, uint32_t tag_num)
{
	/* Check for 3-byte tag (BF 85 42) */
	if (tag_num > 127) {
		/* Context-specific, Constructed, High-tag */
		ctx->p[ctx->n++] = V_ASN1_CCH;
		ctx->p[ctx->n++] = 0x80 | (tag_num >> 7);
		ctx->p[ctx->n++] = tag_num & 0x7F;
		return 3;
	}
	/* Check for 2-byte tag (BF 0F) */
	if (tag_num > 30) {
		ctx->p[ctx->n++] = V_ASN1_CCH;
		ctx->p[ctx->n++] = (uint8_t)tag_num;
		return 2;
	}
	/* Check for 1-byte tag (A1)
	 * Context-specific, Constructed, Low-tag
	 */
	ctx->p[ctx->n++] = V_ASN1_CCL | (uint8_t)tag_num;
	return 1;
}

/*
 * Helper to encode: [tag_num] EXPLICIT NULL
 */
static void asn1_explicit_null(struct asn1 *ctx, uint32_t tag_num)
{
	/* 1. Write the EXPLICIT tag (e.g., [303]) */
	asn1_write_context_tag(ctx, tag_num);

	/* 2. Write the length of the inner payload (which is 2 bytes) */
	asn1_len(ctx->p + ctx->n, 2);
	/* asn1_len for <128 is 1 byte */
	ctx->n += 1;

	/* 3. Write the inner NULL TLV */
	asn1_tag(ctx, V_ASN1_NULL);
	/* Length 0 */
	ctx->p[ctx->n++] = 0x00;
}

/*
 * Helper to encode: [tag_num] EXPLICIT INTEGER
 */
static void asn1_explicit_int(struct asn1 *ctx, uint32_t tag_num, uint64_t val)
{
	/* 8-byte int + 0-padding + TLV = max ~12 bytes */
	uint8_t int_buf[15];
	struct asn1 int_ctx = {
		int_buf,
		0,
	};
	/* 1. Encode the inner INTEGER into a temp buffer */
	/* int_ctx.n now holds the length of the inner TLV */
	asn1_int64(&int_ctx, val);

	/* 2. Write the EXPLICIT tag (e.g., [706]) */
	asn1_write_context_tag(ctx, tag_num);

	/* 3. Write the outer length (which is the length of the inner TLV) */
	ctx->n += asn1_len(ctx->p + ctx->n, int_ctx.n);

	/* 4. Write the inner TLV (the value) */
	memcpy(ctx->p + ctx->n, int_buf, int_ctx.n);
	ctx->n += int_ctx.n;
}

/*
 * Helper to encode: [tag_num] EXPLICIT OCTET_STRING
 * Used for: [709] attestationApplicationId, [710] attestationIdBrand, etc.
 */
static void asn1_explicit_bytes(struct asn1 *ctx, uint32_t tag_num,
				struct slice data)
{
	/* Calculate wrapped length */
	size_t len = 1 + data.n + asn1_len_len(data.n);
	/* Write the EXPLICIT tag (e.g., [709]) */
	asn1_write_context_tag(ctx, tag_num);
	/* Write the outer length (the length of the inner TLV) */
	ctx->n += asn1_len(ctx->p + ctx->n, len);
	/* Write actual data*/
	asn1_bytes(ctx, V_ASN1_BYTES, data);
}

static void add_key_purpose(struct asn1 *ctx, uint32_t km_purpose)
{
	uint32_t purpose = 0;
	/* id-ce-keyUsage OBJECT IDENTIFIER ::= { id-ce 15 }
	 * KeyUsage ::= BIT STRING {
	 *  digitalSignature        (0),
	 *  nonRepudiation          (1),  -- recent editions of X.509 have
	 *        -- renamed this bit to contentCommitment
	 *  keyEncipherment         (2),
	 *  dataEncipherment        (3),
	 *  keyAgreement            (4),
	 *  keyCertSign             (5),
	 *  cRLSign                 (6),
	 *  encipherOnly            (7),
	 *  decipherOnly            (8) }
	 */
	if (km_purpose & (1U << KM_PURPOSE_SIGN))
		purpose |= 0x80;
	if (km_purpose & (1U << KM_PURPOSE_ATTEST_KEY))
		purpose |= 0x04;
	if (km_purpose & (1U << KM_PURPOSE_AGREE_KEY))
		purpose |= 0x08;
	if (km_purpose & (1U << KM_PURPOSE_WRAP_KEY))
		purpose |= 0x20;
	if ((km_purpose &
	     ((1U << KM_PURPOSE_ENCRYPT) | (1U << KM_PURPOSE_DECRYPT))) ==
	    ((1U << KM_PURPOSE_ENCRYPT) | (1U << KM_PURPOSE_DECRYPT)))
		purpose |= 0x10;
	else if (km_purpose & (1U << KM_PURPOSE_ENCRYPT))
		purpose |= 0x01;
	else if (km_purpose & (1U << KM_PURPOSE_DECRYPT))
		purpose |= 0x8000;

	/* Wrap in OCTET_STRING */
	SEQ_START(*ctx, V_ASN1_BYTES, SEQ_MEDIUM)
	{
		SEQ_START(*ctx, V_BITS, SEQ_MEDIUM)
		{
			if (purpose > 0xff) {
				/* 7 unused bits */
				ctx->p[ctx->n++] = 7;
				ctx->p[ctx->n++] = purpose & 0xff;
				ctx->p[ctx->n++] = (purpose >> 8) & 0xff;
			} else {
				ctx->p[ctx->n++] = __builtin_ctz(purpose);
				ctx->p[ctx->n++] = purpose & 0xff;
			}
		}
		SEQ_END(*ctx);
	}
	SEQ_END(*ctx);
}

static void add_set_int(struct asn1 *ctx, uint32_t tag, uint32_t value)
{
	uint8_t *p;

	/* Write the EXPLICIT tag [1] */
	asn1_write_context_tag(ctx, tag);
	/* Location where to output length. It is known to be short */
	p = ctx->p + ctx->n;
	ctx->n++;
	SEQ_START(*ctx, V_SET, SEQ_SMALL)
	{
		/* Up to 30 bits in length to have 1-byte encoding. */
		for (size_t i = 0; i < 30; i++) {
			if (value & (1U << i))
				asn1_int(ctx, i);
		}
	}
	SEQ_END(*ctx);
	*p = ctx->n - (p - ctx->p) - 1;
}

static void add_root_of_trust(struct asn1 *ctx, bool device_locked,
			      enum km_vb_state verified, struct slice data)
{
	/* Calculate wrapped length */
	size_t len = 10 + (data.n + asn1_len_len(data.n)) * 2;

	/* Write the EXPLICIT tag [704] */
	asn1_write_context_tag(ctx, 704);
	/* Write the outer length (the length of the inner TLV) */
	ctx->n += asn1_len(ctx->p + ctx->n, len);
	/*
	 * RootOfTrust ::= SEQUENCE {
	 * // A secure hash of the public key used to verify the integrity and
	 * // authenticity of all code that executes during device boot up as
	 * // part of Verified Boot.
	 * verifiedBootKey            OCTET_STRING,
	 * deviceLocked               BOOLEAN,
	 * verifiedBootState          VerifiedBootState,
	 * // A digest of all data protected by Verified Boot. For devices that
	 * // use the Android Verified Boot reference implementation, this
	 * // field contains the VBMeta digest.
	 * verifiedBootHash           OCTET_STRING, }
	 * VerifiedBootState ::= ENUMERATED {
	 *   Verified                   (0),
	 *   SelfSigned                 (1),
	 *   Unverified                 (2),
	 *   Failed                     (3), }
	 */
	SEQ_START(*ctx, V_SEQ, SEQ_SMALL)
	{
		/* TODO: Should we use some other constant? */
		asn1_bytes(ctx, V_ASN1_BYTES, data);
		ctx->p[ctx->n++] = V_ASN1_BOOL;
		ctx->p[ctx->n++] = 1;
		ctx->p[ctx->n++] = (device_locked) ? 255 : 0;
		asn1_be_int(ctx, V_ASN1_ENUM, (uint8_t *)&verified, 1);
		asn1_bytes(ctx, V_ASN1_BYTES, data);
	}
	SEQ_END(*ctx);
}

static void add_km_enforcements(struct asn1 *ctx,
				const struct km_key_params *params)
{
	/* softwareEnforced */
	SEQ_START(*ctx, V_SEQ, SEQ_LARGE)
	{
		if (params->creationDateTime)
			asn1_explicit_int(ctx, 701, params->creationDateTime);
		if (params->attestationApplicationId.n)
			asn1_explicit_bytes(ctx, 709,
					    params->attestationApplicationId);
		if (params->moduleHash.n)
			asn1_explicit_bytes(ctx, 724, params->moduleHash);
	}
	SEQ_END(*ctx);

	/* hardwareEnforced */
	SEQ_START(*ctx, V_SEQ, SEQ_LARGE)
	{
		add_set_int(ctx, 1, params->attrs.purpose_flags);
		asn1_explicit_int(ctx, 2, params->attrs.algorithm);
		if (params->attrs.key_size)
			asn1_explicit_int(ctx, 3, params->attrs.key_size);
		if (params->attrs.digest_flags)
			add_set_int(ctx, 5, params->attrs.digest_flags);

		if (params->attrs.curve_id)
			asn1_explicit_int(ctx, 10, params->attrs.curve_id);

		if (params->rollbackResistance)
			asn1_explicit_null(ctx, 303);
		if (params->earlyBootOnly)
			asn1_explicit_null(ctx, 304);

		if (params->noAuthRequired)
			asn1_explicit_null(ctx, 503);

		asn1_explicit_int(ctx, 702, params->origin);

		add_root_of_trust(ctx, true, KM_VB_VERIFIED,
				  params->rootOfTrust);

		if (params->osVersion)
			asn1_explicit_int(ctx, 705, params->osVersion);
		if (params->osPatchLevel)
			asn1_explicit_int(ctx, 706, params->osPatchLevel);
		if (params->attestationIdBrand.n)
			asn1_explicit_bytes(ctx, 710,
					    params->attestationIdBrand);
		if (params->attestationIdDevice.n)
			asn1_explicit_bytes(ctx, 711,
					    params->attestationIdDevice);
		if (params->attestationIdProduct.n)
			asn1_explicit_bytes(ctx, 712,
					    params->attestationIdProduct);
		if (params->attestationIdSerial.n)
			asn1_explicit_bytes(ctx, 713,
					    params->attestationIdSerial);
		if (params->attestationIdImei.n)
			asn1_explicit_bytes(ctx, 714,
					    params->attestationIdImei);
		if (params->attestationIdMeid.n)
			asn1_explicit_bytes(ctx, 715,
					    params->attestationIdMeid);
		if (params->attestationIdManufacturer.n)
			asn1_explicit_bytes(ctx, 716,
					    params->attestationIdManufacturer);
		if (params->attestationIdModel.n)
			asn1_explicit_bytes(ctx, 717,
					    params->attestationIdModel);
		if (params->vendorPatchLevel)
			asn1_explicit_int(ctx, 718, params->vendorPatchLevel);
		if (params->bootPatchLevel)
			asn1_explicit_int(ctx, 719, params->vendorPatchLevel);
	}
	SEQ_END(*ctx);
}

/* https://source.android.com/docs/security/features/keystore/attestation */
static void add_km_extension(struct asn1 *ctx,
			     const struct km_key_params *params)
{
	/* Wrap in OCTET_STRING */
	SEQ_START(*ctx, V_ASN1_BYTES, SEQ_LARGE)
	{
		SEQ_START(*ctx, V_SEQ, SEQ_LARGE)
		{
			/* attestationVersion */
			asn1_int(ctx, 400);
			/* attestationSecurityLevel */
			asn1_enum(ctx, KM_SECURITY_STRONGBOX);
			/* keyMintVersion */
			asn1_int(ctx, 400);
			/* keyMintSecurityLevel */
			asn1_enum(ctx, KM_SECURITY_STRONGBOX);

			asn1_bytes(ctx, V_ASN1_BYTES,
				   params->attestationChallenge);

			/* A privacy-sensitive device identifier that system
			 * apps can request at key generation time. If the
			 * unique ID is not requested, this field is empty.
			 * The Unique ID is a 128-bit value that identifies the
			 * device, but only for a limited period of time. The
			 * value is computed with: HMAC_SHA256(T || C || R, HBK)
			 */
			asn1_bytes(ctx, V_ASN1_BYTES, params->uniqueId);

			add_km_enforcements(ctx, params);
		}
		SEQ_END(*ctx);
	}
	SEQ_END(*ctx);
}

static void add_cert_extension(struct asn1 *ctx,
			       const struct km_key_params *params)
{
	/* Certificate extension:
	 * extensions      [3]  EXPLICIT Extensions OPTIONAL
	 */
	SEQ_START(*ctx, V_ASN1_CCL | 3, SEQ_LARGE)
	{
		/* Extensions  ::=  SEQUENCE SIZE (1..MAX) OF Extension */
		SEQ_START(*ctx, V_SEQ, SEQ_LARGE)
		{
			/*
			 * Extensions ::= SEQUENCE SIZE (1..MAX) OF Extension
			 * Extension ::= SEQUENCE  {
			 *   extnID      OBJECT IDENTIFIER,
			 *   critical    BOOLEAN DEFAULT FALSE,
			 *   extnValue   OCTET STRING }
			 */
			SEQ_START(*ctx, V_SEQ, SEQ_SMALL)
			{
				asn1_object(ctx, OID(keyUsage));
				add_key_purpose(ctx,
						params->attrs.purpose_flags);
			}
			SEQ_END(*ctx);

			SEQ_START(*ctx, V_SEQ, SEQ_LARGE)
			{
				asn1_object(ctx, OID(keymint));
				add_km_extension(ctx, params);
			}
			SEQ_END(*ctx);

			SEQ_START(*ctx, V_SEQ, SEQ_SMALL)
			{
				asn1_object(ctx, OID(strongbox));
				asn1_bytes(ctx, V_ASN1_BYTES,
					   SLICE_OBJ(CBOR_strongbox));
			}
			SEQ_END(*ctx);
		}
		SEQ_END(*ctx);
	}
	SEQ_END(*ctx);
}

static size_t SB_cert_name(const p256_int *d, const p256_int *pk_x,
			   const p256_int *pk_y,
			   const struct km_key_params *params, uint8_t *cert,
			   const size_t n)
{
	struct asn1 ctx = { cert, 0 };
	struct sha256_ctx sha;
	p256_int h, r, s;
	struct drbg_ctx drbg;
	enum dcrypto_result result;
	size_t total_slice_len = 0;

	for (size_t i = KM_SLICE_TAG_CERT_START; i < ARRAY_SIZE(slice_tags);
	     i++) {
		struct slice *slice = (struct slice *)(((uint8_t *)params) +
						       slice_tags[i].offset);

		total_slice_len += slice->n;
	}

	/* Rough check that total variable inputs + known fixed sized fields
	 * would fit into available space. It is not exact and adds small
	 * buffer to account for variable length ASN.1. */
	if (total_slice_len + 532 > n)
		return 0;

	/**
	 * RFC 5280: https://datatracker.ietf.org/doc/html/rfc5280
	 * Certificate  ::=  SEQUENCE  {
	 * tbsCertificate       TBSCertificate,
	 * signatureAlgorithm   AlgorithmIdentifier,
	 * signatureValue       BIT STRING  }
	 */
	SEQ_START(ctx, V_SEQ, SEQ_LARGE)
	{ /* outer seq */
		/*
		 * Grab current pointer to data to hash later.
		 * Note this will fail if cert body + cert sign is less
		 * than 256 bytes (SEQ_MEDIUM) -- not likely.
		 */
		uint8_t *body = ctx.p + ctx.n;

		/**
		 * TBSCertificate  ::=  SEQUENCE  {
		 *   version         [0]  EXPLICIT Version DEFAULT v1,
		 *   serialNumber         CertificateSerialNumber,
		 *   signature            AlgorithmIdentifier,
		 *   issuer               Name,
		 *   validity             Validity,
		 *   subject              Name,
		 *   subjectPublicKeyInfo SubjectPublicKeyInfo,
		 *   issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL,
		 *       -- If present, version MUST be v2 or v3
		 *   subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL,
		 *       -- If present, version MUST be v2 or v3
		 *   extensions      [3]  EXPLICIT Extensions OPTIONAL
		 *       -- If present, version MUST be v3
		 * }
		 */
		SEQ_START(ctx, V_SEQ, SEQ_LARGE)
		{
			/* X509 v3 */
			SEQ_START(ctx, 0xa0, SEQ_SMALL)
			{
				asn1_int(&ctx, 2);
			}
			SEQ_END(ctx);

			/* Serial number */
			if (params->certificate_serial.n)
				asn1_be_int(&ctx, V_ASN1_INT,
					    params->certificate_serial.p,
					    params->certificate_serial.n);
			else
				asn1_int(&ctx, 1);

			/* Signature algorithm
			 * AlgorithmIdentifier  ::=  SEQUENCE  {
			 * algorithm               OBJECT IDENTIFIER,
			 * parameters              ANY DEFINED BY algorithm
			 * OPTIONAL  }
			 */
			SEQ_START(ctx, V_SEQ, SEQ_SMALL)
			{
				asn1_object(&ctx, OID(ecdsa_with_SHA256));
			}
			SEQ_END(ctx);

			/* Issuer: Same as the subject field of the batch
			 * attestation key.
			 */
			if (!verify_and_add_seq(&ctx,
						&params->certificate_issuer))
				return 0;

			/* Expiry */
			SEQ_START(ctx, V_SEQ, SEQ_SMALL)
			{
				asn1_string(&ctx, V_ASN1_TIME,
					    "20000101000000Z");
				asn1_string(&ctx, V_ASN1_TIME,
					    "20991231235959Z");
			}
			SEQ_END(ctx);

			/* Subject */
			if (!verify_and_add_seq(&ctx,
						&params->certificate_subject))
				return 0;

			/* Subject pk */
			SEQ_START(ctx, V_SEQ, SEQ_SMALL)
			{
				/* pk parameters */
				SEQ_START(ctx, V_SEQ, SEQ_SMALL)
				{
					asn1_object(&ctx, OID(id_ecPublicKey));
					asn1_object(&ctx, OID(prime256v1));
				}
				SEQ_END(ctx);
				/* pk bits */
				SEQ_START(ctx, V_BITS, SEQ_SMALL)
				{
					/* No unused bit at the end */
					asn1_tag(&ctx, 0);
					asn1_pub(&ctx, pk_x, pk_y);
				}
				SEQ_END(ctx);
			}
			SEQ_END(ctx);

			add_cert_extension(&ctx, params);
		}
		SEQ_END(ctx); /* Cert body */

		/* Sign all of cert body */
		SHA256_sw_init(&sha);
		SHA256_sw_update(&sha, body, (ctx.p + ctx.n) - body);
		p256_from_bin(SHA256_sw_final(&sha)->b8, &h);
		hmac_drbg_init_rfc6979(&drbg, d, &h);
		result = dcrypto_p256_ecdsa_sign(&drbg, d, &h, &r, &s);
		drbg_exit(&drbg);
		if (result != DCRYPTO_OK)
			return 0;

		/* Append X509 signature */
		SEQ_START(ctx, V_SEQ, SEQ_SMALL);
		asn1_object(&ctx, OID(ecdsa_with_SHA256));
		SEQ_END(ctx);
		SEQ_START(ctx, V_BITS, SEQ_SMALL)
		{
			/* Number of unused bits at the end (0-7). */
			asn1_tag(&ctx, 0);
			asn1_sig(&ctx, &r, &s);
		}
		SEQ_END(ctx);
	}
	SEQ_END(ctx); /* end of outer seq */

	return ctx.n;
}
