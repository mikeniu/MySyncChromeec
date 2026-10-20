/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chre/core/event_loop_manager.h"
#include "chre/core/host_comms_manager.h"
#include "chre/core/multi_threading_api_mutex.h"
#include "chre/core/nanoapp.h"
#include "chre/platform/host_link.h"
#include "chre/platform/shared/fbs/host_messages_generated.h"
#include "chre/platform/shared/host_protocol_chre.h"
#include "chre/platform/shared/nanoapp_load_manager.h"
#include "chre/platform/system_time.h"
#include "chre/util/dynamic_vector.h"
#include "chre/util/flatbuffers/helpers.h"
#include "chre/util/memory.h"
#include "chre_api/chre/re.h"
#include "chre_api/chre/version.h"

#ifdef ARRAY_END
#undef ARRAY_END
#endif
#include "ec_commands.h"
#include "host_command.h"
#include "mkbp_event.h"

#include <inttypes.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

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

// Inbound message reassembly state (Host -> Target)
struct ChreTunnelRxState {
	uint8_t *buffer = nullptr;
	uint32_t total_size = 0;
	uint32_t received_size = 0;
	uint16_t next_fragment_id = 0;
	bool in_progress = false;
};

// Outbound message queue element (Target -> Host)
struct ChreTunnelTxMessage {
	uint8_t *data = nullptr;
	size_t size = 0;
	size_t offset = 0;
	uint16_t fragment_id = 0;
};

constexpr size_t kMaxTxQueueSize = 8;

static struct ChreTunnelRxState s_rx_state;
static K_MUTEX_DEFINE(s_rx_mutex);

static ChreTunnelTxMessage s_tx_queue[kMaxTxQueueSize];
static size_t s_tx_head = 0;
static size_t s_tx_tail = 0;
static size_t s_tx_count = 0;
static K_MUTEX_DEFINE(s_tx_mutex);

static flatbuffers::Offset<flatbuffers::Vector<int8_t> >
add_string_as_byte_vector(chre::ChreFlatBufferBuilder &builder, const char *str)
{
	return builder.CreateVector(reinterpret_cast<const int8_t *>(str),
				    strlen(str) + 1);
}

} // namespace

namespace chre
{

// Function called to enqueue outbound message to host
bool enqueueMessageToHost(const uint8_t *data, size_t size)
{
	if (data == nullptr || size == 0) {
		return false;
	}

	{
		KMutexLock lock(&s_tx_mutex);
		if (s_tx_count >= kMaxTxQueueSize) {
			LOG_ERR("CHRE TX queue full (%zu items)", s_tx_count);
			return false;
		}

		uint8_t *buf = static_cast<uint8_t *>(chre::memoryAlloc(size));
		if (buf == nullptr) {
			LOG_ERR("Failed to allocate %zu bytes for CHRE TX message",
				size);
			return false;
		}
		memcpy(buf, data, size);

		s_tx_queue[s_tx_tail].data = buf;
		s_tx_queue[s_tx_tail].size = size;
		s_tx_queue[s_tx_tail].offset = 0;
		s_tx_queue[s_tx_tail].fragment_id = 0;
		s_tx_tail = (s_tx_tail + 1) % kMaxTxQueueSize;
		s_tx_count++;
	}

	// Signal the host via MKBP interrupt
	mkbp_send_event(EC_MKBP_EVENT_CHRE);
	return true;
}

void sendDebugDumpResultToHost(uint16_t /*hostClientId*/,
			       const char * /*debugStr*/,
			       size_t /*debugStrSize*/, bool /*complete*/,
			       uint32_t /*dataCount*/)
{
}

void HostLink::flushMessagesSentByNanoapp(uint64_t /*appId*/)
{
}

bool HostLink::sendMessage(const MessageToHost *message)
{
	if (message == nullptr) {
		return false;
	}

	LOG_INF("Sending message from nanoapp 0x%016llx to host endpoint 0x%x "
		"(size %zu)",
		static_cast<unsigned long long>(message->appId),
		message->toHostData.hostEndpoint, message->message.size());

	ChreFlatBufferBuilder builder(128 + message->message.size());
	auto msg_data = builder.CreateVector(message->message.data(),
					     message->message.size());
	auto nanoapp_msg = fbs::CreateNanoappMessage(
		builder, message->appId, message->toHostData.messageType,
		message->toHostData.hostEndpoint, msg_data,
		message->toHostData.messagePermissions,
		message->toHostData.appPermissions,
		message->toHostData.wokeHost, message->isReliable,
		message->messageSequenceNumber);
	fbs::HostAddress host_addr(kHostClientIdUnspecified);
	auto container = fbs::CreateMessageContainer(
		builder, fbs::ChreMessage::NanoappMessage, nanoapp_msg.Union(),
		&host_addr);
	builder.Finish(container);

	bool success = enqueueMessageToHost(builder.GetBufferPointer(),
					    builder.GetSize());
	if (message->isReliable) {
		EventLoopManagerSingleton::get()
			->getHostCommsManager()
			.completeTransaction(message->messageSequenceNumber,
					     success ? CHRE_ERROR_NONE :
						       CHRE_ERROR);
	} else {
		EventLoopManagerSingleton::get()
			->getHostCommsManager()
			.onMessageToHostComplete(message);
	}
	return success;
}

} // namespace chre

