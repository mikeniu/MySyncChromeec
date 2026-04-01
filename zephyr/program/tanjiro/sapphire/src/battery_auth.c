#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "timer.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

#define BATCmd_SHA1W 				0x27    // sending 20 bytes challenge from host EC to battery BMU
#define BATCmd_SHA1R 				0x28    // receiving 20 bytes response from battery BMU to host EC
#define BATCmd_ECCMode 				0x3E

/* Battery ECC type */
enum battery_ecc_id {
	BATTERY_ID_NONE = 0,
	BATTERY_ID_TI,
	BATTERY_ID_RENESAS,
	BATTERY_ID_MPC,
};

enum battery_status {
	BAT_ACCEPT = 0x55,
	BAT_REJECT = 0xAA,
	BAT_TIMEOUT,
	BAT_ERROR,
	BAT_ECC_NOT_SUPPORT,
	BAT_REPORT_FAIL,
};

/* ====================================================== */
/* ECC MODE READ */
/* ====================================================== */

static uint8_t get_battery_ecc_mode(void)
{
	int rv;
	int ecc_word;
	uint8_t ecc_type[2];

	rv = sb_read(BATCmd_ECCMode, &ecc_word);
	if (rv) {
		CPRINTS("ECC mode read fail");
		return BATTERY_ID_NONE;
	}

	ecc_type[0] = ecc_word & 0xFF;
	ecc_type[1] = (ecc_word >> 8) & 0xFF;

	CPRINTS("ECC mode raw: %02x %02x",
		ecc_type[0], ecc_type[1]);

	switch ((ecc_type[0] >> 3) & 0x03) {
	case 0:
		return BATTERY_ID_NONE;

	case 1:
		return BATTERY_ID_TI;

	case 2:
		return BATTERY_ID_RENESAS;

	case 3:
		return BATTERY_ID_MPC;
	}

	return BATTERY_ID_NONE;
}

/* ====================================================== */
/* ECDSA VERIFY STUB */
/* Replace with real ECC library */
/* ====================================================== */

static int ecc_ecdsa_verify(uint8_t *msg,
			    uint8_t len,
			    uint8_t *r,
			    uint8_t *s,
			    uint8_t battery_id)
{
	/* TODO: integrate crypto library */

	CPRINTS("ECDSA verify stub");

	return 1; /* accept for test */
}

/* ====================================================== */
/* ECDSA AUTHENTICATION */
/* ====================================================== */

static enum battery_status battery_verify_ecdsa(uint8_t *random_key,
						uint8_t key_len,
						uint8_t battery_id)
{
	uint8_t sig_r[32];
	uint8_t sig_s[32];
	uint8_t tmp[32];
	uint8_t checksum_r = 0;
	uint8_t checksum_s = 0;
	int rv;
	int i;

	/* Send challenge */
	rv = sb_write_block(BATCmd_SHA1W, random_key, key_len);

	if (rv) {
		CPRINTS("challenge write fail");
		return BAT_TIMEOUT;
	}

	k_msleep(500);

	/* Read R */
	rv =  sb_read_sized_block(BATCmd_SHA1R, tmp, 32);

	if (rv)
		return BAT_TIMEOUT;

	memcpy(sig_r, tmp, 32);

    CPRINTS("R:");
    for (i = 0; i < 32; i++)
	    ccprintf("%02x", sig_r[i]);
    ccprintf("\n");

	k_msleep(3);

	/* Read S */
	rv = sb_read_sized_block(BATCmd_SHA1R, tmp, 32);

	if (rv)
		return BAT_TIMEOUT;

	memcpy(sig_s, tmp, 32);
    CPRINTS("S:");
    for (i = 0; i < 32; i++)
	    ccprintf("%02x", sig_r[i]);
    ccprintf("\n");

	for (i = 0; i < key_len; i++) {
		checksum_r |= sig_r[i];
		checksum_s |= sig_s[i];
	}

	if (!checksum_r || !checksum_s) {
		CPRINTS("signature invalid");
		return BAT_REPORT_FAIL;
	}

	if (ecc_ecdsa_verify(random_key,
			     key_len,
			     sig_r,
			     sig_s,
			     battery_id))
		return BAT_ACCEPT;

	return BAT_REJECT;
}

/* ====================================================== */
/* RANDOM GENERATION */
/* ====================================================== */

static uint32_t rand_seed = 0x12345678;

static void generate_random(uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		rand_seed = rand_seed * 1103515245 + 12345;
		buf[i] = (rand_seed >> 16) & 0xFF;
	}
}
/* ====================================================== */
/* MAIN VERIFY ENTRY */
/* ====================================================== */

enum battery_status battery_verify_ecc(void)
{
	uint8_t random_key[32];
	uint8_t ecc_mode;
	enum battery_status status = BAT_REJECT;
	int retry;

	ecc_mode = get_battery_ecc_mode();

	if (ecc_mode == BATTERY_ID_NONE) {
		CPRINTS("battery no ECC support");
		return BAT_ECC_NOT_SUPPORT;
	}

	generate_random(random_key, sizeof(random_key));

	CPRINTS("ECC mode %d", ecc_mode);

	for (retry = 0; retry < 3; retry++) {

		status = battery_verify_ecdsa(random_key,
					      sizeof(random_key),
					      ecc_mode);

		if (status == BAT_ACCEPT)
			break;

		k_msleep(50);
	}

	CPRINTS("ECC verify result %d", status);

	return status;
}

extern enum battery_status battery_verify_ecc(void);

void board_battery_authenticate(void)
{
	enum battery_status ret;

	ret = battery_verify_ecc();

	if (ret == BAT_ACCEPT)
		cprints(CC_CHARGER, "Battery authentication PASS");
	else
		cprints(CC_CHARGER, "Battery authentication FAIL");
}

void base_auth_test(void)
{
    board_battery_authenticate();
}
DECLARE_HOOK(HOOK_AC_CHANGE, base_auth_test, HOOK_PRIO_DEFAULT);
