/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test Cr50 Virtual NVRAM Index 0x013fff0b (CCD Flags) and read_ccd_flags().
 */

#include <endian.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef htobe16
#define htobe16(x) __builtin_bswap16(x)
#endif
#ifndef htobe32
#define htobe32(x) __builtin_bswap32(x)
#endif
#ifndef be32toh
#define be32toh(x) __builtin_bswap32(x)
#endif

#ifndef CC_CCD
#define CC_CCD CC_COMMAND
#endif

#ifndef DISABLE_SLEEP_TIME_TPM_WIPE
#define DISABLE_SLEEP_TIME_TPM_WIPE 0
#endif

#include "Global.h"
#include "common.h"

#ifndef CONFIG_CASE_CLOSED_DEBUG_V1
#define CONFIG_CASE_CLOSED_DEBUG_V1
#endif

#ifndef NVMEM_VAR_CCD_CONFIG
#define NVMEM_VAR_CCD_CONFIG 3
#endif

#include "board_id.h"
#include "ccd_config.h"
#include "console.h"
#include "dcrypto.h"
#include "factory_config.h"
#include "hooks.h"
#include "nvmem_vars.h"
#include "physical_presence.h"
#include "rma_auth.h"
#include "sn_bits.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "tpm_nvmem.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"
#include "u2f_cmds.h"
#include "util.h"
#include "virtual_nvmem.h"

/* Mock NVMEM tuple storage for ccd_config */
#define MOCK_MAX_VARS 16
#define MOCK_VAR_KEY_LEN 32
#define MOCK_VAR_VAL_LEN 256

struct mock_tuple {
	int active;
	uint8_t key[MOCK_VAR_KEY_LEN];
	uint8_t key_len;
	struct tuple t;
	uint8_t val[MOCK_VAR_VAL_LEN];
};

static struct mock_tuple mock_vars[MOCK_MAX_VARS];

static void reset_mock_nvmem(void)
{
	memset(mock_vars, 0, sizeof(mock_vars));
}

const struct tuple *getvar(const uint8_t *key, uint8_t key_len)
{
	int i;

	for (i = 0; i < MOCK_MAX_VARS; i++) {
		if (mock_vars[i].active && mock_vars[i].key_len == key_len &&
		    memcmp(mock_vars[i].key, key, key_len) == 0) {
			return &mock_vars[i].t;
		}
	}
	return NULL;
}

void freevar(const struct tuple *var) {}

const uint8_t *tuple_key(const struct tuple *t)
{
	struct mock_tuple *mt =
		(struct mock_tuple *)((uintptr_t)t -
				      offsetof(struct mock_tuple, t));

	return mt->key;
}

const uint8_t *tuple_val(const struct tuple *t)
{
	struct mock_tuple *mt =
		(struct mock_tuple *)((uintptr_t)t -
				      offsetof(struct mock_tuple, t));

	return mt->val;
}

enum ec_error_list setvar(const uint8_t *key, uint8_t key_len,
			  const uint8_t *val, uint8_t val_len)
{
	int i;
	int slot = -1;

	if (!val || val_len == 0) {
		for (i = 0; i < MOCK_MAX_VARS; i++) {
			if (mock_vars[i].active &&
			    mock_vars[i].key_len == key_len &&
			    memcmp(mock_vars[i].key, key, key_len) == 0) {
				mock_vars[i].active = 0;
				return EC_SUCCESS;
			}
		}
		return EC_SUCCESS;
	}

	for (i = 0; i < MOCK_MAX_VARS; i++) {
		if (mock_vars[i].active &&
		    mock_vars[i].key_len == key_len &&
		    memcmp(mock_vars[i].key, key, key_len) == 0) {
			slot = i;
			break;
		}
		if (!mock_vars[i].active && slot == -1)
			slot = i;
	}
	if (slot == -1)
		return EC_ERROR_OVERFLOW;

	mock_vars[slot].active = 1;
	mock_vars[slot].key_len = key_len;
	memcpy(mock_vars[slot].key, key, key_len);
	mock_vars[slot].t.key_len = key_len;
	mock_vars[slot].t.val_len = val_len;
	memcpy(mock_vars[slot].val, val, val_len);
	return EC_SUCCESS;
}

