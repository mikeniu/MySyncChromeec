/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "pw_host_command/service.h"
#include "pw_rpc/nanopb/test_method_context.h"

#include <gtest/gtest.h>

namespace
{

using ::chromiumos::ec::HostCommandService;

TEST(HostCommandServiceTest, ExecuteInvalidCommandReturnsError)
{
	PW_NANOPB_TEST_METHOD_CONTEXT(HostCommandService, ExecuteCommand)
	context;

	chromiumos_ec_HostCommandRequest request = {};
	request.command = 0xffff; // Invalid command ID
	request.version = 0;
	request.request_payload.size = 0;

	pw::Status status = context.call(request);
	EXPECT_TRUE(status.ok());
	EXPECT_NE(context.response().ec_status, EC_RES_SUCCESS);
}

TEST(HostCommandServiceTest, ExecuteHelloCommandSuccess)
{
	PW_NANOPB_TEST_METHOD_CONTEXT(HostCommandService, ExecuteCommand)
	context;

	struct ec_params_hello params = {
		.in_data = 0xaabbccdd,
	};

	chromiumos_ec_HostCommandRequest request = {};
	request.command = EC_CMD_HELLO;
	request.version = 0;
	request.request_payload.size = sizeof(params);
	memcpy(request.request_payload.bytes, &params, sizeof(params));

	pw::Status status = context.call(request);
	EXPECT_TRUE(status.ok());
	EXPECT_EQ(context.response().ec_status, EC_RES_SUCCESS);
	EXPECT_EQ(context.response().response_payload.size,
		  sizeof(struct ec_response_hello));

	struct ec_response_hello response;
	memcpy(&response, context.response().response_payload.bytes,
	       sizeof(response));
	EXPECT_EQ(response.out_data, 0xaabbccdd + 0x01020304);
}

} // namespace
