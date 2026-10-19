/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "spi_flash_reg.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(flash_reg_to_protect, NULL, NULL, NULL, NULL, NULL);

ZTEST(flash_reg_to_protect, test_invalid_args)
{
	unsigned int start = 0;
	unsigned int len = 0;

	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(0, 0, NULL, &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(0, 0, &start, NULL));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(0xff, 0, &start, &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(0, 0xff, &start, &len));
}

ZTEST(flash_reg_to_protect, test_no_matching_range)
{
	unsigned int start = 0;
	unsigned int len = 0;

	/* Bad SR1 */
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_BP0, 0, &start,
					       &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_BP1, 0, &start,
					       &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_BP2, 0, &start,
					       &len));

	/* BAD SR2 */
	zassert_equal(EC_ERROR_INVAL, spi_flash_reg_to_protect(
					      SPI_FLASH_SR1_BP0,
					      SPI_FLASH_SR2_CMP, &start, &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_SEC |
						       SPI_FLASH_SR1_BP0,
					       0, &start, &len));
}

ZTEST(flash_reg_to_protect, test_matching_range)
{
	unsigned int start = 0;
	unsigned int len = 0;

	zassert_equal(EC_SUCCESS, spi_flash_reg_to_protect(0, 0, &start, &len));
	zassert_equal(0, start);
	zassert_equal(0, len);
}

#ifdef CONFIG_SPI_FLASH_GD25Q16
ZTEST(flash_reg_to_protect, test_gd25q16_ranges)
{
	/* BP4/BP3 act as SEC/TB, so SR1 bits 5:2 select the range */
	static const struct {
		uint8_t sr1;
		unsigned int len;
	} cases[] = {
		{ 0x24, 0x10000 }, { 0x28, 0x20000 },  { 0x2c, 0x40000 },
		{ 0x30, 0x80000 }, { 0x34, 0x100000 }, { 0x38, 0x200000 },
		{ 0x3c, 0x200000 }, /* BP0 is ignored when protecting all */
	};
	unsigned int start;
	unsigned int len;

	for (int i = 0; i < ARRAY_SIZE(cases); i++) {
		start = 0xdead;
		len = 0xdead;
		zassert_equal(EC_SUCCESS,
			      spi_flash_reg_to_protect(cases[i].sr1, 0, &start,
						       &len),
			      "sr1=0x%02x", cases[i].sr1);
		zassert_equal(0, start, "sr1=0x%02x", cases[i].sr1);
		zassert_equal(cases[i].len, len, "sr1=0x%02x", cases[i].sr1);
	}

	/* The whole 2MB flash is protected by the "All" entry */
	zassert_equal(CONFIG_FLASH_SIZE_BYTES, 0x200000);

	/* SEC/TB are ignored when no protection or all is selected */
	zassert_equal(EC_SUCCESS,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_SEC |
						       SPI_FLASH_SR1_TB,
					       0, &start, &len));
	zassert_equal(0, len);
	zassert_equal(EC_SUCCESS,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_SEC |
						       SPI_FLASH_SR1_TB |
						       SPI_FLASH_SR1_BP2 |
						       SPI_FLASH_SR1_BP1,
					       0, &start, &len));
	zassert_equal(0x200000, len);

	/* Ranges only used with CMP=1 or the upper half are unsupported */
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_TB |
						       SPI_FLASH_SR1_BP0,
					       SPI_FLASH_SR2_CMP, &start,
					       &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_BP2 |
						       SPI_FLASH_SR1_BP0,
					       0, &start, &len));
}
#endif

ZTEST_SUITE(flash_protect_to_reg, NULL, NULL, NULL, NULL, NULL);

ZTEST(flash_protect_to_reg, test_invalid_args)
{
	uint8_t sr1;
	uint8_t sr2;

	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(0, 0, NULL, &sr2));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(0, 0, &sr1, NULL));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(128, 0, &sr1, &sr2));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(128, CONFIG_FLASH_SIZE_BYTES,
					       &sr1, &sr2));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(128, 128, &sr1, &sr2));
}

#ifdef CONFIG_SPI_FLASH_W25Q64
ZTEST(flash_protect_to_reg, test_matching_range)
{
	uint8_t sr1;
	uint8_t sr2;

	zassert_equal(EC_SUCCESS,
		      spi_flash_protect_to_reg(0, 0x400000, &sr1, &sr2));
	zassert_equal(0x38, sr1, "Expected 0x38, but got 0x%02x", sr1);
	zassert_equal(0x00, sr2, "Expected 0x00, but got 0x%02x", sr2);

	zassert_equal(EC_SUCCESS,
		      spi_flash_protect_to_reg(0, 0x200000, &sr1, &sr2));
	zassert_equal(0x34, sr1, "Expected 0x38, but got 0x%02x", sr1);
	zassert_equal(0x00, sr2, "Expected 0x00, but got 0x%02x", sr2);
}
#endif

#ifdef CONFIG_SPI_FLASH_GD25Q16
ZTEST(flash_protect_to_reg, test_gd25q16_ranges)
{
	static const struct {
		unsigned int len;
		uint8_t sr1;
	} cases[] = {
		{ 0, 0x00 },	    { 0x10000, 0x24 },	{ 0x20000, 0x28 },
		{ 0x40000, 0x2c },  { 0x80000, 0x30 },	{ 0x100000, 0x34 },
		{ 0x200000, 0x18 },
	};
	uint8_t sr1;
	uint8_t sr2;

	for (int i = 0; i < ARRAY_SIZE(cases); i++) {
		zassert_equal(EC_SUCCESS,
			      spi_flash_protect_to_reg(0, cases[i].len, &sr1,
						       &sr2),
			      "len=0x%x", cases[i].len);
		zassert_equal(cases[i].sr1, sr1,
			      "len=0x%x: expected 0x%02x, but got 0x%02x",
			      cases[i].len, cases[i].sr1, sr1);
		zassert_equal(0x00, sr2, "Expected 0x00, but got 0x%02x", sr2);
	}

	/* Ranges not in the table (e.g. upper half, 3/4) are rejected */
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(0x100000, 0x100000, &sr1, &sr2));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(0, 0x180000, &sr1, &sr2));
	/* Larger than the 2MB flash */
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(0, 0x400000, &sr1, &sr2));
}
#endif
