/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_TRACE_MSG_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_TRACE_MSG_H_

#include "common.h"

#include <zephyr/shell/shell.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset the message FIFO
 */
__test_only void pdc_trace_msg_fifo_reset(void);

/**
 * @brief Control PDC message tracing
 *
 * @param port Type-C port number or
 *             EC_PDC_TRACE_MSG_PORT_NONE to disable or
 *             EC_PDC_TRACE_MSG_PORT_ALL to enable on all ports
 *
 * @retval previous port number
 */
__test_only int pdc_trace_msg_enable(int port);

/**
 * @brief PDC trace console sub-command
 */
int cmd_pdc_trace(const struct shell *sh, int argc, const char **argv);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_TRACE_MSG_H_ */