namespace
{

chre::NanoappLoadManager s_load_manager;

void send_fragment_response(uint16_t host_client_id, uint32_t transaction_id,
			    uint32_t fragment_id, bool success)
{
	chre::ChreFlatBufferBuilder builder(64);
	auto resp = chre::fbs::CreateLoadNanoappResponse(
		builder, transaction_id, success, fragment_id);
	chre::fbs::HostAddress host_addr(host_client_id);
	auto container = chre::fbs::CreateMessageContainer(
		builder, chre::fbs::ChreMessage::LoadNanoappResponse,
		resp.Union(), &host_addr);
	builder.Finish(container);
	chre::enqueueMessageToHost(builder.GetBufferPointer(),
				   builder.GetSize());
}

struct LoadNanoappCallbackData {
	uint64_t app_id;
	uint32_t transaction_id;
	uint16_t host_client_id;
	uint32_t fragment_id;
	chre::UniquePtr<chre::Nanoapp> nanoapp;
	bool send_fragment_response;
};

void start_nanoapp_callback(uint16_t /*type*/, void *data, void * /*extraData*/)
{
	auto *cb_data = static_cast<LoadNanoappCallbackData *>(data);
	auto *event_loop =
		&chre::EventLoopManagerSingleton::get()->getEventLoop();

	auto *mutex = chre::getMultiThreadingApiMutex();
	mutex->unlock();
	bool success = event_loop->startNanoapp(std::move(cb_data->nanoapp));
	mutex->lock();

	if (cb_data->send_fragment_response) {
		send_fragment_response(cb_data->host_client_id,
				       cb_data->transaction_id,
				       cb_data->fragment_id, success);
	}
	chre::memoryFree(cb_data);
}

void finish_loading_nanoapp_callback(uint16_t type, void *data,
				     void *extra_data)
{
	auto *cb_data = static_cast<LoadNanoappCallbackData *>(data);
	if (!cb_data->nanoapp->isLoaded() || !cb_data->nanoapp->isOpen()) {
		LOG_ERR("Nanoapp is not loaded or open");
		if (cb_data->send_fragment_response) {
			send_fragment_response(cb_data->host_client_id,
					       cb_data->transaction_id,
					       cb_data->fragment_id, false);
		}
		chre::memoryFree(cb_data);
	} else {
		start_nanoapp_callback(type, data, extra_data);
	}
}

void handle_load_nanoapp_request(uint16_t host_client_id,
				 const chre::fbs::LoadNanoappRequest *req)
{
	if (req == nullptr || req->app_binary() == nullptr) {
		LOG_ERR("Invalid LoadNanoappRequest");
		send_fragment_response(host_client_id,
				       req ? req->transaction_id() : 0,
				       req ? req->fragment_id() : 0, false);
		return;
	}

	const auto *app_binary = req->app_binary();
	uint32_t fragment_id = req->fragment_id();
	uint32_t transaction_id = req->transaction_id();
	uint64_t app_id = req->app_id();
	size_t buffer_len = app_binary->size();
	const void *buffer = app_binary->data();
	bool success = true;

	if (fragment_id == 0 || fragment_id == 1) {
		size_t total_app_binary_len =
			(fragment_id == 0) ? buffer_len : req->total_app_size();
		LOG_INF("Load nanoapp request for app ID 0x%016llx ver 0x%x"
			" flags 0x%x target API 0x%x size %zu (txnId %u client %u)",
			static_cast<unsigned long long>(app_id),
			req->app_version(), req->app_flags(),
			req->target_api_version(), total_app_binary_len,
			transaction_id, host_client_id);

		if (s_load_manager.hasPendingLoadTransaction()) {
			auto info = s_load_manager.getTransactionInfo();
			LOG_WRN("Pending load transaction exists "
				"(clientId=%u, txnId=%u, nextFragmentId=%u). "
				"Overriding it",
				info.hostClientId, info.transactionId,
				info.nextFragmentId);
			send_fragment_response(info.hostClientId,
					       info.transactionId,
					       info.nextFragmentId, false);
			s_load_manager.markFailure();
		}

		success = s_load_manager.prepareForLoad(
			host_client_id, transaction_id, app_id,
			req->app_version(), req->app_flags(),
			total_app_binary_len, req->target_api_version());
	}

	if (success) {
		success = s_load_manager.copyNanoappFragment(
			host_client_id, transaction_id,
			(fragment_id == 0) ? 1 : fragment_id, buffer,
			buffer_len);
	} else {
		LOG_ERR("Failed to prepare for load");
	}

	if (s_load_manager.isLoadComplete()) {
		LOG_INF("Nanoapp load complete, opening and starting...");
		auto *cb_data = chre::memoryAlloc<LoadNanoappCallbackData>();
		if (cb_data == nullptr) {
			LOG_ERR("OOM for LoadNanoappCallbackData");
			send_fragment_response(host_client_id, transaction_id,
					       fragment_id, false);
			return;
		}
		cb_data->transaction_id = transaction_id;
		cb_data->host_client_id = host_client_id;
		cb_data->app_id = app_id;
		cb_data->fragment_id = fragment_id;
		cb_data->nanoapp = s_load_manager.releaseNanoapp();
		cb_data->send_fragment_response = !req->respond_before_start();

		bool open_success = cb_data->nanoapp->openNanoapp();
		if (!open_success) {
			LOG_ERR("Failed to open nanoapp");
			send_fragment_response(host_client_id, transaction_id,
					       fragment_id, false);
			chre::memoryFree(cb_data);
		} else {
			chre::EventLoopManagerSingleton::get()->deferCallback(
				chre::SystemCallbackType::FinishLoadingNanoapp,
				cb_data, finish_loading_nanoapp_callback);
			if (req->respond_before_start()) {
				send_fragment_response(host_client_id,
						       transaction_id,
						       fragment_id, true);
			}
		}
	} else {
		send_fragment_response(host_client_id, transaction_id,
				       fragment_id, success);
	}
}

void handle_unload_nanoapp_request(uint16_t host_client_id,
				   const chre::fbs::UnloadNanoappRequest *req)
{
	if (req == nullptr) {
		return;
	}
	struct UnloadNanoappCallbackData {
		uint16_t host_client_id;
		uint32_t transaction_id;
		uint64_t app_id;
		bool allow_system;
	};
	auto *cb_data = chre::memoryAlloc<UnloadNanoappCallbackData>();
	if (cb_data == nullptr) {
		LOG_ERR("OOM for unload nanoapp callback");
		return;
	}
	cb_data->host_client_id = host_client_id;
	cb_data->transaction_id = req->transaction_id();
	cb_data->app_id = req->app_id();
	cb_data->allow_system = req->allow_system_nanoapp_unload();

	auto cb = [](uint16_t /*type*/, void *data, void * /*extraData*/) {
		auto *d = static_cast<UnloadNanoappCallbackData *>(data);
		bool success = false;
		uint16_t instance_id;
		auto &el =
			chre::EventLoopManagerSingleton::get()->getEventLoop();
		if (!el.findNanoappInstanceIdByAppId(d->app_id, &instance_id)) {
			LOG_ERR("Could not find app ID 0x%016llx to unload",
				static_cast<unsigned long long>(d->app_id));
		} else {
			success =
				el.unloadNanoapp(instance_id, d->allow_system);
		}
		chre::ChreFlatBufferBuilder builder(64);
		auto resp = chre::fbs::CreateUnloadNanoappResponse(
			builder, d->transaction_id, success);
		chre::fbs::HostAddress host_addr(d->host_client_id);
		auto container = chre::fbs::CreateMessageContainer(
			builder, chre::fbs::ChreMessage::UnloadNanoappResponse,
			resp.Union(), &host_addr);
		builder.Finish(container);
		chre::enqueueMessageToHost(builder.GetBufferPointer(),
					   builder.GetSize());
		chre::memoryFree(d);
	};
	chre::EventLoopManagerSingleton::get()->deferCallback(
		chre::SystemCallbackType::HandleUnloadNanoapp, cb_data, cb);
}

void handle_hub_info_request(uint16_t host_client_id)
{
	LOG_INF("Hub info request from client 0x%x", host_client_id);
	chre::ChreFlatBufferBuilder builder(256);

	auto name =
		add_string_as_byte_vector(builder, "Google Context Hub (ISH)");
	auto vendor = add_string_as_byte_vector(builder, "Google");
	auto toolchain = add_string_as_byte_vector(builder, "Zephyr");

	chre::fbs::HubInfoResponseBuilder resp_builder(builder);
	resp_builder.add_name(name);
	resp_builder.add_vendor(vendor);
	resp_builder.add_toolchain(toolchain);
	resp_builder.add_platform_version(1);
	resp_builder.add_toolchain_version(1);
	resp_builder.add_max_msg_len(chreGetMessageToHostMaxSize());
	resp_builder.add_platform_id(chreGetPlatformId());
	resp_builder.add_chre_platform_version(chreGetVersion());
	resp_builder.add_supports_reliable_messages(false);
	auto resp = resp_builder.Finish();

	chre::fbs::HostAddress host_addr(host_client_id);
	auto container = chre::fbs::CreateMessageContainer(
		builder, chre::fbs::ChreMessage::HubInfoResponse, resp.Union(),
		&host_addr);
	builder.Finish(container);

	chre::enqueueMessageToHost(builder.GetBufferPointer(),
				   builder.GetSize());
}

void handle_nanoapp_list_request(uint16_t host_client_id)
{
	LOG_INF("Nanoapp list request from client 0x%x", host_client_id);
	chre::ChreFlatBufferBuilder builder(256);
	chre::DynamicVector<flatbuffers::Offset<chre::fbs::NanoappListEntry> >
		entries;

	struct CbData {
		chre::ChreFlatBufferBuilder *builder;
		chre::DynamicVector<
			flatbuffers::Offset<chre::fbs::NanoappListEntry> >
			*entries;
	} cb_data = { &builder, &entries };

	auto callback = [](const chre::Nanoapp *nanoapp, void *data) {
		auto *cb = static_cast<CbData *>(data);
		auto entry = chre::fbs::CreateNanoappListEntry(
			*cb->builder, nanoapp->getAppId(),
			nanoapp->getAppVersion(), /*enabled=*/true,
			nanoapp->isSystemNanoapp(),
			nanoapp->getAppPermissions());
		cb->entries->push_back(entry);
	};

	chre::EventLoopManagerSingleton::get()->getEventLoop().forEachNanoapp(
		callback, &cb_data);

	auto vector_offset = builder.CreateVector<
		flatbuffers::Offset<chre::fbs::NanoappListEntry> >(entries);
	auto resp =
		chre::fbs::CreateNanoappListResponse(builder, vector_offset);

	chre::fbs::HostAddress host_addr(host_client_id);
	auto container = chre::fbs::CreateMessageContainer(
		builder, chre::fbs::ChreMessage::NanoappListResponse,
		resp.Union(), &host_addr);
	builder.Finish(container);

	chre::enqueueMessageToHost(builder.GetBufferPointer(),
				   builder.GetSize());
}

void handle_debug_dump_request(uint16_t host_client_id)
{
	LOG_INF("Debug dump request from client 0x%x", host_client_id);
	chre::ChreFlatBufferBuilder builder(64);
	auto resp = chre::fbs::CreateDebugDumpResponse(builder, true, 0);

	chre::fbs::HostAddress host_addr(host_client_id);
	auto container = chre::fbs::CreateMessageContainer(
		builder, chre::fbs::ChreMessage::DebugDumpResponse,
		resp.Union(), &host_addr);
	builder.Finish(container);

	chre::enqueueMessageToHost(builder.GetBufferPointer(),
				   builder.GetSize());
}

bool decode_message_from_host(const void *message, size_t message_len)
{
	if (message == nullptr || message_len == 0) {
		LOG_ERR("Null or zero message from host");
		return false;
	}

	flatbuffers::Verifier verifier(static_cast<const uint8_t *>(message),
				       message_len);
	if (!chre::fbs::VerifyMessageContainerBuffer(verifier)) {
		LOG_ERR("Dropping invalid/corrupted message from host (%zu bytes)",
			message_len);
		return false;
	}

	const auto *container = chre::fbs::GetMessageContainer(message);
	if (container == nullptr) {
		LOG_ERR("Null message container");
		return false;
	}

	uint16_t host_client_id = container->host_addr() ?
					  container->host_addr()->client_id() :
					  0;

	switch (container->message_type()) {
	case chre::fbs::ChreMessage::HubInfoRequest:
		handle_hub_info_request(host_client_id);
		break;

	case chre::fbs::ChreMessage::NanoappListRequest:
		handle_nanoapp_list_request(host_client_id);
		break;

	case chre::fbs::ChreMessage::TimeSyncMessage: {
		const auto *req =
			static_cast<const chre::fbs::TimeSyncMessage *>(
				container->message());
		if (req != nullptr) {
			LOG_INF("Time sync offset from host: %" PRId64 " ns",
				req->offset());
			chre::SystemTime::setEstimatedHostTimeOffset(
				req->offset());
		}
		break;
	}

	case chre::fbs::ChreMessage::NanoappMessage: {
		const auto *msg =
			static_cast<const chre::fbs::NanoappMessage *>(
				container->message());
		if (msg != nullptr && msg->message() != nullptr) {
			LOG_INF("Nanoapp message: app 0x%" PRIx64
				", ep 0x%x, len %u",
				msg->app_id(), msg->host_endpoint(),
				msg->message()->size());
			chre::EventLoopManagerSingleton::get()
				->getHostCommsManager()
				.sendMessageToNanoappFromHost(
					msg->app_id(), msg->message_type(),
					msg->host_endpoint(),
					msg->message()->data(),
					msg->message()->size(),
					msg->is_reliable(),
					msg->message_sequence_number());
		}
		break;
	}

	case chre::fbs::ChreMessage::DebugDumpRequest:
		handle_debug_dump_request(host_client_id);
		break;

	case chre::fbs::ChreMessage::LoadNanoappRequest: {
		const auto *req =
			static_cast<const chre::fbs::LoadNanoappRequest *>(
				container->message());
		handle_load_nanoapp_request(host_client_id, req);
		break;
	}

	case chre::fbs::ChreMessage::UnloadNanoappRequest: {
		const auto *req =
			static_cast<const chre::fbs::UnloadNanoappRequest *>(
				container->message());
		handle_unload_nanoapp_request(host_client_id, req);
		break;
	}

	default:
		LOG_WRN("Unhandled CHRE message type %d from host",
			static_cast<int>(container->message_type()));
		break;
	}

	return true;
}

} // namespace

