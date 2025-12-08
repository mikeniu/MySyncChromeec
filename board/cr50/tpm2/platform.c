/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Global.h"
#include "Platform.h"
#include "TPM_Types.h"
#include "NV_fp.h"

#include "board_id_features.h"
#include "boot_param_platform_cr50.h"
#include "ccd_config.h"
#include "console.h"
#include "nvmem_vars.h"
#include "pinweaver.h"
#include "pinweaver_eal.h"
#include "strongbox.h"
#include "tpm_nvmem.h"
#include "tpm_nvmem_ops.h"
#include "dcrypto.h"
#include "u2f_impl.h"
#include "util.h"
#include "version.h"

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)
#define CPRINTS(format, args...) cprints(CC_EXTENSION, format, ## args)

/* Used to control access to NVMEM spaces with different PCR states */
#define PCR_UPDATE_BOOT_POLICY			BIT(0) /* FWMP/antirollback */
#define PCR_UPDATE_ENCSTATEFUL			BIT(1) /* EncStateful */
/*
 * Block rec+dev mode by triggering an EC reset immediately
 * Only enabled on certain board ids.
 */
#define PCR_FWMP_BLOCK_DEV_TRIGGER_ECRST	BIT(2)

struct pcr_config {
	/* The PCR digest */
	uint8_t digest[SHA256_DIGEST_SIZE];
	/* Define what spaces are allowed to be updated in the given state */
	uint32_t config;
};

/*
 * PCR0 values for the states when FWMP, antirollback, and EncStateful updates
 * are allowed.
 * For the source of the FWMP/antirollback values see b/140958855#comment25.
 * For the source of the Encstateful values see b/373478634.
 */
static const struct pcr_config pcr_configs[] = {
	{
		/* initial PCR0 state at reset - all zeroes */
		.digest = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
		.config = PCR_UPDATE_BOOT_POLICY,
	},
	{
		/* normal mode (rec=0, dev=0) */
		.digest = {
			0x89, 0xEA, 0xF3, 0x51, 0x34, 0xB4, 0xB3, 0xC6,
			0x49, 0xF4, 0x4C, 0x0C, 0x76, 0x5B, 0x96, 0xAE,
			0xAB, 0x8B, 0xB3, 0x4E, 0xE8, 0x3C, 0xC7, 0xA6,
			0x83, 0xC4, 0xE5, 0x3D, 0x15, 0x81, 0xC8, 0xC7
		},
		.config = PCR_UPDATE_BOOT_POLICY |
			PCR_UPDATE_ENCSTATEFUL,
	},
	{
		/* dev mode (rec=0, dev=1) */
		.digest = {
			0x23, 0xE1, 0x4D, 0xD9, 0xBB, 0x51, 0xA5, 0x0E,
			0x16, 0x91, 0x1F, 0x7E, 0x11, 0xDF, 0x1E, 0x1A,
			0xAF, 0x0B, 0x17, 0x13, 0x4D, 0xC7, 0x39, 0xC5,
			0x65, 0x36, 0x07, 0xA1, 0xEC, 0x8D, 0xD3, 0x7A
		},
		.config = PCR_UPDATE_BOOT_POLICY |
			PCR_UPDATE_ENCSTATEFUL,
	},
	{
		/* recovery + dev mode (rec=1, dev=1) */
		.digest = {
			0x2A, 0x75, 0x80, 0xE5, 0xDA, 0x28, 0x95, 0x46,
			0xF4, 0xD2, 0xE0, 0x50, 0x9C, 0xC6, 0xDE, 0x15,
			0x5E, 0xA1, 0x31, 0x81, 0x89, 0x54, 0xD3, 0x6D,
			0x49, 0xE0, 0x27, 0xFD, 0x42, 0xB8, 0xC8, 0xF8
		},
		.config = PCR_FWMP_BLOCK_DEV_TRIGGER_ECRST,
	},
	{
		/* recovery mode (rec=1, dev=0) */
		.digest = {
			0x9F, 0x9E, 0xA8, 0x66, 0xD3, 0xF3, 0x4F, 0xE3,
			0xA3, 0x11, 0x2A, 0xE9, 0xCB, 0x1F, 0xBA, 0xBC,
			0x6F, 0xFE, 0x8C, 0xD2, 0x61, 0xD4, 0x24, 0x93,
			0xBC, 0x68, 0x42, 0xA9, 0xE4, 0xF9, 0x3B, 0x3D
		},
		.config = PCR_UPDATE_BOOT_POLICY,
	},
};

uint16_t _cpri__GenerateRandom(int32_t random_size,
			uint8_t *buffer)
{
	if (!fips_rand_bytes(buffer, random_size))
		return 0;
	return random_size;
}

/*
 * Return the pointer to the character immediately after the first dash
 * encountered in the passed in string, or NULL if there is no dashes in the
 * string.
 */
