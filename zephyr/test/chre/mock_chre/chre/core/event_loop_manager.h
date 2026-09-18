/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_EVENT_LOOP_MANAGER_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_EVENT_LOOP_MANAGER_H_

namespace chre
{

class EventLoop {
    public:
	void run();
};

class EventLoopManager {
    public:
	void lateInit();
	EventLoop &getEventLoop();
};

class EventLoopManagerSingleton {
    public:
	static EventLoopManager *get();
};

void loadStaticNanoapps();

} // namespace chre

#endif // ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_EVENT_LOOP_MANAGER_H_
