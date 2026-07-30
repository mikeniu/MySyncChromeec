/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This header declares the TPM manufacture related interface.
 * Individual boards are expected to provide implementations.
 */

#ifndef __CROS_EC_TPM_MANUFACTURE_H
#define __CROS_EC_TPM_MANUFACTURE_H

#include <stdbool.h>

/* Returns non-zero if the TPM manufacture steps have been completed. */
int tpm_manufactured(void);

/* Codes for success and various manufacturing error conditions. */
enum manufacturing_status {
	mnf_success = 0,
	mnf_no_certs = 1,
	mnf_eps_decr = 2,
	mnf_bad_rsa_size = 3,
	mnf_bad_total_size = 4,
	mnf_bad_rsa_type = 5,
	mnf_bad_ecc_type = 6,
	mnf_hmac_mismatch = 7,
	mnf_rsa_proc = 8,
	mnf_ecc_proc = 9,
	mnf_store = 10,
	mnf_manufactured = 11,
	mnf_unverified_cert = 12,
};

enum manufacturing_status tpm_endorse(void);

static inline bool is_keymgr_prod_mode(uint32_t fwr7, uint32_t rwr7)
{
	return (fwr7 == 0) && (rwr7 == 0xaa66150f);
}

#define compute_board_in_prod_mode(keymgr_prod, hmac_valid) \
	((keymgr_prod) && (hmac_valid))

#if defined(SECTION_IS_RO) || defined(CR50_USE_FIXED_CERT)
static inline bool verify_ro_certs_hmac(void) { return true; }
#else
bool verify_ro_certs_hmac(void);
#endif
bool board_keymgr_in_prod_mode(void);

#endif	/* __CROS_EC_TPM_MANUFACTURE_H */
