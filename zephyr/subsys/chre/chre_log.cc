/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chre/target_platform/log.h"

#include <stdarg.h>
#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>

#include <pw_log/levels.h>

LOG_MODULE_REGISTER(chre, LOG_LEVEL_INF);

extern "C" void chre_ec_log(uint8_t level, const char *fmt, ...)
{
#if CONFIG_LOG_RUNTIME_FILTERING
	if (level >
	    Z_LOG_RUNTIME_FILTER(((const struct log_source_dynamic_data *)
					  Z_LOG_CURRENT_DATA())
					 ->filters)) {
		return;
	}
#endif
	va_list ap;
	va_start(ap, fmt);
	z_log_msg_runtime_vcreate(Z_LOG_LOCAL_DOMAIN_ID, Z_LOG_CURRENT_DATA(),
				  level, NULL, 0, Z_LOG_MSG_CBPRINTF_FLAGS(0),
				  fmt, ap);
	va_end(ap);
}

extern "C" void pw_log_string_HandleMessageVaList(int level, unsigned int flags,
						  const char *module_name,
						  const char *file_name,
						  int line_number,
						  const char *message,
						  va_list args)
{
	ARG_UNUSED(flags);
	ARG_UNUSED(module_name);
	ARG_UNUSED(file_name);
	ARG_UNUSED(line_number);

	uint8_t zephyr_level;
	switch (level) {
	case PW_LOG_LEVEL_DEBUG:
		zephyr_level = LOG_LEVEL_DBG;
		break;
	case PW_LOG_LEVEL_INFO:
		zephyr_level = LOG_LEVEL_INF;
		break;
	case PW_LOG_LEVEL_WARN:
		zephyr_level = LOG_LEVEL_WRN;
		break;
	case PW_LOG_LEVEL_ERROR:
	case PW_LOG_LEVEL_CRITICAL:
	case PW_LOG_LEVEL_FATAL:
	default:
		zephyr_level = LOG_LEVEL_ERR;
		break;
	}

#if CONFIG_LOG_RUNTIME_FILTERING
	if (zephyr_level >
	    Z_LOG_RUNTIME_FILTER(((const struct log_source_dynamic_data *)
					  Z_LOG_CURRENT_DATA())
					 ->filters)) {
		return;
	}
#endif
	z_log_msg_runtime_vcreate(Z_LOG_LOCAL_DOMAIN_ID, Z_LOG_CURRENT_DATA(),
				  zephyr_level, NULL, 0,
				  Z_LOG_MSG_CBPRINTF_FLAGS(0), message, args);
}
