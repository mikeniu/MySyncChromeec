/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chre/core/event_loop_manager.h"
#include "chre/core/multi_threading_api_mutex.h"
#include "chre/core/nanoapp.h"
#include "chre/target_platform/context.h"
#include "chre/util/memory.h"
#include "chre/util/unique_ptr.h"

#ifdef ARRAY_END
#undef ARRAY_END
#endif
#include "ec_commands.h"
#include "host_command.h"

#include <inttypes.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(chre, LOG_LEVEL_INF);

namespace
{

class KMutexLock {
    public:
	explicit KMutexLock(struct k_mutex *mutex)
		: mutex_(mutex)
	{
		k_mutex_lock(mutex_, K_FOREVER);
	}
	~KMutexLock()
	{
		k_mutex_unlock(mutex_);
	}

    private:
	struct k_mutex *mutex_;
};

} // namespace

struct ChreNanoappLoadState {
	chre::UniquePtr<chre::Nanoapp> nanoapp;
	uint64_t app_id;
	uint32_t total_size;
	uint32_t loaded_size;
	bool in_progress;
};

static struct ChreNanoappLoadState s_load_state;
static K_MUTEX_DEFINE(s_load_mutex);

struct StartNanoappData {
	chre::UniquePtr<chre::Nanoapp> nanoapp;
	uint64_t app_id;
	struct k_sem sem;
	bool success;
	uint32_t instance_id;
};

static void start_nanoapp_callback(uint16_t /*type*/, void *data,
				   void * /*extraData*/)
{
	auto *cb = static_cast<StartNanoappData *>(data);
	chre::EventLoop &eventLoop =
		chre::EventLoopManagerSingleton::get()->getEventLoop();
	uint64_t actual_app_id = cb->nanoapp->getAppId();
	if (actual_app_id != 0) {
		cb->app_id = actual_app_id;
	}
	/* The global API mutex must always be unlocked prior to calling into
	 * nanoapp entrypoints (such as startNanoapp) to prevent deadlocks.
	 */
	auto *mutex = chre::getMultiThreadingApiMutex();
	mutex->unlock();
	cb->success = eventLoop.startNanoapp(std::move(cb->nanoapp));
	mutex->lock();
	if (cb->success) {
		uint16_t inst_id = 0;
		if (eventLoop.findNanoappInstanceIdByAppId(cb->app_id,
							   &inst_id)) {
			cb->instance_id = inst_id;
		}
	}
	k_sem_give(&cb->sem);
}

static enum ec_host_cmd_status
hc_chre_load_nanoapp(struct ec_host_cmd_handler_args *args)
{
	KMutexLock lock(&s_load_mutex);

	const auto *params =
		static_cast<const struct ec_params_chre_load_nanoapp *>(
			args->input_buf);
	auto *resp = static_cast<struct ec_response_chre_load_nanoapp *>(
		args->output_buf);

	if (args->input_buf_size < sizeof(*params) ||
	    args->input_buf_size < sizeof(*params) + params->chunk_size) {
		LOG_ERR("Invalid params size %u", args->input_buf_size);
		return EC_HOST_CMD_INVALID_PARAM;
	}

	if (params->flags & EC_CHRE_LOAD_FLAG_ABORT) {
		s_load_state.nanoapp.reset();
		s_load_state.in_progress = false;
		return EC_HOST_CMD_SUCCESS;
	}

	if (params->flags & EC_CHRE_LOAD_FLAG_START) {
		s_load_state.nanoapp = chre::MakeUnique<chre::Nanoapp>();
		if (s_load_state.nanoapp.isNull()) {
			LOG_ERR("Failed to allocate Nanoapp");
			return EC_HOST_CMD_ERROR;
		}
		if (!s_load_state.nanoapp->reserveBuffer(
			    params->app_id, 0 /* version */, 0 /* flags */,
			    params->total_binary_size,
			    0 /* targetApiVersion */)) {
			LOG_ERR("Failed to reserve %u bytes for app "
				"0x%016" PRIx64,
				params->total_binary_size, params->app_id);
			s_load_state.nanoapp.reset();
			return EC_HOST_CMD_ERROR;
		}
		s_load_state.app_id = params->app_id;
		s_load_state.total_size = params->total_binary_size;
		s_load_state.loaded_size = 0;
		s_load_state.in_progress = true;
	}

	if (!s_load_state.in_progress || s_load_state.nanoapp.isNull()) {
		LOG_ERR("No load in progress");
		return EC_HOST_CMD_INVALID_PARAM;
	}

	if (params->offset != s_load_state.loaded_size ||
	    s_load_state.loaded_size + params->chunk_size >
		    s_load_state.total_size) {
		LOG_ERR("Offset mismatch: expected %u, got %u, "
			"chunk %u, total %u",
			s_load_state.loaded_size, params->offset,
			params->chunk_size, s_load_state.total_size);
		return EC_HOST_CMD_INVALID_PARAM;
	}

	if (params->chunk_size > 0) {
		if (!s_load_state.nanoapp->copyNanoappFragment(
			    params->data, params->chunk_size)) {
			LOG_ERR("Failed to copy fragment");
			s_load_state.nanoapp.reset();
			s_load_state.in_progress = false;
			return EC_HOST_CMD_ERROR;
		}
		s_load_state.loaded_size += params->chunk_size;
	}

	if (params->flags & EC_CHRE_LOAD_FLAG_FINISH) {
		if (s_load_state.loaded_size != s_load_state.total_size) {
			LOG_ERR("Size mismatch at finish: loaded %u != "
				"total %u",
				s_load_state.loaded_size,
				s_load_state.total_size);
			s_load_state.nanoapp.reset();
			s_load_state.in_progress = false;
			return EC_HOST_CMD_INVALID_PARAM;
		}

		if (!s_load_state.nanoapp->openNanoapp()) {
			LOG_ERR("Failed to open nanoapp ELF");
			s_load_state.nanoapp.reset();
			s_load_state.in_progress = false;
			return EC_HOST_CMD_ERROR;
		}

		StartNanoappData start_data;
		start_data.nanoapp = std::move(s_load_state.nanoapp);
		start_data.app_id = s_load_state.app_id;
		k_sem_init(&start_data.sem, 0, 1);
		start_data.success = false;
		start_data.instance_id = 0;

		s_load_state.in_progress = false;

		if (!chre::EventLoopManagerSingleton::get()->deferCallback(
			    chre::SystemCallbackType::FinishLoadingNanoapp,
			    &start_data, start_nanoapp_callback)) {
			LOG_ERR("Failed to defer start_nanoapp callback");
			return EC_HOST_CMD_ERROR;
		}

		/* Wait for the event loop to execute the callback. Using
		 * K_FOREVER ensures start_data on the stack is not destroyed
		 * prematurely before the deferred callback executes.
		 */
		k_sem_take(&start_data.sem, K_FOREVER);

		if (!start_data.success) {
			LOG_ERR("Failed to start nanoapp on event loop");
			return EC_HOST_CMD_ERROR;
		}

		resp->instance_id = start_data.instance_id;
		resp->reserved = 0;
		resp->app_id = start_data.app_id;
		args->output_buf_size = sizeof(*resp);
		return EC_HOST_CMD_SUCCESS;
	}

	args->output_buf_size = 0;
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_CHRE_LOAD_NANOAPP, hc_chre_load_nanoapp,
		    EC_VER_MASK(0), struct ec_params_chre_load_nanoapp,
		    struct ec_response_chre_load_nanoapp);