static enum ec_host_cmd_status
hc_chre_host_to_target(struct ec_host_cmd_handler_args *args)
{
	KMutexLock lock(&s_rx_mutex);

	const auto *params =
		static_cast<const struct ec_params_chre_tunnel_msg *>(
			args->input_buf);
	auto *resp = static_cast<struct ec_response_chre_tunnel_msg *>(
		args->output_buf);

	if (args->input_buf_size < sizeof(*params) ||
	    args->input_buf_size < sizeof(*params) + params->fragment_size ||
	    args->output_buf_max < sizeof(*resp)) {
		LOG_ERR("Invalid host-to-target params size %u",
			args->input_buf_size);
		return EC_HOST_CMD_INVALID_PARAM;
	}

	// First fragment of a new message
	if (params->offset == 0) {
		if (s_rx_state.buffer != nullptr) {
			chre::memoryFree(s_rx_state.buffer);
			s_rx_state.buffer = nullptr;
		}

		if (params->total_size == 0 ||
		    params->total_size > CONFIG_CHRE_DYNAMIC_MEMORY_SIZE) {
			LOG_ERR("Invalid total size %u (max %u)",
				params->total_size,
				CONFIG_CHRE_DYNAMIC_MEMORY_SIZE);
			resp->status = 1;
			args->output_buf_size = sizeof(*resp);
			return EC_HOST_CMD_INVALID_PARAM;
		}

		s_rx_state.buffer = static_cast<uint8_t *>(
			chre::memoryAlloc(params->total_size));
		if (s_rx_state.buffer == nullptr) {
			LOG_ERR("Failed to allocate %u bytes for CHRE message",
				params->total_size);
			resp->status = 1;
			args->output_buf_size = sizeof(*resp);
			return EC_HOST_CMD_ERROR;
		}
		s_rx_state.total_size = params->total_size;
		s_rx_state.received_size = 0;
		s_rx_state.next_fragment_id = 0;
		s_rx_state.in_progress = true;
	}

	if (!s_rx_state.in_progress || s_rx_state.buffer == nullptr) {
		LOG_ERR("No tunnel rx in progress");
		return EC_HOST_CMD_INVALID_PARAM;
	}

	if (params->offset != s_rx_state.received_size ||
	    params->fragment_id != s_rx_state.next_fragment_id ||
	    s_rx_state.received_size + params->fragment_size >
		    s_rx_state.total_size) {
		LOG_ERR("Fragment mismatch: expected offset %u (got %u), "
			"expected frag %u (got %u), total %u",
			s_rx_state.received_size, params->offset,
			s_rx_state.next_fragment_id, params->fragment_id,
			s_rx_state.total_size);
		chre::memoryFree(s_rx_state.buffer);
		s_rx_state.buffer = nullptr;
		s_rx_state.in_progress = false;
		return EC_HOST_CMD_INVALID_PARAM;
	}

	if (params->fragment_size > 0) {
		memcpy(s_rx_state.buffer + s_rx_state.received_size,
		       params->data, params->fragment_size);
		s_rx_state.received_size += params->fragment_size;
		s_rx_state.next_fragment_id++;
	}

	// Complete message assembled
	if (s_rx_state.received_size == s_rx_state.total_size) {
		LOG_INF("Received complete CHRE message of %u bytes from host",
			s_rx_state.total_size);

		decode_message_from_host(s_rx_state.buffer,
					 s_rx_state.total_size);

		chre::memoryFree(s_rx_state.buffer);
		s_rx_state.buffer = nullptr;
		s_rx_state.in_progress = false;
	}

	resp->status = 0;
	args->output_buf_size = sizeof(*resp);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_CHRE_HOST_TO_TARGET, hc_chre_host_to_target,
		    EC_VER_MASK(0), struct ec_params_chre_tunnel_msg,
		    struct ec_response_chre_tunnel_msg);