/* Stubs for ccd_config dependencies */
enum pp_fsm_state physical_presense_fsm_state(void)
{
	return PP_OTHER;
}

int physical_detect_start(int is_long, void (*callback)(void))
{
	return EC_SUCCESS;
}

int physical_detect_press(void)
{
	return EC_SUCCESS;
}

int physical_detect_release(void)
{
	return EC_SUCCESS;
}

int physical_detect_busy(void)
{
	return 0;
}

void physical_detect_abort(void) {}

void physical_presence_fsm_abort(void) {}

int physical_presence_is_asserted(void)
{
	return 0;
}

enum ec_error_list tpm_sync_reset(bool wipe_first)
{
	return EC_SUCCESS;
}

void tpm_alt_extension(struct tpm_cmd_header *tpmh, size_t buffer_size) {}

enum ec_error_list tpm_reset_request(bool wait_until_done, bool wipe_tpm)
{
	return EC_SUCCESS;
}

void board_wipe_tpm_data(void) {}
int board_wipe_tpm(int wipe_type)
{
	return EC_SUCCESS;
}
void board_wp_follow_ccd_config(void) {}
void board_reboot_ap(void) {}
int board_is_first_factory_boot(void)
{
	return 0;
}
int board_fwmp_allows_unlock(void)
{
	return 1;
}
int board_fwmp_allows_boot_policy_update(void)
{
	return 1;
}
int board_vboot_dev_mode_enabled(void)
{
	return 0;
}
int board_battery_is_present(void)
{
	return 1;
}
void tpm_stop_wipe_timer(void) {}
void tpm_restart_wipe_timer(void) {}
void delay_sleep_by(uint32_t delay_us) {}

void ccd_board_init(void) {}

int ccd_board_has_user_presence(void)
{
	return 1;
}

int ccd_board_has_open_auth(void)
{
	return 0;
}

void dlog_clear(void) {}
void dlog_put(uint8_t type, const void *data, size_t len) {}
const void *dlog_get_idx(uint16_t idx)
{
	return NULL;
}

int rma_status_is_open(void)
{
	return 0;
}

const char *rma_get_challenge(void)
{
	return "12345";
}

int rma_try_auth(void *auth_code, void *challenge)
{
	return 0;
}

int rma_auth_is_active(void)
{
	return 0;
}

int tpm_generated_random_post_reset(void)
{
	return 0;
}

int trng_rand(void)
{
	return 0x12345678;
}

