// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iaptool.h"
#include "utility.h"

#define CMDLINE_FILE_PATH "-F"
#define CMDLINE_VID "-V"
#define CMDLINE_PID "-P"

#define STR_LENGTH 255

typedef struct _CMDLINE {
	uint16_t vid;
	uint16_t pid;
	std::string filePath;
} CMDLINE;

void setProgressBar(size_t progress, size_t totalSize);

class CommandLineManager {
    public:
	CommandLineManager(int argc, char *argv[])
	{
		parseCommandLine(argc, argv);
	}

	const std::optional<CMDLINE> &getCommandLine() const
	{
		return m_cmdLine;
	}

    private:
	std::optional<CMDLINE> m_cmdLine;

	void parseCommandLine(int argc, char *argv[])
	{
		CMDLINE cmd{ BOOT_VID, BOOT_PID, "" };
		int opt;

		while ((opt = getopt(argc, argv, "F:V:P:")) != -1) {
			switch (opt) {
			case 'F':
				cmd.filePath = optarg;
				break;
			case 'V':
				if (auto val = parseHex(optarg)) {
					cmd.vid = *val;
				} else {
					logInfo("[IAP] {} - invalid vid format: {}\n",
						__func__, optarg);
					return;
				}
				break;
			case 'P':
				if (auto val = parseHex(optarg)) {
					cmd.pid = *val;
				} else {
					logInfo("[IAP] {} - invalid pid format: {}\n",
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
		if (cmd.filePath.empty()) {
			logInfo("[IAP] {} - missing required arguments.\n",
				__func__);
			logInfo("[IAP] {} - usage: {} -F <filepath>\n",
				__func__, argv[0]);
			return;
		}

		m_cmdLine = cmd;
	}

	std::optional<uint16_t> parseHex(std::string_view strv)
	{
		if (strv.empty()) {
			return std::nullopt;
		}

		uint16_t val;
		std::ispanstream stream(strv);

		// Extract as hex, and ensure we consumed the entire view
		if (stream >> std::hex >> val && stream.eof()) {
			return val;
		}

		return std::nullopt;
	}
};

int main(int argc, char *argv[])
{
	ElanApp::IapControl iapCtrl;

	logInfo("[IAP] main - start\n");

	CommandLineManager commandLineMgr(argc, argv);

	auto cmdLine = commandLineMgr.getCommandLine();
	if (cmdLine == std::nullopt) {
		logInfo("[IAP] main - parse command line fail\n");
		return -1;
	}

	if (iapCtrl.initialize(cmdLine->vid, cmdLine->pid) != 0) {
		logInfo("[IAP] main - device initialization failed\n");
		return -1;
	}

	logInfo("[IAP] Parse Command Line - File Path: {}\n",
		cmdLine->filePath);

	// Get File Size and Content
	auto binSize = getBinarySize(cmdLine->filePath);
	if (binSize == std::nullopt) {
		logInfo("[IAP] main - get file size fail\n");
		return -1;
	}

	logInfo("[IAP] main - binary file size {} bytes\n",
		binSize.value_or(0));

	auto binary = getBinary(cmdLine->filePath);
	if (binary == std::nullopt) {
		logInfo("[IAP] main - get binary content fail\n");
		return -1;
	}

	// Write Rom
	iapCtrl.setProgressCallback(setProgressBar);

	if (iapCtrl.runIapProcess(*binary)) {
		logInfo("[IAP] main - iap write rom fail\n");
		return -1;
	}

	logInfo("[IAP] main - exit\n");

	return 0;
}

void setProgressBar(size_t progress, size_t totalSize)
{
	int percent = 0;

	if (totalSize == 0)
		percent = 0;
	else
		percent = (100 * (progress)) / totalSize;

	logInfo("\r[{}{}]", std::string(percent / 5, '#'),
		std::string(100 / 5 - percent / 5, ' '));
	logInfo(" {} % [Data {} of {}]", percent, progress, totalSize);
	std::cout << std::flush;

	if (percent >= 100)
		logInfo("\n");
}
