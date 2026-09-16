/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void chre_ec_log(uint8_t level, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));

#ifdef __cplusplus
}
#endif

#define CHRE_LOG_LEVEL_ERR 1
#define CHRE_LOG_LEVEL_WRN 2
#define CHRE_LOG_LEVEL_INF 3
#define CHRE_LOG_LEVEL_DBG 4

#ifdef LOG_TAG
#define _CHRE_LOG_TAG LOG_TAG " "
#else
#define _CHRE_LOG_TAG ""
#endif

#define LOGE(fmt, ...) \
	chre_ec_log(CHRE_LOG_LEVEL_ERR, _CHRE_LOG_TAG fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) \
	chre_ec_log(CHRE_LOG_LEVEL_WRN, _CHRE_LOG_TAG fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) \
	chre_ec_log(CHRE_LOG_LEVEL_INF, _CHRE_LOG_TAG fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) \
	chre_ec_log(CHRE_LOG_LEVEL_DBG, _CHRE_LOG_TAG fmt, ##__VA_ARGS__)
