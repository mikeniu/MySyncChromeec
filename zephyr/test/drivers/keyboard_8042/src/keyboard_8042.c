/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atkbd_protocol.h"
#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "i8042_protocol.h"
#include "keyboard_8042.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_protocol.h"
#include "keyboard_scan.h"
#include "power_button.h"
#include "system.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST(keyboard_8042, test_console_cmd__typematic__status)
{
	const char *outbuffer;
	size_t buffer_size;

	const uint8_t scan_code[] = { 0x01, 0x02, 0x03 };

	/* Set a typematic scan code to verify */
	set_typematic_key(scan_code, ARRAY_SIZE(scan_code));

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 typematic"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check that the default typematic configuration is reported */
	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "From host:   0x2b"));
	/* Check that the default delay configuration is reported */
	zassert_not_null(strstr(outbuffer, "First delay: 500 ms"));
	zassert_not_null(strstr(outbuffer, "Inter delay:  91 ms"));
	/* Check that the repeat scan code is not in the output */
	zassert_ok(
		!strstr(outbuffer, "Repeat scan code: {0x01, 0x02, 0x03, }"));
}

ZTEST(keyboard_8042, test_console_cmd__typematic__set_delays)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Set first delay to 123ms and inter delay to 456ms */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 typematic 123 456"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check that new delay configuration is reported */
	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "First delay: 123 ms"));
	zassert_not_null(strstr(outbuffer, "Inter delay: 456 ms"));
}

ZTEST(keyboard_8042, test_console_cmd__codeset__set_codeset1)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Set code set 1 */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 codeset 1"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check that new code set is reported */
	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "Set: 1"));
}

ZTEST(keyboard_8042, test_console_cmd__codeset__set_invalid)
{
	/* Invalid code set */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "8042 codeset 999"));
}

ZTEST(keyboard_8042, test_console_cmd__ram__writeread)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Write test data to the highest address of the control RAM */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 ctrlram 0x1f 0xaa"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check for test data in the output */
	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "31 = 0xaa"));
}

ZTEST(keyboard_8042, test_console_cmd__ram__invalid)
{
	/* Missing args */
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(get_ec_shell(), "8042 ctrlram"));

	/* Address out of bounds */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "8042 ctrlram 9999"));
}

ZTEST(keyboard_8042, test_console_cmd__enable__true)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Enable the keyboard */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 kbd y"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check that keyboard is enabled */
	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "Enabled: 1"));
}

ZTEST(keyboard_8042, test_console_cmd__enable__invalid)
{
	/* Non-bool arg */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "8042 kbd abc"));
}

ZTEST(keyboard_8042, test_console_cmd__internal)
{
	const char *outbuffer;
	size_t buffer_size;

	uint8_t resend_command[] = { 7, 8, 9 };

	test_keyboard_8042_set_resend_command(resend_command,
					      ARRAY_SIZE(resend_command));

	/* Dump the internal state of the keyboard driver */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 internal"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "keyboard_enabled=0"));
	zassert_not_null(strstr(outbuffer, "i8042_keyboard_irq_enabled=0"));
	zassert_not_null(strstr(outbuffer, "i8042_aux_irq_enabled=0"));
	zassert_not_null(strstr(outbuffer, "keyboard_enabled=0"));
	zassert_not_null(strstr(outbuffer, "keystroke_enabled=0"));
	zassert_not_null(strstr(outbuffer, "aux_chan_enabled=0"));
	zassert_not_null(strstr(outbuffer, "controller_ram_address=0x00"));
	zassert_not_null(
		strstr(outbuffer, "resend_command[]={0x07, 0x08, 0x09, }"));
	zassert_not_null(strstr(outbuffer, "A20_status=0"));
}

ZTEST(keyboard_8042, test_console_cmd__invalid)
{
	/* Non-existent subcommand */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "8042 foobar"));
}