int system_get_chip_unique_id(uint8_t **id)
{
	static uint8_t uid[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

	*id = uid;
	return sizeof(uid);
}

bool fips_rand_bytes(void *buffer, size_t len)
{
	memset(buffer, 0x42, len);
	return true;
}

enum dcrypto_result DCRYPTO_hw_hash_init(union hash_ctx *ctx,
					 enum hashing_mode mode)
{
	return DCRYPTO_OK;
}

/* Stubs for virtual_nvmem dependencies */
int read_board_id(struct board_id *id)
{
	if (id) {
		id->type = 0x5a5a4352;
		id->type_inv = ~id->type;
		id->flags = 0;
	}
	return EC_SUCCESS;
}

int read_sn_data(struct sn_data *sn)
{
	if (sn)
		memset(sn, 0x42, sizeof(*sn));
	return EC_SUCCESS;
}

int read_factory_config(uint64_t *fc)
{
	if (fc)
		*fc = 0x0102030405060708ULL;
	return EC_SUCCESS;
}

int get_rma_device_id(uint8_t rma_device_id[RMA_DEVICE_ID_SIZE])
{
	memset(rma_device_id, 0x55, RMA_DEVICE_ID_SIZE);
	return EC_SUCCESS;
}

int u2f_get_cert(uint8_t *buf, size_t len)
{
	memset(buf, 0xAA, len);
	return len;
}

#ifdef CONFIG_PLATFORM_BOOT_PARAM
size_t get_dice_chain_bytes(void *buf, size_t offset, size_t size)
{
	memset(buf, 0xBB, size);
	return size;
}

size_t get_boot_param_bytes(void *buf, size_t offset, size_t size)
{
	memset(buf, 0xCC, size);
	return size;
}
#endif

/* Include actual implementation files */
#include "../common/ccd_config.c"
#include "../board/cr50/tpm2/virtual_nvmem.c"

static int test_read_ccd_flags_sanitization(void)
{
	uint8_t out = 0xFF;

	/* Null pointer check */
	TEST_ASSERT(read_ccd_flags(NULL) == EC_ERROR_INVAL);

	/* Uninitialized ccd config returns 0 */
	ccd_config_loaded = 0;
	force_disabled = 0;
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0);

	/* Initialize ccd config */
	reset_mock_nvmem();
	ccd_config_init(CCD_STATE_LOCKED);
	TEST_ASSERT(ccd_config_loaded != 0);

	/* Default state: no flags set -> returns 0 */
	memset(config.flags, 0, sizeof(config.flags));
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0);

	/* Set Bit 0 (Testlab) */
	raw_set_flag(CCD_FLAG_TESTLAB, 1);
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0x01);
	raw_set_flag(CCD_FLAG_TESTLAB, 0);

	/*
	 * Set Bit 1 (Internal flag: Password set when unlocked) ->
	 * Must be sanitized/masked out.
	 */
	raw_set_flag(CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED, 1);
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0x00);
	raw_set_flag(CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED, 0);

	/* Set Bit 2 (Factory Mode) */
	raw_set_flag(CCD_FLAG_FACTORY_MODE_ENABLED, 1);
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0x04);
	raw_set_flag(CCD_FLAG_FACTORY_MODE_ENABLED, 0);

	/* Set Bit 3 (RMA Mode) */
	raw_set_flag(CCD_FLAG_RMA_MODE, 1);
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0x08);
	raw_set_flag(CCD_FLAG_RMA_MODE, 0);

	/* Set all public flags together */
	raw_set_flag(CCD_FLAG_TESTLAB, 1);
	raw_set_flag(CCD_FLAG_FACTORY_MODE_ENABLED, 1);
	raw_set_flag(CCD_FLAG_RMA_MODE, 1);
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0x0D);

	/* Set boot override and internal flags alongside public flags */
	raw_set_flag(CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED, 1);
	raw_set_flag(CCD_FLAG_RDDKEEPALIVE_AT_BOOT, 1);
	raw_set_flag(CCD_FLAG_OVERRIDE_WP_AT_BOOT, 1);
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0x0D);

	/* When force_disabled is set, returns 0 */
	force_disabled = 1;
	TEST_ASSERT(read_ccd_flags(&out) == EC_SUCCESS);
	TEST_ASSERT(out == 0x00);
	force_disabled = 0;

	return EC_SUCCESS;
}

