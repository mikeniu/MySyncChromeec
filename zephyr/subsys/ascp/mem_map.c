/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"

#include <ascp/ascp.h>

#define PK_M_ADDR DT_PROP(DT_NODELABEL(ascp), pk_m)
#define S_GOOG_ADDR DT_PROP(DT_NODELABEL(ascp), s_goog)
#define PK_D_ADDR DT_PROP(DT_NODELABEL(ascp), pk_d)
#define S_M_ADDR DT_PROP(DT_NODELABEL(ascp), s_m)
#define PK_F_ADDR DT_PROP(DT_NODELABEL(ascp), pk_f)
#define H_F_ADDR DT_PROP(DT_NODELABEL(ascp), h_f)
#define S_D_ADDR DT_PROP(DT_NODELABEL(ascp), s_d)
#define SK_F_ADDR DT_PROP(DT_NODELABEL(ascp), sk_f)

int get_ascp_claim(struct ec_response_fp_ascp_claim *res)
{
	memcpy(res->pk_m, (uint8_t *)PK_M_ADDR, sizeof(res->pk_m));
	memcpy(res->s_goog, (uint8_t *)S_GOOG_ADDR, sizeof(res->s_goog));
	memcpy(res->pk_d, (uint8_t *)PK_D_ADDR, sizeof(res->pk_d));
	memcpy(res->s_m, (uint8_t *)S_M_ADDR, sizeof(res->s_m));
	memcpy(res->pk_f, (uint8_t *)PK_F_ADDR, sizeof(res->pk_f));
	memcpy(res->h_f, (uint8_t *)H_F_ADDR, sizeof(res->h_f));
	memcpy(res->s_d, (uint8_t *)S_D_ADDR, sizeof(res->s_d));

	return EC_SUCCESS;
}

int get_ascp_sk_f(uint8_t *buffer, size_t len)
{
	memcpy(buffer, (uint8_t *)SK_F_ADDR, len);
	return EC_SUCCESS;
}