static const char *char_after_dash(const char *str)
{
	char c;

	do {
		c = *str++;

		if (c == '-')
			return str;
	} while (c);

	return NULL;
}

/*
 * The properly formatted build_info string has the ec code SHA1 after the
 * first dash, and tpm2 code sha1 after the second dash.
 */

void   _plat__GetFwVersion(uint32_t *firmwareV1, uint32_t *firmwareV2)
{
	const char *ver_str = char_after_dash(build_info);

	/* Just in case the build_info string is misformatted. */
	*firmwareV1 = 0;
	*firmwareV2 = 0;

	if (!ver_str)
		return;

	*firmwareV1 = strtoi(ver_str, NULL, 16);

	ver_str = char_after_dash(ver_str);
	if (!ver_str)
		return;

	*firmwareV2 = strtoi(ver_str, NULL, 16);
}

/* Old FWMP spaces were created with these attrs. */
#define OLD_FWMP_ATTRS ((TPMA_NV) {				  \
	.TPMA_NV_AUTHWRITE = 1,					  \
	.TPMA_NV_AUTHREAD = 1,					  \
	.TPMA_NV_PPREAD = 1,					  \
	.TPMA_NV_WRITEDEFINE = 1,				  \
})

/* New FWMP spaces were created with these attrs. */
#define NEW_FWMP_ATTRS ((TPMA_NV) {				  \
	.TPMA_NV_PLATFORMCREATE = 1,				  \
	.TPMA_NV_OWNERWRITE = 1,				  \
	.TPMA_NV_AUTHREAD = 1,					  \
	.TPMA_NV_PPREAD = 1,					  \
	.TPMA_NV_PPWRITE = 1,					  \
})

static TPMA_NV board_get_new_fwmp_attributes(TPMA_NV attributes)
{
	TPMA_NV new_attributes = NEW_FWMP_ATTRS;

	if (attributes.TPMA_NV_WRITTEN == 1)
		new_attributes.TPMA_NV_WRITTEN = 1;

	return new_attributes;
}

static bool fwmp_uses_old_attributes(TPMA_NV attributes)
{
	TPMA_NV old_fwmp_attrs = OLD_FWMP_ATTRS;
	/* Ignore the WRITTEN and WRITELOCKED values. */
	old_fwmp_attrs.TPMA_NV_WRITTEN = attributes.TPMA_NV_WRITTEN;
	old_fwmp_attrs.TPMA_NV_WRITELOCKED = attributes.TPMA_NV_WRITELOCKED;
	return memcmp(&attributes, &old_fwmp_attrs, sizeof(attributes)) == 0;
}

/* Update NV_INDEX_FWMP attributes. */
static void board_update_fwmp_attributes(void)
{
	TPM_HANDLE handle;
	NV_INDEX nvIndex;
	TPM_RC result;
	uint32_t handle_addr;

	result = NvIsAvailable();
	if (result != TPM_RC_SUCCESS)
		return;

	handle = HR_NV_INDEX + NV_INDEX_FWMP;
	handle_addr = NvEarlyStageFindHandle(handle);
	if (!handle_addr) {
		CPRINTF("%s: object at 0x%x not found\n", __func__,
			NV_INDEX_FWMP);
		return;
	}

	NvGetIndexInfo(handle, &nvIndex);

	if (!fwmp_uses_old_attributes(nvIndex.publicArea.attributes))
		return;

	CPRINTF("%s: updating FWMP attrs\n", __func__);
	/* Change the old FWMP space attributes to the new attributes. */
	nvIndex.publicArea.attributes = board_get_new_fwmp_attributes(
			nvIndex.publicArea.attributes);
	/*
	 * Write the new FWMP attributes. Don't run NvCommit. It'll get run
	 * later in the boot process
	 */
	NvWriteIndexInfo(handle, &nvIndex);
}

void _plat__StartupCallback(int shall_reset_state)
{
	pinweaver_init();
	board_update_fwmp_attributes();
	boot_param_handle_tpm_startup(shall_reset_state);

#ifdef CONFIG_STRONGBOX
	/*
	 * After boot from S5 clear the SB disable bit to make it possible to
	 * enable Strongbox again. Don't clear the bit on S3 resume.
	 * Keep SB enable bit as is.
	 */
	if (shall_reset_state)
		reset_board_cfg(BOARD_CFG_SB_DISABLE_SET);
#endif

	/*
	 * Eventually, we'll want to allow CCD unlock with no password, so
	 * enterprise policy can set a password to block CCD instead of locking
	 * it out via the FWMP.
	 *
	 * When we do that, we'll allow unlock without password between a real
	 * TPM startup (not just a resume) - which is this callback - and
	 * explicit disabling of that feature via a to-be-created vendor
	 * command.  That vendor command will be called after enterprize policy
	 * is updated, or the device is determined not to be enrolled.
	 *
	 * But for now, we'll just block unlock entirely if no password is set,
	 * so we don't yet need to tell CCD that a real TPM startup has
	 * occurred.
	 */

	/* TODO(b/262324344). Remove when zero sized EPS fixed. */
	if (gp.EPSeed.t.size != PRIMARY_SEED_SIZE ||
	    gp.SPSeed.t.size != PRIMARY_SEED_SIZE) {
		CPRINTF("%s: Seed length is zero [%x, %x]!\n", __func__,
			gp.EPSeed.t.size, gp.SPSeed.t.size);
		cflush();
	}
}

