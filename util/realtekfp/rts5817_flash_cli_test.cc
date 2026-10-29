/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_flash_cli.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

/* getopt needs a mutable argv terminated by nullptr, and its global state
 * reset before every run (parse_args() does the latter). */
template <typename... Args> bool RunParseArgs(Config &cfg, Args... args)
{
	std::array<char *, sizeof...(args) + 1> argv = {
		const_cast<char *>(args)..., nullptr
	};

	return parse_args(argv.size() - 1, argv.data(), cfg);
}

TEST(ParseArgsTest, ImageAndDeviceId)
{
	Config cfg;

	ASSERT_TRUE(RunParseArgs(cfg, "rts5817_flash", "-p", "1234:abcd", "-i",
				 "fw.bin"));
	EXPECT_EQ(cfg.dev_id, "1234:abcd");
	EXPECT_EQ(cfg.img, "fw.bin");
}

TEST(ParseArgsTest, LongOptions)
{
	Config cfg;

	ASSERT_TRUE(RunParseArgs(cfg, "rts5817_flash", "--pid", "0BDA:5817",
				 "--img", "fw.bin"));
	EXPECT_EQ(cfg.dev_id, "0BDA:5817");
	EXPECT_EQ(cfg.img, "fw.bin");
}

TEST(ParseArgsTest, DeviceIdEmptyWhenOmitted)
{
	Config cfg;

	ASSERT_TRUE(RunParseArgs(cfg, "rts5817_flash", "-i", "fw.bin"));
	EXPECT_TRUE(cfg.dev_id.empty());
	EXPECT_EQ(cfg.img, "fw.bin");
}

TEST(ParseArgsTest, ImageIsRequired)
{
	Config cfg;

	EXPECT_FALSE(RunParseArgs(cfg, "rts5817_flash"));
	EXPECT_FALSE(RunParseArgs(cfg, "rts5817_flash", "-p", "0BDA:5817"));
}

TEST(ParseArgsTest, HelpReturnsFalse)
{
	Config cfg;

	EXPECT_FALSE(RunParseArgs(cfg, "rts5817_flash", "-h"));
	EXPECT_FALSE(RunParseArgs(cfg, "rts5817_flash", "--help"));
}

TEST(ParseArgsTest, UnknownOptionReturnsFalse)
{
	Config cfg;

	EXPECT_FALSE(RunParseArgs(cfg, "rts5817_flash", "-x", "-i", "fw.bin"));
}

TEST(ParseArgsTest, OptionMissingValueReturnsFalse)
{
	Config cfg;

	EXPECT_FALSE(RunParseArgs(cfg, "rts5817_flash", "-i"));
}

TEST(ParseVidPidTest, ValidUpperAndLowerCase)
{
	uint16_t vid = 0;
	uint16_t pid = 0;

	ASSERT_TRUE(parse_vid_pid("0BDA:5817", vid, pid));
	EXPECT_EQ(vid, 0x0BDA);
	EXPECT_EQ(pid, 0x5817);

	ASSERT_TRUE(parse_vid_pid("abcd:ef01", vid, pid));
	EXPECT_EQ(vid, 0xABCD);
	EXPECT_EQ(pid, 0xEF01);
}

TEST(ParseVidPidTest, MalformedLeavesOutputsUntouched)
{
	uint16_t vid = 0x1111;
	uint16_t pid = 0x2222;

	EXPECT_FALSE(parse_vid_pid("", vid, pid));
	EXPECT_FALSE(parse_vid_pid("0BDA", vid, pid));
	EXPECT_FALSE(parse_vid_pid("0BDA-5817", vid, pid));
	EXPECT_FALSE(parse_vid_pid("zzzz:yyyy", vid, pid));
	EXPECT_FALSE(parse_vid_pid("0BDA:5817foo", vid, pid));
	EXPECT_FALSE(parse_vid_pid("10000:5817", vid, pid));
	EXPECT_FALSE(parse_vid_pid("0BDA:10000", vid, pid));
	EXPECT_EQ(vid, 0x1111);
	EXPECT_EQ(pid, 0x2222);
}

/* A valid 16-byte DFU suffix with the given bLength. */
std::vector<uint8_t> DfuSuffix(uint8_t length = 16)
{
	return { 0x00, 0x00, 0x17, 0x58, 0xDA, 0x0B, 0x00, 0x01,
		 'U',  'F',  'D',  length, 0x11, 0x22, 0x33, 0x44 };
}

