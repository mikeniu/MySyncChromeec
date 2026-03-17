/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ec_comm.h"
#include "ec_protocol_defs.h"
#include "usb_comm.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

class ECCommand {
    public:
	ECCommand(uint16_t cmd_id, uint8_t cmd_ver,
		  const std::vector<uint8_t> &payload = {})
		: cmd_id_(cmd_id)
		, cmd_ver_(cmd_ver)
		, payload_(payload)
	{
		pack_command();
	}

	const std::vector<uint8_t> &bytes() const
	{
		return cmd_bytes_;
	}

    private:
	uint16_t cmd_id_;
	uint8_t cmd_ver_;
	std::vector<uint8_t> payload_;
	std::vector<uint8_t> cmd_bytes_;

	void pack_command()
	{
		EcHostCommandRequest hdr;
		hdr.protocol_version = 3;
		hdr.checksum = 0;
		hdr.command_id = cmd_id_;
		hdr.command_version = cmd_ver_;
		hdr.reserved = 0;
		hdr.data_length = payload_.size();

		cmd_bytes_.resize(sizeof(hdr) + payload_.size());
		std::memcpy(cmd_bytes_.data(), &hdr, sizeof(hdr));

		if (!payload_.empty()) {
			std::memcpy(cmd_bytes_.data() + sizeof(hdr),
				    payload_.data(), payload_.size());
		}
		update_checksum();
	}

	void update_checksum()
	{
		uint32_t sum = 0;
		uint8_t *buf = cmd_bytes_.data();
		buf[1] = 0;
		for (size_t i = 0; i < cmd_bytes_.size(); i++)
			sum += buf[i];
		buf[1] = (256 - (sum % 256)) % 256;
	}
};

static std::string result_to_string(EcCommandResult result)
{
	switch (result) {
	case EcCommandResult::SUCCESS:
		return "SUCCESS (0)";
	case EcCommandResult::INVALID_COMMAND:
		return "INVALID_COMMAND (1)";
	case EcCommandResult::INVALID_HEADER:
		return "INVALID_HEADER (12)";
	default:
		return "UNKNOWN_ERROR (" +
		       std::to_string(static_cast<int>(result)) + ")";
	}
}

ECComm::ECComm(UsbComm &usb)
	: usb_(usb)
{
}

bool ECComm::send_command(uint16_t cmd_id, uint8_t cmd_ver,
			  const std::vector<uint8_t> &payload,
			  std::vector<uint8_t> &resp_out)
{
	resp_out.clear();
	ECCommand cmd(cmd_id, cmd_ver, payload);
	const auto &tx_bytes = cmd.bytes();

	if (!usb_.send(tx_bytes.data(), tx_bytes.size())) {
		return false;
	}

	std::vector<uint8_t> resp_buf(512);
	int actual = usb_.receive(resp_buf.data(), resp_buf.size());
	if (actual < static_cast<int>(sizeof(EcHostCommandResponse))) {
		return false;
	}
	resp_buf.resize(actual);

	EcHostCommandResponse *resp_hdr =
		reinterpret_cast<EcHostCommandResponse *>(resp_buf.data());
	if (resp_hdr->result != 0) {
		return false;
	}

	if (resp_hdr->data_length > 0) {
		size_t header_len = sizeof(EcHostCommandResponse);
		if (header_len + resp_hdr->data_length <= resp_buf.size()) {
			resp_out.assign(resp_buf.begin() + header_len,
					resp_buf.begin() + header_len +
						resp_hdr->data_length);
		}
	}

	return true;
}