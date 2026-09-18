/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chre/core/static_nanoapps.h"
#include "chre/target_platform/log.h"
#include "mock_chre.h"
#include "tagged_log_test.h"

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(chre_lifecycle, NULL, NULL, NULL, NULL, NULL);

ZTEST(chre_lifecycle, test_chre_thread_and_lifecycle)
{
	mock_chre::State &state = mock_chre::getState();

	/* Wait for the CHRE thread to enter its event loop */
	zassert_ok(k_sem_take(&state.sem_started, K_MSEC(1000)),
		   "Timed out waiting for CHRE thread to start");

	/* Verify thread properties */
	zassert_not_null(state.thread_tid, "CHRE thread tid was not recorded");
	zassert_str_equal(k_thread_name_get(state.thread_tid), "chre",
			  "CHRE thread name mismatch");

	/* Verify lifecycle sequence prior to run */
	zassert_equal(state.call_count, 5,
		      "Expected 5 calls before event loop wait, got %zu",
		      state.call_count);
	zassert_equal(state.call_sequence[0], mock_chre::Step::kRegisterTask);
	zassert_equal(state.call_sequence[1], mock_chre::Step::kInitCommon);
	zassert_equal(state.call_sequence[2], mock_chre::Step::kLateInit);
	zassert_equal(state.call_sequence[3], mock_chre::Step::kLoadNanoapps);
	zassert_equal(state.call_sequence[4], mock_chre::Step::kRun);

	/* Verify static nanoapp configuration */
#if defined(CONFIG_CHRE_TIMER_WORLD) && defined(CONFIG_CHRE_HELLO_WORLD)
	zassert_equal(chre::kStaticNanoappCount, 2,
		      "Expected 2 static nanoapps");
	zassert_true(chre::kStaticNanoappList[0] ==
			     chre::initializeStaticNanoappTimerWorld,
		     "Expected TimerWorld nanoapp in static list");
	zassert_true(chre::kStaticNanoappList[1] ==
			     chre::initializeStaticNanoappHelloWorld,
		     "Expected HelloWorld nanoapp in static list");
	zassert_equal(state.nanoapp_init_count, 2,
		      "Expected 2 nanoapps initialized");
#elif defined(CONFIG_CHRE_TIMER_WORLD)
	zassert_equal(chre::kStaticNanoappCount, 1,
		      "Expected 1 static nanoapp");
	zassert_true(chre::kStaticNanoappList[0] ==
			     chre::initializeStaticNanoappTimerWorld,
		     "Expected TimerWorld nanoapp in static list");
	zassert_equal(state.nanoapp_init_count, 1,
		      "Expected TimerWorld nanoapp to have been initialized");
#elif defined(CONFIG_CHRE_HELLO_WORLD)
	zassert_equal(chre::kStaticNanoappCount, 1,
		      "Expected 1 static nanoapp");
	zassert_true(chre::kStaticNanoappList[0] ==
			     chre::initializeStaticNanoappHelloWorld,
		     "Expected HelloWorld nanoapp in static list");
	zassert_equal(state.nanoapp_init_count, 1,
		      "Expected HelloWorld nanoapp to have been initialized");
#else
	zassert_equal(chre::kStaticNanoappCount, 0,
		      "Expected 0 static nanoapps");
	zassert_equal(state.nanoapp_init_count, 0,
		      "Expected 0 nanoapps initialized");
#endif

	/* Allow the event loop to exit */
	k_sem_give(&state.sem_allow_exit);

	/* Wait for thread to deinitialize and finish */
	zassert_ok(k_sem_take(&state.sem_finished, K_MSEC(1000)),
		   "Timed out waiting for CHRE thread to finish");

	/* Verify deinit was called */
	zassert_equal(state.call_count, 6,
		      "Expected 6 total lifecycle calls, got %zu",
		      state.call_count);
	zassert_equal(state.call_sequence[5], mock_chre::Step::kDeinitCommon);
}

ZTEST_SUITE(chre_logging, NULL, NULL, NULL, NULL, NULL);

ZTEST(chre_logging, test_chre_log_module_registration)
{
	int16_t source_id = log_source_id_get("chre");

	zassert_true(source_id >= 0, "chre log module was not registered");
	zassert_str_equal(log_source_name_get(Z_LOG_LOCAL_DOMAIN_ID, source_id),
			  "chre", "chre module name mismatch");
}

ZTEST(chre_logging, test_chre_log_level_constants)
{
	zassert_equal(CHRE_LOG_LEVEL_ERR, 1);
	zassert_equal(CHRE_LOG_LEVEL_WRN, 2);
	zassert_equal(CHRE_LOG_LEVEL_INF, 3);
	zassert_equal(CHRE_LOG_LEVEL_DBG, 4);
}

ZTEST(chre_logging, test_chre_ec_log_direct)
{
	chre_ec_log(CHRE_LOG_LEVEL_ERR, "Direct error test: %d", 1);
	chre_ec_log(CHRE_LOG_LEVEL_WRN, "Direct warning test");
	chre_ec_log(CHRE_LOG_LEVEL_INF, "Direct info test: %s", "ok");
	chre_ec_log(CHRE_LOG_LEVEL_DBG, "Direct debug test");
}

ZTEST(chre_logging, test_chre_log_runtime_filtering)
{
	int16_t source_id = log_source_id_get("chre");

	zassert_true(source_id >= 0, "chre log module was not found");

	/* Set runtime level to INFO: DBG should be filtered out by chre_ec_log
	 */
	log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, LOG_LEVEL_INF);
	chre_ec_log(CHRE_LOG_LEVEL_DBG, "Filtered debug message");

	/* Elevate runtime level to DBG: DBG should now pass */
	log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, LOG_LEVEL_DBG);
	chre_ec_log(CHRE_LOG_LEVEL_DBG, "Active debug message");

	/* Restore to INFO */
	log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, LOG_LEVEL_INF);
}

ZTEST(chre_logging, test_chre_log_macros_untagged)
{
	LOGE("Untagged error: %d", 42);
	LOGW("Untagged warning: %s", "check");
	LOGI("Untagged info message");
	LOGD("Untagged debug message");
}

ZTEST(chre_logging, test_chre_log_macros_tagged)
{
	trigger_tagged_logs();
}
