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
#include <string>
#include <vector>

class EcCommand {
    public:
	EcCommand(uint16_t cmd_id, uint8_t cmd_ver,
		  std::span<const uint8_t> payload = std::span<const uint8_t>())
		: cmd_id_(cmd_id)
		, cmd_ver_(cmd_ver)
		, payload_(payload.begin(), payload.end())
	{
		PackCommand();
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

	void PackCommand()
	{
		EcHostCommandRequest hdr = {
			.command_id = cmd_id_,
			.command_version = cmd_ver_,
			.data_length = static_cast<uint16_t>(payload_.size())
		};
		cmd_bytes_.resize(sizeof(hdr) + payload_.size());
		std::memcpy(cmd_bytes_.data(), &hdr, sizeof(hdr));

		if (!payload_.empty()) {
			std::memcpy(cmd_bytes_.data() + sizeof(hdr),
				    payload_.data(), payload_.size());
		}
		UpdateChecksum();
	}

	void UpdateChecksum()
	{
		uint32_t sum = 0;
		uint8_t *buf = cmd_bytes_.data();
		buf[1] = 0;
		for (size_t i = 0; i < cmd_bytes_.size(); i++)
			sum += buf[i];
		buf[1] = (256 - (sum % 256)) % 256;
	}
};

EcComm::EcComm(UsbComm &usb)
	: usb_(usb)
{
}

std::expected<std::vector<uint8_t>, bool>
EcComm::SendCommand(uint16_t cmd_id, uint8_t cmd_ver,
		    std::span<const uint8_t> payload)
{
	EcCommand cmd(cmd_id, cmd_ver, payload);
	const auto &tx_bytes = cmd.bytes();

	if (!usb_.Send(tx_bytes)) {
		return std::unexpected(false);
	}

	std::vector<uint8_t> resp_buf(512);
	int actual = usb_.Receive(resp_buf);
	if (actual < static_cast<int>(sizeof(EcHostCommandResponse))) {
		return std::unexpected(false);
	}
	resp_buf.resize(actual);

	EcHostCommandResponse *resp_hdr =
		reinterpret_cast<EcHostCommandResponse *>(resp_buf.data());
	if (resp_hdr->result != 0) {
		return std::unexpected(false);
	}

	std::vector<uint8_t> resp_out;
	if (resp_hdr->data_length > 0) {
		size_t header_len = sizeof(EcHostCommandResponse);
		if (header_len + resp_hdr->data_length <= resp_buf.size()) {
			resp_out.assign(resp_buf.begin() + header_len,
					resp_buf.begin() + header_len +
						resp_hdr->data_length);
		}
	}

	return resp_out;
}