static enum ec_host_cmd_status
hc_chre_target_to_host(struct ec_host_cmd_handler_args *args)
{
	KMutexLock lock(&s_tx_mutex);

	if (args->input_buf_size < sizeof(struct ec_params_chre_target_to_host) ||
	    args->output_buf_max < sizeof(struct ec_response_chre_target_to_host)) {
		LOG_ERR("Invalid target-to-host params or response size (in %u, out max %u)",
			args->input_buf_size, args->output_buf_max);
		return EC_HOST_CMD_INVALID_PARAM;
	}

	const auto *params =
		static_cast<const struct ec_params_chre_target_to_host *>(
			args->input_buf);
	auto *resp = static_cast<struct ec_response_chre_target_to_host *>(
		args->output_buf);

	if (s_tx_count == 0) {
		resp->total_size = 0;
		resp->offset = 0;
		resp->fragment_id = 0;
		resp->fragment_size = 0;
		args->output_buf_size = sizeof(*resp);
		return EC_HOST_CMD_SUCCESS;
	}

	auto &msg = s_tx_queue[s_tx_head];
	size_t max_payload = args->output_buf_max - sizeof(*resp);
	if (params->max_size > 0 && params->max_size < max_payload) {
		max_payload = params->max_size;
	}

	uint32_t remaining = msg.size - msg.offset;
	uint16_t chunk_size = static_cast<uint16_t>(
		MIN(remaining, static_cast<uint32_t>(max_payload)));

	resp->total_size = msg.size;
	resp->offset = msg.offset;
	resp->fragment_id = msg.fragment_id++;
	resp->fragment_size = chunk_size;
	if (chunk_size > 0) {
		memcpy(resp->data, msg.data + msg.offset, chunk_size);
		msg.offset += chunk_size;
	}

	// If whole message transmitted, pop from queue
	if (msg.offset >= msg.size) {
		chre::memoryFree(msg.data);
		msg.data = nullptr;
		s_tx_head = (s_tx_head + 1) % kMaxTxQueueSize;
		s_tx_count--;

		// If more messages remain in queue, signal host to keep reading
		if (s_tx_count > 0) {
			mkbp_send_event(EC_MKBP_EVENT_CHRE);
		}
	}

	args->output_buf_size = sizeof(*resp) + chunk_size;
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_CHRE_TARGET_TO_HOST, hc_chre_target_to_host,
		    EC_VER_MASK(0), struct ec_params_chre_target_to_host,
		    struct ec_response_chre_target_to_host);

static int chre_get_next_event(uint8_t *data)
{
	ARG_UNUSED(data);
	return 0;
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_CHRE, chre_get_next_event);
