/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_NANOAPP_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_NANOAPP_H_

#include <cstddef>
#include <cstdint>

namespace chre
{

class Nanoapp {
    public:
	uint64_t getAppId() const
	{
		return appId_;
	}
	bool reserveBuffer(uint64_t appId, uint32_t /*appVersion*/,
			   uint32_t /*appFlags*/, size_t /*nanoappSize*/,
			   uint32_t /*targetApiVersion*/ = 0)
	{
		appId_ = appId;
		return true;
	}
	bool copyNanoappFragment(const void * /*buffer*/, size_t /*size*/)
	{
		return true;
	}
	bool openNanoapp()
	{
		return true;
	}

    private:
	uint64_t appId_ = 0;
};

} // namespace chre

#endif // ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_NANOAPP_H_
