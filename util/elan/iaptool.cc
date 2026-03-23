// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iaptool.h"
#include "utility.h"

#include <charconv>
#include <format>
#include <string_view>
#include <unistd.h>

struct CmdLine {
	uint16_t vid;
	uint16_t pid;
	std::string file_Path;
};

void SetProgressBar(size_t progress, size_t total_size);

class CommandLineManager {
    public:
	CommandLineManager(int argc, char *argv[])
	{
		ParseCommandLine(argc, argv);
	}

	const std::optional<CmdLine> &GetCommandLine() const
	{
		return cmd_line_;
	}

    private:
	std::optional<CmdLine> cmd_line_;

	void ParseCommandLine(int argc, char *argv[])
	{
		CmdLine cmd{ elan::kBootVid, elan::kBootPid, "" };
		int opt;

		while ((opt = getopt(argc, argv, "F:V:P:")) != -1) {
			switch (opt) {
			case 'F':
				cmd.file_Path = optarg;
				break;
			case 'V':
				if (auto val = ParseHex(optarg)) {
					cmd.vid = *val;
				} else {
					LogInfo("[IAP] {} - invalid vid format: {}\n",
						__func__, optarg);
					return;
				}
				break;
			case 'P':
				if (auto val = ParseHex(optarg)) {
					cmd.pid = *val;
				} else {
					LogInfo("[IAP] {} - invalid pid format: {}\n",
						__func__, optarg);
					return;
				}
				break;
			default: /* '?' */
				std::cerr << "Usage: " << argv[0]
					  << " -F <filepath>\n";
				return;
			}
		}

		// Ensure required fields were actually provided
		if (cmd.file_Path.empty()) {
			LogInfo("[IAP] {} - missing required arguments.\n",
				__func__);
			LogInfo("[IAP] {} - usage: {} -F <filepath>\n",
				__func__, argv[0]);
			return;
		}

		cmd_line_ = cmd;
	}

	std::optional<uint16_t> ParseHex(std::string_view strv)
	{
		if (strv.empty()) {
			return std::nullopt;
		}

		uint16_t val;
		auto [ptr, ec] = std::from_chars(
			strv.data(), strv.data() + strv.size(), val, 16);

		if (ec == std::errc() && ptr == strv.data() + strv.size()) {
			return val;
		}

		return std::nullopt;
	}
};

int main(int argc, char *argv[])
{
	elan::IapControl iap_ctrl;

	LogInfo("[IAP] main - start\n");

	CommandLineManager command_line_mgr(argc, argv);

	auto cmd_line = command_line_mgr.GetCommandLine();
	if (cmd_line == std::nullopt) {
		LogInfo("[IAP] main - parse command line fail\n");
		return -1;
	}

	if (iap_ctrl.Initialize(cmd_line->vid, cmd_line->pid) != 0) {
		LogInfo("[IAP] main - device initialization failed\n");
		return -1;
	}

	LogInfo("[IAP] Parse Command Line - File Path: {}\n",
		cmd_line->file_Path);

	// Get File Size and Content
	auto bin_size = GetBinarySize(cmd_line->file_Path);
	if (bin_size == std::nullopt) {
		LogInfo("[IAP] main - get file size fail\n");
		return -1;
	}

	LogInfo("[IAP] main - binary file size {} bytes\n",
		bin_size.value_or(0));

	auto binary = GetBinary(cmd_line->file_Path);
	if (binary == std::nullopt) {
		LogInfo("[IAP] main - get binary content fail\n");
		return -1;
	}

	// Write Rom
	iap_ctrl.SetProgressCallback(SetProgressBar);

	if (iap_ctrl.RunIapProcess(*binary)) {
		LogInfo("[IAP] main - iap write rom fail\n");
		return -1;
	}

	LogInfo("[IAP] main - exit\n");

	return 0;
}

void SetProgressBar(size_t progress, size_t total_size)
{
	int percent = 0;

	if (total_size == 0)
		percent = 0;
	else
		percent = (100 * (progress)) / total_size;

	LogInfo("\r[{}{}]", std::string(percent / 5, '#'),
		std::string(100 / 5 - percent / 5, ' '));
	LogInfo(" {} % [Data {} of {}]", percent, progress, total_size);
	std::fflush(stdout);

	if (percent >= 100)
		LogInfo("\n");
}
