/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TANJIRO_SCP_INCLUDE_IPI_CHIP_H_
#define ZEPHYR_TANJIRO_SCP_INCLUDE_IPI_CHIP_H_

#include <stdint.h>

#define SCP_IPI_INIT 0
#define SCP_IPI_VDEC_H264 1
#define SCP_IPI_VDEC_VP8 2
#define SCP_IPI_VDEC_VP9 3
#define SCP_IPI_VENC_H264 4
#define SCP_IPI_VENC_VP8 5
#define SCP_IPI_MDP_INIT 6
#define SCP_IPI_MDP_DEINIT 7
#define SCP_IPI_MDP_FRAME 8
#define SCP_IPI_DIP 9
#define SCP_IPI_ISP_CMD 10
#define SCP_IPI_ISP_FRAME 11
#define SCP_IPI_FD_CMD 12
#define SCP_IPI_HOST_COMMAND 13
#define SCP_IPI_VDEC_LAT 14
#define SCP_IPI_VDEC_CORE 15
#define SCP_IPI_ISP_IMG_CMD 16
#define SCP_IPI_COUNT 17

#define IPI_COUNT SCP_IPI_COUNT

#define SCP_IPI_NS_SERVICE 0xFF

/*
 * Length of EC version string is at most 32 byte (NULL included), which
 * also aligns SCP fw_version length.
 */
#define SCP_FW_VERSION_LEN 32

/*
 * Share buffer layout for SCP_IPI_INIT response. This structure should sync
 * across kernel and EC.
 */
struct scp_run_t {
	uint32_t signaled;
	int8_t fw_ver[SCP_FW_VERSION_LEN];
	uint32_t dec_capability;
	uint32_t enc_capability;
};

#endif /* ZEPHYR_TANJIRO_SCP_INCLUDE_IPI_CHIP_H_ */
