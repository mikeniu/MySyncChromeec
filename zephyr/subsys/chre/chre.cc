/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chre/core/event_loop_manager.h"
#include "chre/core/static_nanoapps.h"
#include "chre/platform/shared/init.h"
#include "chre/target_platform/context.h"
#include "pw_thread/thread.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

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
#include "chre/core/host_endpoint_manager.h"
#include "chre/target_platform/memory_impl.h"
#include "pw_allocator_zephyr/synchronized_heap_allocator.h"

static std::byte chre_heap_buffer[CONFIG_CHRE_DYNAMIC_MEMORY_SIZE] __aligned(
	sizeof(void *));
static pw::allocator_zephyr::SynchronizedHeapAllocator
	chre_allocator(chre_heap_buffer);

/* Force reference to HostEndpointManager::getHostEndpointInfo to prevent the
 * linker from dropping host_endpoint_manager.cc.obj from libchre.core.a
 * before libchre.platform.shared.chre_api_core.a is scanned.
 */
void chre_host_endpoint_symbol_anchor(chre::HostEndpointManager *mgr)
{
	if (mgr != nullptr) {
		mgr->getHostEndpointInfo(0, nullptr);
	}
}
#endif

static K_THREAD_STACK_DEFINE(chre_thread_stack, CONFIG_ISH_CHRE_STACK_SIZE);
static struct k_thread chre_thread;

static void chre_thread_entry(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	printk("Starting CHRE runtime\n");

#ifndef CONFIG_ZTEST
	chre::setPlatformPigweedAllocator(chre_allocator);
#endif

	chre::registerChreEventLoopTask(pw::this_thread::get_id());
	chre::initCommon();
	chre::EventLoopManagerSingleton::get()->lateInit();
	chre::loadStaticNanoapps();

	chre::EventLoopManagerSingleton::get()->getEventLoop().run();

	chre::deinitCommon();
	printk("CHRE runtime exited\n");
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