ZTEST(keyboard_8042, test_console_cmd__all)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Run all the subcommands */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Just look for the headers since we already tested the individual
	 * subcommands
	 */

	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "- Typematic:"));
	zassert_not_null(strstr(outbuffer, "- Codeset:"));
	zassert_not_null(strstr(outbuffer, "- Control RAM:"));
	zassert_not_null(strstr(outbuffer, "- Keyboard:"));
	zassert_not_null(strstr(outbuffer, "- Internal:"));
}

FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);

ZTEST(keyboard_8042, test_command__system_reset)
{
	keyboard_host_write(I8042_SYSTEM_RESET, true);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	zassert_equal(1, chipset_reset_fake.call_count);
}

FAKE_VOID_FUNC(lpc_keyboard_put_char, uint8_t, int);

ZTEST(keyboard_8042, test_command__read_control_ram)
{
	/* Put test data (0x55) into control RAM */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 ctrlram 0x1 0x55"));

	/* Read offset 0 in the control RAM, which is actually address 0x01.
	 * (address 0x00, the command register, is skipped over)
	 */
	keyboard_host_write(I8042_READ_CTL_RAM + 0, true);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Check the correct byte was reported to the host. */
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(0x55, lpc_keyboard_put_char_fake.arg0_history[0]);
}

ZTEST(keyboard_8042, test_command__a20)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Enable A20 */
	keyboard_host_write(I8042_ENABLE_A20, true);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Verify A20 enabled */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 internal"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "A20_status=1"));

	/* Disable A20 */
	keyboard_host_write(I8042_DISABLE_A20, true);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Verify A20 is not off */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 internal"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "A20_status=0"));
}

ZTEST(keyboard_8042, test_command__pulse)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Sending this pulse command should enable A20 */
	keyboard_host_write(I8042_PULSE_START | BIT(1), true);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Verify A20 enabled */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "8042 internal"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "A20_status=1"));
}

ZTEST(keyboard_8042, test_command__invalid)
{
	/* Unsupported command */
	keyboard_host_write(0x00, true);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Check for NAK sent back to host */
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(I8042_RET_NAK,
		      lpc_keyboard_put_char_fake.arg0_history[0]);
}

