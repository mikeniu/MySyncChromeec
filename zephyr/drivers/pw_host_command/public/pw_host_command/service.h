/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "host_command.h"
#include "pw_host_command/proto/host_command.rpc.pb.h"
#include "pw_log/log.h"
#include "pw_status/status.h"

#include <cstring>

namespace chromiumos::ec
{

/**
 * Pigweed RPC service providing ChromeOS EC Host Command execution.
 */
class HostCommandService final
	: public pw_rpc::nanopb::HostCommandService::Service<HostCommandService> {
    public:
	static constexpr size_t kMaxPayloadSize = sizeof(
		chromiumos_ec_HostCommandResponse{}.response_payload.bytes);

	constexpr HostCommandService() = default;

	pw::Status
	ExecuteCommand(const chromiumos_ec_HostCommandRequest &request,
		       chromiumos_ec_HostCommandResponse &response)
	{
		PW_LOG_INFO(
			"Received host command 0x%04x (v%u), %u bytes payload",
			request.command, request.version,
			request.request_payload.size);

		uint8_t response_buf[kMaxPayloadSize] = { 0 };

		struct host_cmd_handler_args args = {};
		args.send_response = nullptr;
		args.command = static_cast<uint16_t>(request.command);
		args.version = static_cast<uint8_t>(request.version);
		args.params = request.request_payload.bytes;
		args.params_size =
			static_cast<uint16_t>(request.request_payload.size);
		args.response = response_buf;
		args.response_max = sizeof(response_buf);
		args.response_size = 0;

		uint16_t result = host_command_process(&args);
		PW_LOG_DEBUG(
			"Host command 0x%04x completed: status=%u, resp_size=%u",
			request.command, result, args.response_size);
		response.ec_status = result;

		if (result == EC_RES_SUCCESS && args.response_size > 0) {
			size_t copy_size =
				(args.response_size <=
				 sizeof(response.response_payload.bytes)) ?
					args.response_size :
					sizeof(response.response_payload.bytes);
			memcpy(response.response_payload.bytes, response_buf,
			       copy_size);
			response.response_payload.size = copy_size;
		} else {
			response.response_payload.size = 0;
		}

		return pw::OkStatus();
	}
};

} // namespace chromiumos::ec
