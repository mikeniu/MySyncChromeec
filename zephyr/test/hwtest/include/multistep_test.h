/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _HWTEST_MULTISTEP_TEST_
#define _HWTEST_MULTISTEP_TEST_

#include <zephyr/devicetree.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_test.h>

#if DT_NODE_EXISTS(DT_ALIAS(retained_mem))
#define MULTISTEP_RETAINED_MEM_DEV DEVICE_DT_GET(DT_ALIAS(retained_mem))
#elif DT_NODE_EXISTS(DT_NODELABEL(retained_mem))
#define MULTISTEP_RETAINED_MEM_DEV DEVICE_DT_GET(DT_NODELABEL(retained_mem))
#elif DT_HAS_COMPAT_STATUS_OKAY(zephyr_retained_ram)
#define MULTISTEP_RETAINED_MEM_DEV DEVICE_DT_GET_ANY(zephyr_retained_ram)
#else
#define MULTISTEP_RETAINED_MEM_DEV NULL
#endif

/* Unique step zero for tests. Divide by 2 not to overflow scratchpad area and
 * shift to assume 0 is an incorrect value for step zero.
 */
#define TEST_STEP_ZERO(steps) \
	((crc16_itu_t(0, (const uint8_t *)(steps), sizeof(steps)) / 2) + 1)

/**
 * @brief Register a multi-step test.
 *
 * Next step will be called after reboot/crash.
 *
 * @param name Name of a test.
 * @param steps Array of steps which cause reboot/crash. Each step is a pointer
 * to a void function that takes no arguments.
 */
#define MULTISTEP_TEST(name, steps)                                          \
	static void *multisetep_test_setup(void)                             \
	{                                                                    \
		uint32_t step = 0;                                           \
		int ret = 0;                                                 \
		const struct device *dev = MULTISTEP_RETAINED_MEM_DEV;       \
                                                                             \
		zassert_not_null(dev, "No retained_mem device found");       \
		zassert_true(device_is_ready(dev),                           \
			     "Retained mem device not ready");               \
                                                                             \
		ret = retained_mem_read(dev, 0, (uint8_t *)&step,            \
					sizeof(step));                       \
		if ((step < TEST_STEP_ZERO(steps)) ||                        \
		    (step >= (TEST_STEP_ZERO(steps) + ARRAY_SIZE(steps)))) { \
			step = TEST_STEP_ZERO(steps);                        \
			retained_mem_write(dev, 0, (const uint8_t *)&step,   \
					   sizeof(step));                    \
		}                                                            \
		zassert_equal(ret, 0);                                       \
                                                                             \
		return NULL;                                                 \
	}                                                                    \
	static void multisetep_test_teardown(void *fixture)                  \
	{                                                                    \
		const struct device *dev = MULTISTEP_RETAINED_MEM_DEV;       \
		uint32_t step = 0;                                           \
		if (dev && device_is_ready(dev)) {                           \
			retained_mem_write(dev, 0, (const uint8_t *)&step,   \
					   sizeof(step));                    \
		}                                                            \
	}                                                                    \
	ZTEST_SUITE(name, NULL, multisetep_test_setup, NULL, NULL,           \
		    multisetep_test_teardown);                               \
	ZTEST(name, test_##name)                                             \
	{                                                                    \
		uint32_t step = 0;                                           \
		int ret = 0;                                                 \
		const struct device *dev = MULTISTEP_RETAINED_MEM_DEV;       \
                                                                             \
		zassert_not_null(dev, "No retained_mem device found");       \
		zassert_true(device_is_ready(dev),                           \
			     "Retained mem device not ready");               \
                                                                             \
		ret = retained_mem_read(dev, 0, (uint8_t *)&step,            \
					sizeof(step));                       \
		zassert_equal(ret, 0);                                       \
		step++;                                                      \
		ret = retained_mem_write(dev, 0, (const uint8_t *)&step,     \
					 sizeof(step));                      \
                                                                             \
		steps[step - 1 - TEST_STEP_ZERO(steps)]();                   \
	}                                                                    \
                                                                             \
	/* If the test shell is enabled, the test will be run once by a test \
	 * runner. The steps call cause reboot/crash, which means we need to \
	 * run it again.                                                     \
	 */                                                                  \
	IF_ENABLED(CONFIG_ZTEST_SHELL,                                       \
		   (                                                         \
			   static struct k_work multistep_test_work;         \
			   static void multistep_test_handler(               \
				   struct k_work *work) {                    \
				   uint32_t step = 0;                        \
				   const struct device *dev =                \
					   MULTISTEP_RETAINED_MEM_DEV;       \
                                                                             \
				   if (!dev || !device_is_ready(dev)) {      \
					   return;                           \
				   }                                         \
                                                                             \
				   retained_mem_read(dev, 0,                 \
						     (uint8_t *)&step,       \
						     sizeof(step));          \
				   if ((step > TEST_STEP_ZERO(steps)) &&     \
				       (step < (TEST_STEP_ZERO(steps) +      \
						ARRAY_SIZE(steps)))) {       \
					   ztest_run_test_suite(name, false, \
								1, 1, NULL); \
				   }                                         \
			   }                                                 \
                                                                             \
			   static int multistep_test_init(void) {            \
				   k_work_init(&multistep_test_work,         \
					       multistep_test_handler);      \
                                                                             \
				   /* Check if the test has to be run after  \
				    * reboot */                              \
				   k_work_submit(&multistep_test_work);      \
                                                                             \
				   return 0;                                 \
			   } SYS_INIT(multistep_test_init, POST_KERNEL,      \
				      CONFIG_APPLICATION_INIT_PRIORITY);))

#endif /* _HWTEST_MULTISTEP_TEST_ */
