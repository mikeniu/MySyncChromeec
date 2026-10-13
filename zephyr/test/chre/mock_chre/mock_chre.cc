/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chre/core/event_loop_manager.h"
#include "chre/core/static_nanoapps.h"
#include "chre/platform/shared/init.h"
#include "chre/target_platform/context.h"
#include "mock_chre.h"

#include <zephyr/kernel.h>

namespace mock_chre
{

static K_SEM_DEFINE(s_sem_started, 0, 1);
static K_SEM_DEFINE(s_sem_allow_exit, 0, 1);
static K_SEM_DEFINE(s_sem_finished, 0, 1);

static State s_state = {
	.sem_started = s_sem_started,
	.sem_allow_exit = s_sem_allow_exit,
	.sem_finished = s_sem_finished,
	.call_sequence = {},
	.call_count = 0,
	.thread_tid = nullptr,
	.nanoapp_init_count = 0,
};

State &getState()
{
	return s_state;
}

void recordStep(Step step)
{
	if (s_state.call_count <
	    sizeof(s_state.call_sequence) / sizeof(s_state.call_sequence[0])) {
		s_state.call_sequence[s_state.call_count++] = step;
	}
}

} // namespace mock_chre

namespace chre
{

static EventLoop s_event_loop;
static EventLoopManager s_event_loop_manager;

void registerChreEventLoopTask(pw::thread::Id id)
{
	mock_chre::getState().thread_tid = k_current_get();
	mock_chre::recordStep(mock_chre::Step::kRegisterTask);
}

void initCommon()
{
	mock_chre::recordStep(mock_chre::Step::kInitCommon);
}

void deinitCommon()
{
	mock_chre::recordStep(mock_chre::Step::kDeinitCommon);
	k_sem_give(&mock_chre::getState().sem_finished);
}

void EventLoopManager::lateInit()
{
	mock_chre::recordStep(mock_chre::Step::kLateInit);
}

EventLoop &EventLoopManager::getEventLoop()
{
	return s_event_loop;
}

EventLoopManager *EventLoopManagerSingleton::get()
{
	return &s_event_loop_manager;
}

void EventLoop::run()
{
	mock_chre::recordStep(mock_chre::Step::kRun);
	k_sem_give(&mock_chre::getState().sem_started);
	k_sem_take(&mock_chre::getState().sem_allow_exit, K_FOREVER);
}

void loadStaticNanoapps()
{
	mock_chre::recordStep(mock_chre::Step::kLoadNanoapps);
	for (size_t i = 0; i < kStaticNanoappCount; ++i) {
		if (kStaticNanoappList[i] != nullptr) {
			kStaticNanoappList[i]();
		}
	}
}

#ifdef CONFIG_CHRE_TIMER_WORLD
static Nanoapp s_timer_world_nanoapp;

UniquePtr<Nanoapp> initializeStaticNanoappTimerWorld()
{
	mock_chre::getState().nanoapp_init_count++;
	return UniquePtr<Nanoapp>(&s_timer_world_nanoapp);
}
#endif

#ifdef CONFIG_CHRE_HELLO_WORLD
static Nanoapp s_hello_world_nanoapp;

UniquePtr<Nanoapp> initializeStaticNanoappHelloWorld()
{
	mock_chre::getState().nanoapp_init_count++;
	return UniquePtr<Nanoapp>(&s_hello_world_nanoapp);
}
#endif

} // namespace chre
