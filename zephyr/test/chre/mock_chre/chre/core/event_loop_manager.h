/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_EVENT_LOOP_MANAGER_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_EVENT_LOOP_MANAGER_H_

#include "chre/core/nanoapp.h"
#include "chre/util/unique_ptr.h"

#include <cstdint>

namespace chre
{

enum class SystemCallbackType : uint16_t {
	FinishLoadingNanoapp,
	HandleUnloadNanoapp,
};

typedef void(EventCallbackFunction)(uint16_t type, void *data, void *extraData);

class EventLoop {
    public:
	void run();
	bool startNanoapp(UniquePtr<Nanoapp> nanoapp);
	bool unloadNanoapp(uint16_t instanceId, bool allowSystemNanoappUnload);
	bool findNanoappInstanceIdByAppId(uint64_t appId, uint16_t *instanceId);
	Nanoapp *findNanoappByInstanceId(uint16_t instanceId);
};

class EventLoopManager {
    public:
	void lateInit();
	EventLoop &getEventLoop();
	bool deferCallback(SystemCallbackType type, void *data,
			   EventCallbackFunction *callback);
};

class EventLoopManagerSingleton {
    public:
	static EventLoopManager *get();
};

void loadStaticNanoapps();

} // namespace chre

#endif // ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_EVENT_LOOP_MANAGER_H_
