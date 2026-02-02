/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"

#include <ascp/ascp.h>
#ifdef CONFIG_ASCP_ET171
#include "et171.h"
#endif

int get_ascp_claim(struct ec_response_fp_ascp_claim *res)
{
	memcpy(res->pk_m, (uint8_t *)ASCP_ADDR_PK_M, sizeof(res->pk_m));
	memcpy(res->s_goog, (uint8_t *)ASCP_ADDR_S_GOOG, sizeof(res->s_goog));
	memcpy(res->pk_d, (uint8_t *)ASCP_ADDR_PK_D, sizeof(res->pk_d));
	memcpy(res->s_m, (uint8_t *)ASCP_ADDR_S_M, sizeof(res->s_m));
	memcpy(res->pk_f, (uint8_t *)ASCP_ADDR_PK_F, sizeof(res->pk_f));
	memcpy(res->h_f, (uint8_t *)ASCP_ADDR_H_F, sizeof(res->h_f));
	memcpy(res->s_d, (uint8_t *)ASCP_ADDR_S_D, sizeof(res->s_d));

	return EC_SUCCESS;
}

const uint8_t *get_ascp_sk_f()
{
	return (const uint8_t *)ASCP_ADDR_SK_F;
}

struct ascp_api ascp_api_instance = {
	.get_claim = get_ascp_claim,
	.get_sk_f = get_ascp_sk_f,
};