static int test_virtual_nvmem_ccd_flags(void)
{
	uint8_t buf[4];
	NV_INDEX nv_index;
	uint32_t v_offset;

	TEST_ASSERT(VIRTUAL_NV_INDEX_RMA_BYTES_UNIMPLEMENTED == 0x013fff04);
	TEST_ASSERT(VIRTUAL_NV_INDEX_CCD_FLAGS == 0x013fff0b);

	/* Check that deprecated RMA index has dataSize == 0 */
	v_offset = _plat__NvGetHandleVirtualOffset(
		VIRTUAL_NV_INDEX_RMA_BYTES_UNIMPLEMENTED);
	TEST_ASSERT(v_offset != 0);
	memset(&nv_index, 0, sizeof(nv_index));
	_plat__NvVirtualMemoryRead(v_offset + NV_INDEX_READ_OFFSET,
				   sizeof(NV_INDEX), &nv_index);
	TEST_ASSERT(nv_index.publicArea.nvIndex ==
		    VIRTUAL_NV_INDEX_RMA_BYTES_UNIMPLEMENTED);
	TEST_ASSERT(nv_index.publicArea.dataSize == 0);

	/* Get virtual offset for VIRTUAL_NV_INDEX_CCD_FLAGS */
	v_offset = _plat__NvGetHandleVirtualOffset(VIRTUAL_NV_INDEX_CCD_FLAGS);
	TEST_ASSERT(v_offset != 0);
	TEST_ASSERT(_plat__NvOffsetIsVirtual(v_offset));

	/* Read NV_INDEX structure via _plat__NvVirtualMemoryRead */
	memset(&nv_index, 0, sizeof(nv_index));
	_plat__NvVirtualMemoryRead(v_offset + NV_INDEX_READ_OFFSET,
				   sizeof(NV_INDEX), &nv_index);
	TEST_ASSERT(nv_index.publicArea.nvIndex == VIRTUAL_NV_INDEX_CCD_FLAGS);
	TEST_ASSERT(nv_index.publicArea.dataSize ==
		    VIRTUAL_NV_INDEX_CCD_FLAGS_SIZE);
	TEST_ASSERT(nv_index.publicArea.attributes.TPMA_NV_AUTHREAD == 1);
	TEST_ASSERT(nv_index.publicArea.attributes.TPMA_NV_PPREAD == 1);
	TEST_ASSERT(nv_index.publicArea.attributes.TPMA_NV_WRITEDEFINE == 1);
	TEST_ASSERT(nv_index.publicArea.attributes.TPMA_NV_WRITELOCKED == 1);
	TEST_ASSERT(nv_index.publicArea.attributes.TPMA_NV_AUTHWRITE == 0);
	TEST_ASSERT(nv_index.publicArea.attributes.TPMA_NV_PPWRITE == 0);

	/* Initialize CCD config and reset flags */
	reset_mock_nvmem();
	force_disabled = 0;
	ccd_config_loaded = 0;
	ccd_config_init(CCD_STATE_LOCKED);
	memset(config.flags, 0, sizeof(config.flags));

	/* Default state: read returns 0 */
	memset(buf, 0xA5, sizeof(buf));
	_plat__NvVirtualMemoryRead(v_offset + NV_DATA_READ_OFFSET, 1, buf);
	TEST_ASSERT(buf[0] == 0);

	/* Set Testlab and RMA flags */
	raw_set_flag(CCD_FLAG_TESTLAB, 1);
	raw_set_flag(CCD_FLAG_RMA_MODE, 1);

	/* Read 1 byte at NV_DATA_READ_OFFSET */
	memset(buf, 0xA5, sizeof(buf));
	_plat__NvVirtualMemoryRead(v_offset + NV_DATA_READ_OFFSET, 1, buf);
	TEST_ASSERT(buf[0] == (CCD_FLAG_TESTLAB | CCD_FLAG_RMA_MODE));
	TEST_ASSERT(buf[1] == 0xA5);

	/* Set internal flags too, verify they are sanitized */
	raw_set_flag(CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED, 1);
	raw_set_flag(CCD_FLAG_OVERRIDE_WP_AT_BOOT, 1);
	raw_set_flag(CCD_FLAG_FACTORY_MODE_ENABLED, 1);
	memset(buf, 0, sizeof(buf));
	_plat__NvVirtualMemoryRead(v_offset + NV_DATA_READ_OFFSET, 1, buf);
	TEST_ASSERT(buf[0] == 0x0D);

	/* Read out of bounds: offset past data size */
	memset(buf, 0x5A, sizeof(buf));
	_plat__NvVirtualMemoryRead(v_offset + NV_DATA_READ_OFFSET + 1, 1, buf);
	TEST_ASSERT(buf[0] == 0);

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();
	RUN_TEST(test_read_ccd_flags_sanitization);
	RUN_TEST(test_virtual_nvmem_ccd_flags);
	test_print_result();
}
