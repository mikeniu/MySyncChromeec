/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"

#include <ascp/ascp.h>

#define DT_ASCP_MEM_MAP_COMPAT cros_ec_ascp_mem_map

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_ASCP_MEM_MAP_COMPAT) == 1,
	     "only one 'cros-ec,ascp-mem-map' compatible node may be present");

#define DT_ASCP_MEM_MAP_NODE(prop) \
	DT_PHANDLE(DT_INST(0, DT_ASCP_MEM_MAP_COMPAT), prop)

#define DT_ASCP_MEM_MAP_NODE_SIZE(prop) DT_REG_SIZE(DT_ASCP_MEM_MAP_NODE(prop))

#define DT_ASCP_MEM_MAP_PARENT_ADDR(prop)                                    \
	COND_CODE_1(DT_NODE_HAS_PROP(DT_GPARENT(DT_ASCP_MEM_MAP_NODE(prop)), \
				     reg),                                   \
		    (DT_REG_ADDR(DT_GPARENT(DT_ASCP_MEM_MAP_NODE(prop)))),   \
		    (CONFIG_FLASH_BASE_ADDRESS))

#define DT_ASCP_MEM_MAP_NODE_ADDR(prop) DT_REG_ADDR(DT_ASCP_MEM_MAP_NODE(prop))

#define DT_ASCP_MEM_MAP_ADDR(prop) \
	(DT_ASCP_MEM_MAP_PARENT_ADDR(prop) + DT_ASCP_MEM_MAP_NODE_ADDR(prop))

#define PK_M_ADDR DT_ASCP_MEM_MAP_ADDR(pk_m)
#define S_GOOG_ADDR DT_ASCP_MEM_MAP_ADDR(s_goog)
#define PK_D_ADDR DT_ASCP_MEM_MAP_ADDR(pk_d)
#define S_M_ADDR DT_ASCP_MEM_MAP_ADDR(s_m)
#define PK_F_ADDR DT_ASCP_MEM_MAP_ADDR(pk_f)
#define H_F_ADDR DT_ASCP_MEM_MAP_ADDR(h_f)
#define S_D_ADDR DT_ASCP_MEM_MAP_ADDR(s_d)
#define SK_F_ADDR DT_ASCP_MEM_MAP_ADDR(sk_f)

int ascp_get_claim(struct ec_response_fp_ascp_claim *res)
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

int ascp_get_sk_f(uint8_t *buffer, size_t len)
{
	memcpy(buffer, (uint8_t *)SK_F_ADDR, len);
	return EC_SUCCESS;
}

BUILD_ASSERT(DT_ASCP_MEM_MAP_NODE_SIZE(pk_m) == FP_ASCP_KEY_SIZE,
	     "pk_m size must be 65 bytes");
BUILD_ASSERT(DT_ASCP_MEM_MAP_NODE_SIZE(s_goog) == FP_ASCP_SIGNATURE_SIZE,
	     "s_goog size must be 64 bytes");
BUILD_ASSERT(DT_ASCP_MEM_MAP_NODE_SIZE(pk_d) == FP_ASCP_KEY_SIZE,
	     "pk_d size must be 65 bytes");
BUILD_ASSERT(DT_ASCP_MEM_MAP_NODE_SIZE(s_m) == FP_ASCP_SIGNATURE_SIZE,
	     "s_m size must be 64 bytes");
BUILD_ASSERT(DT_ASCP_MEM_MAP_NODE_SIZE(pk_f) == FP_ASCP_KEY_SIZE,
	     "pk_f size must be 65 bytes");
BUILD_ASSERT(DT_ASCP_MEM_MAP_NODE_SIZE(h_f) == FP_ASCP_HASH_SIZE,
	     "h_f size must be 32 bytes");
BUILD_ASSERT(DT_ASCP_MEM_MAP_NODE_SIZE(s_d) == FP_ASCP_SIGNATURE_SIZE,
	     "s_d size must be 64 bytes");
BUILD_ASSERT(DT_ASCP_MEM_MAP_NODE_SIZE(sk_f) ==
		     FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN,
	     "sk_f size must be 32 bytes");