struct UnloadNanoappData {
	uint64_t app_id;
	struct k_sem sem;
	bool success;
};

static void unload_nanoapp_callback(uint16_t /*type*/, void *data,
				    void * /*extraData*/)
{
	auto *cb = static_cast<UnloadNanoappData *>(data);
	chre::EventLoop &eventLoop =
		chre::EventLoopManagerSingleton::get()->getEventLoop();
	uint16_t instance_id = 0;
	if (!eventLoop.findNanoappInstanceIdByAppId(cb->app_id, &instance_id)) {
		if (cb->app_id <= UINT16_MAX &&
		    eventLoop.findNanoappByInstanceId(
			    static_cast<uint16_t>(cb->app_id)) != nullptr) {
			instance_id = static_cast<uint16_t>(cb->app_id);
		} else {
			LOG_ERR("Nanoapp 0x%016" PRIx64 " not found",
				cb->app_id);
			cb->success = false;
			k_sem_give(&cb->sem);
			return;
		}
	}

	/* The global API mutex must always be unlocked prior to calling into
	 * nanoapp entrypoints (such as unloadNanoapp) to prevent deadlocks.
	 */
	auto *mutex = chre::getMultiThreadingApiMutex();
	mutex->unlock();
	cb->success =
		eventLoop.unloadNanoapp(instance_id,
					/*allowSystemNanoappUnload=*/true);
	mutex->lock();
	k_sem_give(&cb->sem);
}

static enum ec_host_cmd_status
hc_chre_unload_nanoapp(struct ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_chre_unload_nanoapp *>(
			args->input_buf);

	if (args->input_buf_size < sizeof(*params)) {
		return EC_HOST_CMD_INVALID_PARAM;
	}

	UnloadNanoappData unload_data;
	unload_data.app_id = params->app_id;
	k_sem_init(&unload_data.sem, 0, 1);
	unload_data.success = false;

	if (!chre::EventLoopManagerSingleton::get()->deferCallback(
		    chre::SystemCallbackType::HandleUnloadNanoapp, &unload_data,
		    unload_nanoapp_callback)) {
		LOG_ERR("Failed to defer unload_nanoapp callback");
		return EC_HOST_CMD_ERROR;
	}

	/* Wait for the event loop to execute the callback. Using K_FOREVER
	 * ensures unload_data on the stack is not destroyed prematurely
	 * before the deferred callback executes.
	 */
	k_sem_take(&unload_data.sem, K_FOREVER);

	if (!unload_data.success) {
		LOG_ERR("Failed to unload nanoapp 0x%016" PRIx64,
			params->app_id);
		return EC_HOST_CMD_ERROR;
	}

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_CHRE_UNLOAD_NANOAPP, hc_chre_unload_nanoapp,
			     EC_VER_MASK(0),
			     struct ec_params_chre_unload_nanoapp);