std::vector<uint8_t> WithSuffix(std::vector<uint8_t> payload,
				const std::vector<uint8_t> &suffix)
{
	payload.insert(payload.end(), suffix.begin(), suffix.end());
	return payload;
}

TEST(StripDfuSuffixTest, RemovesValidSuffix)
{
	const std::vector<uint8_t> payload = { 1, 2, 3, 4, 5 };
	auto fw = WithSuffix(payload, DfuSuffix());

	EXPECT_EQ(strip_dfu_suffix(fw), 16u);
	EXPECT_EQ(fw, payload);
}

TEST(StripDfuSuffixTest, SuffixOnlyLeavesEmptyImage)
{
	auto fw = DfuSuffix();

	EXPECT_EQ(strip_dfu_suffix(fw), 16u);
	EXPECT_TRUE(fw.empty());
}

TEST(StripDfuSuffixTest, HonorsLongerBLength)
{
	const std::vector<uint8_t> payload = { 1, 2, 3, 4, 5 };
	const auto suffix = DfuSuffix(20);
	auto fw = WithSuffix(payload, std::vector<uint8_t>(4, 0xEE));

	fw.insert(fw.end(), suffix.begin(), suffix.end());
	EXPECT_EQ(strip_dfu_suffix(fw), 20u);
	EXPECT_EQ(fw, payload);
}

TEST(StripDfuSuffixTest, KeepsImageWithoutSignature)
{
	auto suffix = DfuSuffix();
	suffix[8] = 'X';
	const auto fw_orig = WithSuffix({ 1, 2, 3 }, suffix);
	auto fw = fw_orig;

	EXPECT_EQ(strip_dfu_suffix(fw), 0u);
	EXPECT_EQ(fw, fw_orig);
}

TEST(StripDfuSuffixTest, KeepsImageWithImplausibleLength)
{
	for (uint8_t len : { 0, 15, 255 }) {
		const auto fw_orig = WithSuffix({ 1, 2, 3 }, DfuSuffix(len));
		auto fw = fw_orig;

		EXPECT_EQ(strip_dfu_suffix(fw), 0u) << "bLength " << int{ len };
		EXPECT_EQ(fw, fw_orig);
	}
}

TEST(StripDfuSuffixTest, KeepsImageShorterThanSuffix)
{
	std::vector<uint8_t> fw = { 'U', 'F', 'D', 16 };

	EXPECT_EQ(strip_dfu_suffix(fw), 0u);
	EXPECT_EQ(fw.size(), 4u);
	fw.clear();
	EXPECT_EQ(strip_dfu_suffix(fw), 0u);
}

class ReadFileTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		temp_dir_ = std::filesystem::temp_directory_path() /
			    "rts5817_flash_cli_test";
		std::filesystem::create_directories(temp_dir_);
	}

	void TearDown() override
	{
		std::filesystem::remove_all(temp_dir_);
	}

	std::string WriteFile(const std::string &name,
			      const std::vector<uint8_t> &data)
	{
		auto path = temp_dir_ / name;

		std::ofstream(path, std::ios::binary)
			.write(reinterpret_cast<const char *>(data.data()),
			       data.size());
		return path.string();
	}

	std::filesystem::path temp_dir_;
};

TEST_F(ReadFileTest, ReadsWholeBinaryFile)
{
	const std::vector<uint8_t> expected = { 0x00, 0xFF, 0x0A, 0x0D, 0x7F };
	std::vector<uint8_t> data;

	ASSERT_TRUE(read_file(WriteFile("fw.bin", expected), data));
	EXPECT_EQ(data, expected);
}

TEST_F(ReadFileTest, EmptyFileYieldsEmptyBuffer)
{
	std::vector<uint8_t> data = { 1, 2, 3 };

	ASSERT_TRUE(read_file(WriteFile("empty.bin", {}), data));
	EXPECT_TRUE(data.empty());
}

TEST_F(ReadFileTest, MissingFileFails)
{
	std::vector<uint8_t> data;

	EXPECT_FALSE(read_file((temp_dir_ / "missing.bin").string(), data));
}

} /* namespace */
