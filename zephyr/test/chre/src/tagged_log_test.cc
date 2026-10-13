/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define LOG_TAG "[NanoAppTag]"

#include "chre/target_platform/log.h"
#include "tagged_log_test.h"

void trigger_tagged_logs(void)
{
	LOGE("Tagged error message");
	LOGW("Tagged warning message");
	LOGI("Tagged info message %d", 42);
	LOGD("Tagged debug message");
}
