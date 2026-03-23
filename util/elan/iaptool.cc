// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iaptool.h"

#include <stdio.h>
#include <stdlib.h>

#include <cstring>
#include <unistd.h>

#define CMDLINE_FILE_PATH "-F"
#define CMDLINE_VID "-V"
#define CMDLINE_PID "-P"

#define STR_LENGTH 255

typedef struct _CMDLINE {
	uint16_t vid;
	uint16_t pid;
	std::string filePath;
} CMDLINE;

template <typename... Args>
void logInfo(std::format_string<Args...> fmt, Args &&...args)
{
	std::cout << std::format(fmt, std::forward<Args>(args)...);
}

// declaratiopns
void setProgressBar(int progress, int totalSize);

// command line manager
class CommandLineManager {
    public:
	CommandLineManager(std::span<const char *> buff)
	{
		ParseCommandLine(buff);
	}

	const std::optional<CMDLINE> &getCommandLine() const
	{
		return m_cmdLine;
	}

    private:
	std::optional<CMDLINE> m_cmdLine;

	void ParseCommandLine(std::span<const char *> buff)
	{
		CMDLINE cmd;

		// Index 0 is application name
		for (size_t i = 1; i + 1 < buff.size(); ++i) {
			if (strcmp(buff[i], CMDLINE_FILE_PATH) == 0) {
				cmd.filePath = buff[i + 1];
				++i;
			} else if (strcmp(buff[i], CMDLINE_VID) == 0) {
				cmd.vid = strtol(buff[i + 1], nullptr, 16);
				++i;
			} else if (strcmp(buff[i], CMDLINE_PID) == 0) {
				cmd.pid = strtol(buff[i + 1], nullptr, 16);
				++i;
			}
		}
		m_cmdLine = cmd;
	}
};

int main(int argc, char *argv[])
{
	ElanApp::iapcontrol iapCtrl;
	size_t binSize = 0;

	logInfo("[IAP] main - start\n");

	std::span<const char *> args(const_cast<const char **>(argv),
				     static_cast<size_t>(argc));

	CommandLineManager commandLineMgr(args);

	auto cmdLine = commandLineMgr.getCommandLine();
	if (cmdLine == std::nullopt) {
		logInfo("[IAP] main - parse command line fail\n");
		return -1;
	}

	logInfo("[IAP] Parase Command Line - File Path: {}\n",
		cmdLine->filePath);

	// Get File Size and Content
	binSize = getBinarySize(cmdLine->filePath);
	if (binSize == 0) {
		logInfo("[IAP] main - file size is zero\n");
		return -1;
	}

	logInfo("[IAP] main - binary file size {} bytes\n", binSize);

	auto binary = getBinary(cmdLine->filePath);
	if (binary == std::nullopt) {
		logInfo("[IAP] main - get binary content fail\n");
		return -1;
	}

	// Write Rom
	iapCtrl.setProgressCallback(setProgressBar);

	if (iapCtrl.runIapProcess(std::span<uint8_t>(*binary))) {
		logInfo("[IAP] main - iap write rom fail\n");
		return -1;
	}

	logInfo("[IAP] main - exit\n");

	return 0;
}

// implementations
void setProgressBar(int progress, int totalSize)
{
	int percent = 0;

	percent = (100 * (progress)) / totalSize;

	logInfo("\r[{}{}]", std::string(percent / 5, '#').c_str(),
		std::string(100 / 5 - percent / 5, ' ').c_str());
	logInfo(" {} % [Data {} of {}]", percent, progress, totalSize);
	fflush(stdout);

	if (percent >= 100)
		logInfo("\n");
}
