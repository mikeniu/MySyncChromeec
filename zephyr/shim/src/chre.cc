/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chre/core/event_loop_manager.h"
#include "chre/core/static_nanoapps.h"
#include "chre/platform/shared/init.h"
#include "chre/target_platform/context.h"
#include "pw_thread/thread.h"

#include <stdarg.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>

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

namespace chre
{
#ifdef CONFIG_CHRE_TIMER_WORLD
UniquePtr<Nanoapp> initializeStaticNanoappTimerWorld();
#endif
#ifdef CONFIG_CHRE_HELLO_WORLD
UniquePtr<Nanoapp> initializeStaticNanoappHelloWorld();
#endif

const StaticNanoappInitFunction kStaticNanoappList[] = {
#ifdef CONFIG_CHRE_TIMER_WORLD
	initializeStaticNanoappTimerWorld,
#endif
#ifdef CONFIG_CHRE_HELLO_WORLD
	initializeStaticNanoappHelloWorld,
#endif
};
const size_t kStaticNanoappCount = ARRAY_SIZE(kStaticNanoappList);
} // namespace chre

#ifndef CONFIG_ZTEST
#include "chre/target_platform/memory_impl.h"
#include "pw_allocator_zephyr/synchronized_heap_allocator.h"

static std::byte chre_heap_buffer[CONFIG_CHRE_DYNAMIC_MEMORY_SIZE] __aligned(
	sizeof(void *));
static pw::allocator_zephyr::SynchronizedHeapAllocator
	chre_allocator(chre_heap_buffer);
#endif

static K_THREAD_STACK_DEFINE(chre_thread_stack, CONFIG_ISH_CHRE_STACK_SIZE);
static struct k_thread chre_thread;

static void chre_thread_entry(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	LOG_INF("Starting CHRE runtime");

#ifndef CONFIG_ZTEST
	chre::setPlatformPigweedAllocator(chre_allocator);
#endif

	chre::registerChreEventLoopTask(pw::this_thread::get_id());
	chre::initCommon();
	chre::EventLoopManagerSingleton::get()->lateInit();
	chre::loadStaticNanoapps();

	chre::EventLoopManagerSingleton::get()->getEventLoop().run();

	chre::deinitCommon();
	LOG_INF("CHRE runtime exited");
}

static int chre_init(void)
{
	k_thread_create(&chre_thread, chre_thread_stack,
			K_THREAD_STACK_SIZEOF(chre_thread_stack),
			chre_thread_entry, NULL, NULL, NULL,
			K_PRIO_PREEMPT(CONFIG_ISH_CHRE_PRIORITY), 0, K_NO_WAIT);
	k_thread_name_set(&chre_thread, "chre");
	return 0;
}

SYS_INIT(chre_init, APPLICATION, 99);