BOOL _plat__ShallSurviveOwnerClear(uint32_t  index)
{
	return index == HR_NV_INDEX + NV_INDEX_FWMP;
}

static void cleanup_report(const char *func, const char *id,
			   enum ec_error_list status)
{
	if (status != EC_SUCCESS)
		CPRINTF("%s: %s cleanup failed (%d)\n", func, id, status);
}

void _plat__OwnerClearCallback(void)
{
	/* Invalidate existing biometrics pairing secrets. */
	cleanup_report(__func__, "pw pk",
		       setvar(PW_FP_PK, sizeof(PW_FP_PK) - 1, NULL, 0));
	cleanup_report(__func__, "pw tree",
		       setvar(PW_TREE_VAR, sizeof(PW_TREE_VAR) - 1, NULL, 0));
	cleanup_report(__func__, "pw log",
		       setvar(PW_LOG_VAR0, sizeof(PW_LOG_VAR0) - 1, NULL, 0));

#ifdef CONFIG_U2F
	/* Invalidate existing u2f registrations. */
	cleanup_report(__func__, "u2f", u2f_zeroize_keys());
#endif

	boot_param_handle_owner_clear();
}

/* Prints the contents of pcr0 */
void print_pcr0(void)
{
	uint8_t pcr0_value[SHA256_DIGEST_SIZE];

	ccprintf("pcr0:    ");
	if (!get_tpm_pcr_value(0, pcr0_value)) {
		ccprintf("error\n");
		return;
	}
	ccprintf("%ph\n", HEX_BUF(&pcr0_value, SHA256_DIGEST_SIZE));
}

/* Returns True if the PCR config has the given space attribute set */
static BOOL pcr_config_enabled(uint32_t space)
{
	uint8_t pcr0_value[SHA256_DIGEST_SIZE];
	int i;

	if (!get_tpm_pcr_value(0, pcr0_value))
		return FALSE;  /* something went wrong, let's be strict */

	for (i = 0; i < ARRAY_SIZE(pcr_configs); ++i) {
		if (memcmp(pcr0_value,
				pcr_configs[i].digest,
				SHA256_DIGEST_SIZE) == 0) {
			if (pcr_configs[i].config & space)
				return TRUE;
			return FALSE;
		}
	}
	return FALSE;
}

BOOL _plat__NvUpdateAllowed(uint32_t handle)
{
	switch (handle) {
	case HR_NV_INDEX + NV_INDEX_ENCSTATEFUL:
		return pcr_config_enabled(PCR_UPDATE_ENCSTATEFUL);
	case HR_NV_INDEX + NV_INDEX_FWMP:
		return pcr_config_enabled(PCR_UPDATE_BOOT_POLICY);
	case HR_NV_INDEX + NV_INDEX_FIRMWARE:
	case HR_NV_INDEX + NV_INDEX_KERNEL:
		return pcr_config_enabled(PCR_UPDATE_BOOT_POLICY)
			|| board_fwmp_allows_boot_policy_update();
	}

	return TRUE;
}

#ifdef CONFIG_FWMP_BLOCK_REC_DEV_RESET_EC
/*
 * Return TRUE if the board id blocks rec+dev with an ecrst, the device is
 * in rec+dev, and the FWMP is blocking dev mode.
 */
static BOOL should_assert_ecrst_if_pcr_state_not_allowed(void)
{
	/* Check feature is enabled for the set board id */
	if (!bid_feature_enabled_pcr_ecrst_recdev())
		return FALSE;

	/* If the current PCR value doesn't trigger ecrst, return */
	if (!pcr_config_enabled(PCR_FWMP_BLOCK_DEV_TRIGGER_ECRST))
		return FALSE;

	/*
	 * Check the FWMP allows boot policy updates. This means that the FWMP
	 * is not set to block dev mode.
	 */
	if (board_fwmp_allows_boot_policy_update())
		return FALSE;

	return TRUE;
}
#endif

void _plat__PCRUpdated(uint32_t index)
{
	if (index != 0)
		return;
	CPRINTS("PCR0 updated");
#ifdef CONFIG_FWMP_BLOCK_REC_DEV_RESET_EC
	/* Reset the EC if the PCR is in a blocked state. */
	if (should_assert_ecrst_if_pcr_state_not_allowed()) {
		CPRINTS("pcr: block rec+dev with ecrst");
		board_reboot_ec();
	}
#endif
}