ZTEST(keyboard_8042, test_atkbdcommand__resend)
{
	uint8_t resend_data[] = { 0xAA, 0xBB, 0xCC };

	/* Fill in test data to the resend buffer */
	test_keyboard_8042_set_resend_command(resend_data,
					      ARRAY_SIZE(resend_data));

	/* Request a resend */
	keyboard_host_write(ATKBD_CMD_RESEND, false);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Check for above data being sent back to host */
	zassert_equal(3, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(resend_data[0],
		      lpc_keyboard_put_char_fake.arg0_history[0]);
	zassert_equal(resend_data[1],
		      lpc_keyboard_put_char_fake.arg0_history[1]);
	zassert_equal(resend_data[2],
		      lpc_keyboard_put_char_fake.arg0_history[2]);
}

ZTEST(keyboard_8042, test_atkbdcommand__unsupported__setall_mb)
{
	keyboard_host_write(ATKBD_CMD_SETALL_MB, false);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Should respond with a resend request */
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(ATKBD_RET_RESEND,
		      lpc_keyboard_put_char_fake.arg0_history[0]);
}

ZTEST(keyboard_8042, test_atkbdcommand__unsupported__setall_mbr)
{
	keyboard_host_write(ATKBD_CMD_SETALL_MBR, false);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Should respond with a resend request */
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(ATKBD_RET_RESEND,
		      lpc_keyboard_put_char_fake.arg0_history[0]);
}

ZTEST(keyboard_8042, test_atkbdcommand__unsupported__ex_enable)
{
	keyboard_host_write(ATKBD_CMD_EX_ENABLE, false);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Should respond with a resend request */
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(ATKBD_RET_RESEND,
		      lpc_keyboard_put_char_fake.arg0_history[0]);
}

ZTEST(keyboard_8042, test_atkbdcommand__unsupported__bad_cmd)
{
	/* Non-existent ATKBD command */
	keyboard_host_write(0x00, false);

	/* Pause a bit to allow the KB task to process */
	k_sleep(K_MSEC(100));

	/* Should respond with a resend request */
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(ATKBD_RET_RESEND,
		      lpc_keyboard_put_char_fake.arg0_history[0]);
}

static void verify_lpc_chars(const uint8_t *expected, size_t len)
{
	zassert_equal(len, lpc_keyboard_put_char_fake.call_count,
		      "Expected %zu put_char calls, got %u", len,
		      lpc_keyboard_put_char_fake.call_count);
	for (size_t i = 0; i < len; ++i) {
		zassert_equal(expected[i],
			      lpc_keyboard_put_char_fake.arg0_history[i],
			      "Char %zu: expected 0x%02x, got 0x%02x", i,
			      expected[i],
			      lpc_keyboard_put_char_fake.arg0_history[i]);
	}
	RESET_FAKE(lpc_keyboard_put_char);
}

ZTEST(keyboard_8042, test_command__write_control_ram)
{
	keyboard_host_write(I8042_WRITE_CTL_RAM + 0, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(0xaa, false);
	k_sleep(K_MSEC(100));

	keyboard_host_write(I8042_READ_CTL_RAM + 0, true);
	k_sleep(K_MSEC(100));

	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(0xaa, lpc_keyboard_put_char_fake.arg0_history[0]);
}

ZTEST(keyboard_8042, test_command__read_write_cmd_byte)
{
	keyboard_host_write(I8042_WRITE_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(I8042_XLATE | I8042_ENIRQ1, false);
	k_sleep(K_MSEC(100));

	keyboard_host_write(I8042_READ_CMD_BYTE, true);
	k_sleep(K_MSEC(100));

	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(I8042_XLATE | I8042_ENIRQ1,
		      lpc_keyboard_put_char_fake.arg0_history[0]);
}

ZTEST(keyboard_8042, test_command__self_test)
{
	keyboard_host_write(I8042_RESET_SELF_TEST, true);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { 0x55 };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_command__test_kb_port)
{
	keyboard_host_write(I8042_TEST_KB_PORT, true);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { 0x00 };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_command__enable_disable_keyboard)
{
	keyboard_host_write(I8042_DIS_KB, true);
	k_sleep(K_MSEC(100));

	keyboard_host_write(I8042_READ_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_true(lpc_keyboard_put_char_fake.arg0_history[0] &
		     I8042_KBD_DIS);
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_host_write(I8042_ENA_KB, true);
	k_sleep(K_MSEC(100));

	keyboard_host_write(I8042_READ_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_false(lpc_keyboard_put_char_fake.arg0_history[0] &
		      I8042_KBD_DIS);
}

ZTEST(keyboard_8042, test_command__enable_disable_mouse)
{
	keyboard_host_write(I8042_DIS_MOUSE, true);
	k_sleep(K_MSEC(100));

	keyboard_host_write(I8042_READ_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_true(lpc_keyboard_put_char_fake.arg0_history[0] &
		     I8042_AUX_DIS);
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_host_write(I8042_ENA_MOUSE, true);
	k_sleep(K_MSEC(100));

	keyboard_host_write(I8042_READ_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_false(lpc_keyboard_put_char_fake.arg0_history[0] &
		      I8042_AUX_DIS);
}

ZTEST(keyboard_8042, test_command__test_mouse)
{
	keyboard_host_write(I8042_TEST_MOUSE, true);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { 0x00 };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_command__echo_mouse_and_send_to_mouse)
{
	const uint8_t expected[] = { 0x01 };

	keyboard_host_write(I8042_ECHO_MOUSE, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(0x01, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(expected, ARRAY_SIZE(expected));

	keyboard_host_write(I8042_SEND_TO_MOUSE, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(0x02, false);
	k_sleep(K_MSEC(100));
}

ZTEST(keyboard_8042, test_atkbdcommand__echo)
{
	keyboard_host_write(ATKBD_CMD_DIAG_ECHO, false);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { ATKBD_RET_ACK, ATKBD_RET_ECHO };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_atkbdcommand__get_id)
{
	keyboard_host_write(ATKBD_CMD_GETID, false);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { ATKBD_RET_ACK, 0xab, 0x83 };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));

	keyboard_host_write(ATKBD_CMD_OK_GETID, false);
	k_sleep(K_MSEC(100));

	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_atkbdcommand__reset)
{
	keyboard_host_write(ATKBD_CMD_RESET, false);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { ATKBD_RET_ACK, ATKBD_RET_TEST_SUCCESS };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_atkbdcommand__reset_def)
{
	keyboard_host_write(ATKBD_CMD_RESET_DEF, false);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { ATKBD_RET_ACK };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_atkbdcommand__reset_dis)
{
	keyboard_host_write(ATKBD_CMD_RESET_DIS, false);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { ATKBD_RET_ACK };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_atkbdcommand__enable)
{
	keyboard_host_write(ATKBD_CMD_ENABLE, false);
	k_sleep(K_MSEC(100));

	const uint8_t expected[] = { ATKBD_RET_ACK };
	verify_lpc_chars(expected, ARRAY_SIZE(expected));
}

ZTEST(keyboard_8042, test_atkbdcommand__set_rep)
{
	const uint8_t ack[] = { ATKBD_RET_ACK };

	keyboard_host_write(ATKBD_CMD_SETREP, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(0x0f, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));
}

ZTEST(keyboard_8042, test_atkbdcommand__scancode_set_and_get)
{
	const uint8_t ack[] = { ATKBD_RET_ACK };

	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(1, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(ATKBD_CMD_GSCANSET, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(0, false);
	k_sleep(K_MSEC(100));
	const uint8_t set1_resp[] = { ATKBD_RET_ACK, 0x01 };
	verify_lpc_chars(set1_resp, ARRAY_SIZE(set1_resp));

	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(2, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(ATKBD_CMD_GSCANSET, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(0, false);
	k_sleep(K_MSEC(100));
	const uint8_t set2_resp[] = { ATKBD_RET_ACK, 0x02 };
	verify_lpc_chars(set2_resp, ARRAY_SIZE(set2_resp));
}

ZTEST(keyboard_8042, test_atkbdcommand__set_leds)
{
	const uint8_t ack[] = { ATKBD_RET_ACK };

	keyboard_host_write(ATKBD_CMD_SETLEDS, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(0x07, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));
}

ZTEST(keyboard_8042, test_atkbdcommand__set_ex_leds)
{
	const uint8_t ack[] = { ATKBD_RET_ACK };

	keyboard_host_write(ATKBD_CMD_EX_SETLEDS, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(0x01, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));

	keyboard_host_write(0x02, false);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(ack, ARRAY_SIZE(ack));
}

ZTEST(keyboard_8042, test_keypress__single_key_set1_and_set2)
{
	keyboard_host_write(ATKBD_CMD_ENABLE, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(1, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_state_changed(1, 1, 1);
	k_sleep(K_MSEC(100));
	const uint8_t k_press1[] = { 0x01 };
	verify_lpc_chars(k_press1, ARRAY_SIZE(k_press1));

	keyboard_state_changed(1, 1, 0);
	k_sleep(K_MSEC(100));
	const uint8_t k_rel1[] = { 0x81 };
	verify_lpc_chars(k_rel1, ARRAY_SIZE(k_rel1));

	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(2, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(I8042_WRITE_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(I8042_XLATE, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_state_changed(1, 1, 1);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(k_press1, ARRAY_SIZE(k_press1));

	keyboard_state_changed(1, 1, 0);
	k_sleep(K_MSEC(100));
	verify_lpc_chars(k_rel1, ARRAY_SIZE(k_rel1));

	keyboard_host_write(I8042_WRITE_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(0, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_state_changed(1, 1, 1);
	k_sleep(K_MSEC(100));
	const uint8_t k_press2[] = { 0x76 };
	verify_lpc_chars(k_press2, ARRAY_SIZE(k_press2));

	keyboard_state_changed(1, 1, 0);
	k_sleep(K_MSEC(100));
	const uint8_t k_rel2[] = { 0xf0, 0x76 };
	verify_lpc_chars(k_rel2, ARRAY_SIZE(k_rel2));
}

ZTEST(keyboard_8042, test_keypress__override)
{
	keyboard_host_write(ATKBD_CMD_ENABLE, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(1, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_state_changed_process(1, 1, 1, -1);
	k_sleep(K_MSEC(100));
	const uint8_t p1[] = { 0x01 };
	verify_lpc_chars(p1, ARRAY_SIZE(p1));

	keyboard_state_changed_process(1, 1, 0, -1);
	k_sleep(K_MSEC(100));
	const uint8_t r1[] = { 0x81 };
	verify_lpc_chars(r1, ARRAY_SIZE(r1));

	keyboard_state_changed_process(1, 1, 1, 0x16);
	k_sleep(K_MSEC(100));
	const uint8_t p2[] = { 0x02 };
	verify_lpc_chars(p2, ARRAY_SIZE(p2));

	keyboard_state_changed_process(1, 1, 0, 0x16);
	k_sleep(K_MSEC(100));
	const uint8_t r2[] = { 0x82 };
	verify_lpc_chars(r2, ARRAY_SIZE(r2));

	keyboard_state_changed_process(6, 12, 1, 0xe016);
	k_sleep(K_MSEC(100));
	const uint8_t ep[] = { 0xe0, 0x02 };
	verify_lpc_chars(ep, ARRAY_SIZE(ep));

	keyboard_state_changed_process(6, 12, 0, 0xe016);
	k_sleep(K_MSEC(100));
	const uint8_t er[] = { 0xe0, 0x82 };
	verify_lpc_chars(er, ARRAY_SIZE(er));
}

ZTEST(keyboard_8042, test_keypress__disabled)
{
	keyboard_host_write(ATKBD_CMD_RESET_DIS, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_state_changed(1, 1, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(0, lpc_keyboard_put_char_fake.call_count);
}

ZTEST(keyboard_8042, test_typematic__repeat)
{
	keyboard_host_write(ATKBD_CMD_ENABLE, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(1, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(ATKBD_CMD_SETREP, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(0x0f, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_state_changed(1, 1, 1);
	k_sleep(K_MSEC(400));

	zassert_true(lpc_keyboard_put_char_fake.call_count >= 2,
		     "Expected typematic repeats, got %u",
		     lpc_keyboard_put_char_fake.call_count);

	keyboard_state_changed(1, 1, 0);
	k_sleep(K_MSEC(100));
}

ZTEST(keyboard_8042, test_button__power_button)
{
	test_set_chipset_to_s0();

	keyboard_host_write(ATKBD_CMD_ENABLE, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(1, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_update_button(KEYBOARD_BUTTON_POWER, 1);
	k_sleep(K_MSEC(100));
	const uint8_t pwr_press1[] = { 0xe0, 0x5e };
	verify_lpc_chars(pwr_press1, ARRAY_SIZE(pwr_press1));

	keyboard_update_button(KEYBOARD_BUTTON_POWER, 0);
	k_sleep(K_MSEC(100));
	const uint8_t pwr_rel1[] = { 0xe0, 0xde };
	verify_lpc_chars(pwr_rel1, ARRAY_SIZE(pwr_rel1));

	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(2, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(I8042_WRITE_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(0, false);
	k_sleep(K_MSEC(100));
	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_update_button(KEYBOARD_BUTTON_POWER, 1);
	k_sleep(K_MSEC(100));
	const uint8_t pwr_press2[] = { 0xe0, 0x37 };
	verify_lpc_chars(pwr_press2, ARRAY_SIZE(pwr_press2));

	keyboard_update_button(KEYBOARD_BUTTON_POWER, 0);
	k_sleep(K_MSEC(100));
	const uint8_t pwr_rel2[] = { 0xe0, 0xf0, 0x37 };
	verify_lpc_chars(pwr_rel2, ARRAY_SIZE(pwr_rel2));
}

FAKE_VALUE_FUNC(const uint8_t *, system_get_jump_tag, uint16_t, int *, int *);

struct test_kb_state {
	uint8_t codeset;
	uint8_t ctlram;
	uint8_t keystroke_enabled;
};

static struct test_kb_state fake_saved_kb_state;
static bool fake_has_jump_tag;

static const uint8_t *fake_system_get_jump_tag(uint16_t tag, int *version,
					       int *size)
{
	if (tag == 0x4b42 && fake_has_jump_tag) {
		*version = 2;
		*size = sizeof(fake_saved_kb_state);
		return (const uint8_t *)&fake_saved_kb_state;
	}
	return NULL;
}

ZTEST(keyboard_8042, test_sysjump__preserve_and_restore)
{
	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(2, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(I8042_WRITE_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(I8042_XLATE | I8042_ENIRQ1, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(ATKBD_CMD_ENABLE, false);
	k_sleep(K_MSEC(100));

	hook_notify(HOOK_SYSJUMP);

	fake_saved_kb_state.codeset = 2;
	fake_saved_kb_state.ctlram = I8042_XLATE | I8042_ENIRQ1;
	fake_saved_kb_state.keystroke_enabled = 1;
	fake_has_jump_tag = true;
	system_get_jump_tag_fake.custom_fake = fake_system_get_jump_tag;

	keyboard_host_write(ATKBD_CMD_SSCANSET, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(1, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(I8042_WRITE_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	keyboard_host_write(0, false);
	k_sleep(K_MSEC(100));
	keyboard_host_write(ATKBD_CMD_RESET_DIS, false);
	k_sleep(K_MSEC(100));

	hook_notify(HOOK_INIT);
	k_sleep(K_MSEC(100));

	RESET_FAKE(lpc_keyboard_put_char);

	keyboard_host_write(I8042_READ_CMD_BYTE, true);
	k_sleep(K_MSEC(100));
	zassert_equal(1, lpc_keyboard_put_char_fake.call_count);
	zassert_equal(I8042_XLATE | I8042_ENIRQ1,
		      lpc_keyboard_put_char_fake.arg0_history[0]);
}

static scancode_set2_t scancode_test_matrix = {
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
};

extern scancode_set2_t *scancode_set2;

ZTEST(keyboard_8042, test_scancode_sharedlib)
{
	scancode_set2_t *default_matrix = scancode_set2;
	uint8_t default_cols = keyboard_get_cols();

	register_scancode_set2(&scancode_test_matrix, 1);
	zassert_equal(1, keyboard_get_cols());
	zassert_equal_ptr(&scancode_test_matrix, scancode_set2);
	zassert_equal(0, get_scancode_set2(0, 1));

	register_scancode_set2(default_matrix, default_cols);

	zassert_equal(0x41, scancode_translate_set2_to_1(0x83));
	zassert_equal(0x90, scancode_translate_set2_to_1(0x90));
	zassert_equal(0x01, scancode_translate_set2_to_1(0x76));
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	test_keyboard_8042_reset();

	RESET_FAKE(chipset_reset);
	RESET_FAKE(lpc_keyboard_put_char);
	RESET_FAKE(system_get_jump_tag);
	fake_has_jump_tag = false;
}

ZTEST_SUITE(keyboard_8042, drivers_predicate_post_main, NULL, reset, reset,
	    NULL);
