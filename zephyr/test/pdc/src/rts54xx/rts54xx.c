/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "common.h"
#include "console.h"
#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "emul/emul_realtek_rts54xx_public.h"
#include "i2c.h"
#include "pdc_trace_msg.h"
#include "test/util.h"
#include "usbc/ppm.h"
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_rts54xx, LOG_LEVEL_INF);

#define RTS5453P_NODE DT_NODELABEL(pdc_emul1)
#define RTS5453P_NODE2 DT_NODELABEL(pdc_emul2)

#define EMUL_PORT 0
#define EMUL2_PORT 1

#define NUM_PORTS 2

static const uint32_t spr_pdos[] = {
	PDO_AUG(1000, 5000, 3000), PDO_FIXED(5000, 3000, 0),
	PDO_AUG(1000, 5000, 3000), PDO_FIXED(9000, 3000, 0),
	PDO_AUG(1000, 5000, 3000), PDO_FIXED(15000, 3000, 0),
	PDO_AUG(1000, 5000, 3000), PDO_FIXED(20000, 3000, 0),
};

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
static const struct emul *emul2 = EMUL_DT_GET(RTS5453P_NODE2);
static const struct device *dev = DEVICE_DT_GET(RTS5453P_NODE);
static const struct device *dev2 = DEVICE_DT_GET(RTS5453P_NODE2);

static void rts54xx_before_test(void *data)
{
	emul_pdc_reset(emul);
	emul_pdc_reset(emul2);
	emul_pdc_set_response_delay(emul, 0);
	emul_pdc_set_response_delay(emul2, 0);
	if (IS_ENABLED(CONFIG_TEST_PDC_MESSAGE_TRACING)) {
		set_pdc_trace_msg_mocks();
	}

	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_ok(emul_pdc_idle_wait(emul2));
}

static int emul_get_src_pdos(enum pdo_offset_t pdo_offset, uint8_t pdo_count,
			     uint32_t *pdos)
{
	return emul_pdc_get_pdos(emul, SOURCE_PDO, pdo_offset, pdo_count,
				 LPM_PDO, pdos);
}

static int emul_get_snk_pdos(enum pdo_offset_t pdo_offset, uint8_t pdo_count,
			     uint32_t *pdos)
{
	return emul_pdc_get_pdos(emul, SINK_PDO, pdo_offset, pdo_count, LPM_PDO,
				 pdos);
}

static int emul_set_src_pdos(enum pdo_offset_t pdo_offset, uint8_t pdo_count,
			     const uint32_t *pdos)
{
	return emul_pdc_set_pdos(emul, SOURCE_PDO, pdo_offset, pdo_count,
				 LPM_PDO, pdos);
}

static int emul_set_snk_pdos(enum pdo_offset_t pdo_offset, uint8_t pdo_count,
			     const uint32_t *pdos)
{
	return emul_pdc_set_pdos(emul, SINK_PDO, pdo_offset, pdo_count, LPM_PDO,
				 pdos);
}

ZTEST_SUITE(rts54xx, NULL, NULL, rts54xx_before_test, NULL, NULL);

ZTEST_USER(rts54xx, test_emul_reset)
{
	uint32_t pdos[PDO_OFFSET_MAX];

	/* Test source PDO reset values. */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_get_src_pdos(PDO_OFFSET_0, 8, pdos));
	zassert_equal(pdos[0], RTS5453P_FIXED1_SRC);
	zassert_equal(pdos[1], RTS5453P_FIXED2_SRC);

	for (int i = 1; i < 7; i++) {
		zassert_equal(pdos[i + 1], 0);
	}

	/* Test sink PDO reset values. */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_get_snk_pdos(PDO_OFFSET_0, 8, pdos));
	zassert_equal(pdos[0], RTS5453P_FIXED_SNK);
	zassert_equal(pdos[1], RTS5453P_BATT_SNK);
	zassert_equal(pdos[2], RTS5453P_VAR_SNK);

	for (int i = 3; i < 7; i++) {
		zassert_equal(pdos[i + 1], 0);
	}
}

ZTEST_USER(rts54xx, test_emul_pdos)
{
	uint32_t pdos[PDO_OFFSET_MAX];

	/* Port partner PDOs aren't currently supported. */
	zassert_ok(emul_pdc_get_pdos(emul, SOURCE_PDO, PDO_OFFSET_0, 1,
				     PARTNER_PDO, pdos));
	zassert_ok(emul_pdc_get_pdos(emul, SINK_PDO, PDO_OFFSET_0, 1,
				     PARTNER_PDO, pdos));

	/* Test PDO overflow. */
	zassert_not_ok(emul_set_src_pdos(PDO_OFFSET_1, 8, spr_pdos));
	zassert_not_ok(emul_set_snk_pdos(PDO_OFFSET_1, 8, spr_pdos));

	zassert_not_ok(emul_get_src_pdos(PDO_OFFSET_5, 8, pdos));
	zassert_not_ok(emul_get_snk_pdos(PDO_OFFSET_5, 8, pdos));

	/* Test that SPR PDOs can be placed in any offset. */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_src_pdos(PDO_OFFSET_1, 7, spr_pdos));
	zassert_ok(emul_get_src_pdos(PDO_OFFSET_1, 7, pdos));
	zassert_ok(memcmp(pdos, spr_pdos, sizeof(uint32_t) * 7));

	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_snk_pdos(PDO_OFFSET_1, 7, spr_pdos));
	zassert_ok(emul_get_snk_pdos(PDO_OFFSET_1, 7, pdos));
	zassert_ok(memcmp(pdos, spr_pdos, sizeof(uint32_t) * 7));
}

ZTEST_USER(rts54xx, test_pdos)
{
	uint32_t pdos[PDO_OFFSET_MAX];
	int num_pdos = UCSI_GET_PDOS_MAX_NUM;

	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_src_pdos(PDO_OFFSET_1, 6, spr_pdos));

	/*
	 * This is implemented using the same underlying code as
	 * emul_pdc_get_pdos so we only need to do a basic test.
	 */
	memset(pdos, 0, sizeof(pdos));

	for (int i = PDO_OFFSET_1; i <= PDO_OFFSET_6; i += num_pdos) {
		if (i + num_pdos > PDO_OFFSET_6) {
			num_pdos = PDO_OFFSET_6 - i + 1;
		}
		/* UCSI GET_PDOS supports a maximum of 4 PDOs per
		 * request. */
		zassert_ok(pdc_get_pdos(dev, SOURCE_PDO, i, num_pdos, LPM_PDO,
					&pdos[i - 1]));
		zassert_ok(emul_pdc_idle_wait(emul));
	}
	zassert_ok(memcmp(pdos, spr_pdos, 6));
}

ZTEST_USER(rts54xx, test_get_hw_config)
{
	struct pdc_hw_config_t config;
	struct i2c_dt_spec i2c_spec = I2C_DT_SPEC_GET(RTS5453P_NODE);

	zassert_not_ok(pdc_get_hw_config(dev, NULL));

	zassert_ok(pdc_get_hw_config(dev, &config));
	zassert_equal(config.bus_type, PDC_BUS_TYPE_I2C);
	zassert_equal(config.i2c.bus, i2c_spec.bus);
	zassert_equal(config.i2c.addr, i2c_spec.addr);
}

static volatile struct {
	const struct device *port_devs[NUM_PORTS];
	bool port_interrupt[NUM_PORTS];
} shared_cb_data;

static void ci_handler_cb(const struct device *cidev,
			  const struct pdc_callback *callback,
			  union cci_event_t cci_event)
{
	if (cci_event.vendor_defined_indicator) {
		for (int i = 0; i < NUM_PORTS; ++i) {
			if (shared_cb_data.port_devs[i] == cidev) {
				LOG_INF("Interrupt on port %d", i);
				shared_cb_data.port_interrupt[i] = true;
				break;
			}
		}
	}
}

bool port_interrupt(int port)
{
	return shared_cb_data.port_interrupt[port];
}

/* Validate IRQ handling for both happy and edge cases. */
ZTEST_USER(rts54xx, test_irq)
{
#define IRQ_TEST_TIMEOUT_MS (TEST_WAIT_FOR_INTERVAL_MS * 5)

	/* Set connector statuses for both ports to be disconnected. This test
	 * only cares about triggering an interrupt / callback, so don't
	 * inadvertently trigger other actions.
	 */
	union connector_status_t status1 = { .connect_status = 0 };
	union connector_status_t status2 = { .connect_status = 0 };
	struct capability_t unused_caps = { 0 };
	struct pdc_callback ci_cb;

	shared_cb_data.port_devs[EMUL_PORT] = dev;
	shared_cb_data.port_devs[EMUL2_PORT] = dev2;
	for (int i = 0; i < NUM_PORTS; ++i) {
		shared_cb_data.port_interrupt[i] = false;
	}

	ci_cb.handler = ci_handler_cb;
	zassert_ok(pdc_add_ci_callback(dev, &ci_cb));
	zassert_ok(pdc_add_ci_callback(dev2, &ci_cb));

	/* Put driver in non-idle state and then queue interrupts. */
	emul_pdc_set_response_delay(emul, IRQ_TEST_TIMEOUT_MS);
	zassert_ok(pdc_get_capability(dev, &unused_caps));

	/* Trigger an interrupt but expect that we don't see interrupts until
	 * the command is completed.
	 */
	zassert_ok(emul_pdc_connect_partner(emul, &status1));
	zassert_ok(emul_pdc_connect_partner(emul2, &status2));
	zassert_false(TEST_WAIT_FOR((port_interrupt(EMUL_PORT) ||
				     port_interrupt(EMUL2_PORT)),
				    TEST_WAIT_FOR_INTERVAL_MS * 4));

	/* Let command complete. */
	zassert_ok(emul_pdc_idle_wait(emul));

	/* Now interrupts should work. */
	zassert_true(TEST_WAIT_FOR((port_interrupt(EMUL_PORT) &&
				    port_interrupt(EMUL2_PORT)),
				   IRQ_TEST_TIMEOUT_MS));
}

ZTEST_USER(rts54xx, test_emul_vdo_set_bounds)
{
	uint8_t types[6] = { 0 };
	uint32_t vdos[6] = { 0 };

	// Test Max Bound: num_vdos = 5 is valid, 6 is invalid
	zassert_ok(emul_pdc_set_vdo(emul, 5, types, vdos),
		   "Failed to set 5 valid VDOs");
	zassert_equal(emul_pdc_set_vdo(emul, 6, types, vdos), -EINVAL,
		      "Accepted 6 VDOs (limit is 5)");

	// Test Type Bound: type 31 is valid, 32 is invalid
	types[0] = 31;
	zassert_ok(emul_pdc_set_vdo(emul, 1, types, vdos),
		   "Failed to set VDO type 31");
	types[0] = 32;
	zassert_equal(emul_pdc_set_vdo(emul, 1, types, vdos), -EINVAL,
		      "Accepted VDO type 32");
}

static union cci_event_t last_cci;

static void test_cc_handler(const struct device *dev,
			    const struct pdc_callback *callback,
			    union cci_event_t cci_event)
{
	last_cci = cci_event;
}

ZTEST_USER(rts54xx, test_get_vdo_invalid_request)
{
	struct pdc_callback cb = { .handler = test_cc_handler };
	// Register the callback to catch command completion events
	pdc_set_cc_callback(dev, &cb);

	// Set specific VDOs in emulator
	uint8_t types[1] = { 1 };
	uint32_t vdos[1] = { 0xAAAAAAAA };
	zassert_ok(emul_pdc_set_vdo(emul, 1, types, vdos),
		   "Failed to set VDO type 1");

	union get_vdo_t vdo_req = { .num_vdos = 2 };
	uint8_t vdo_types_get[] = { 1, 32 }; // 32 is invalid
	uint32_t vdos_get[2] = { 0 };

	// result will be 0 if the command was queued successfully
	zassert_ok(pdc_get_vdo(dev, vdo_req, vdo_types_get, vdos_get));

	// Wait for the driver thread to process the command and the emulator to
	// return CMD_ERROR
	zassert_ok(emul_pdc_idle_wait(emul));

	// Now catch the error from the captured cci_event
	zassert_true(last_cci.error, "Expected CCI error bit to be set");

	// Verify vdos[0] was NOT updated because of the wrong value
	zassert_equal(vdos_get[0], 0x0,
		      "Buffer should not be modified on invalid type");
	zassert_equal(vdos_get[1], 0x0,
		      "Buffer should not be modified on invalid type");

	// Check correct VDO types
	vdo_types_get[0] = 31;
	vdo_types_get[1] = 1;
	zassert_ok(pdc_get_vdo(dev, vdo_req, vdo_types_get, vdos_get));
	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_false(last_cci.error, "CCI error bit not expected");
	// Verify vdos[0] was updated
	zassert_equal(vdos_get[1], 0xAAAAAAAA);
	zassert_equal(vdos_get[0], 0x0);

	// 5. IMPORTANT: Unregister the callback before the function returns
	// to prevent the driver from calling a dangling stack pointer in later
	// tests.
	pdc_set_cc_callback(dev, NULL);
}

ZTEST_USER(rts54xx, test_vdo_integrity_roundtrip)
{
	struct pdc_callback cb = { .handler = test_cc_handler };
	// Register the callback to catch command completion events
	pdc_set_cc_callback(dev, &cb);

	union get_vdo_t vdo_req = { .raw_value = 0 };
	uint8_t set_types[] = { 0, 10, 31 };
	uint32_t set_vdos[] = { 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC };
	uint8_t get_types[3];
	uint32_t get_vdos[3] = { 0 };

	// 1. Setup: Fill disparate VDO slots in the emulator
	zassert_ok(emul_pdc_set_vdo(emul, 3, set_types, set_vdos));

	// 2. Request: Read back those slots in a different order
	vdo_req.num_vdos = 3;
	get_types[0] = 31; // Should get 0xCCCCCCCC
	get_types[1] = 0; // Should get 0xAAAAAAAA
	get_types[2] = 10; // Should get 0xBBBBBBBB

	zassert_ok(pdc_get_vdo(dev, vdo_req, get_types, get_vdos));
	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_false(last_cci.error, "CCI error bit not expected");

	// 3. Verify: Check that values match the requested types
	zassert_equal(get_vdos[0], 0xCCCCCCCC, "VDO Type 31 mismatch");
	zassert_equal(get_vdos[1], 0xAAAAAAAA, "VDO Type 0 mismatch");
	zassert_equal(get_vdos[2], 0xBBBBBBBB, "VDO Type 10 mismatch");

	// 5. IMPORTANT: Unregister the callback before the function returns
	// to prevent the driver from calling a dangling stack pointer in later
	// tests.
	pdc_set_cc_callback(dev, NULL);
}

static uint32_t get_expected_idh(bool usb_device)
{
	union id_header_vdo_rev3 idh_vdo;

	idh_vdo.raw_value = 0;
	idh_vdo.usb_host = true;
	if (usb_device) {
		idh_vdo.usb_device = true;
		idh_vdo.product_type_ufp = IDH_PTYPE_UFP_PERIPH;
	}
	set_idh_product_type_dfp(&idh_vdo, IDH_PTYPE_DFP_HOST);
	idh_vdo.connector_type = USB_TYPEC_RECEPTACLE;
	idh_vdo.usb_vendor_id = USB_VID_GOOGLE;

	return idh_vdo.raw_value;
}

ZTEST_USER(rts54xx, test_vdo_surgical_update)
{
	uint32_t idh;
	union get_vdo_t vdo_req;
	uint8_t vdo_types[] = { VDO_INDEX_IDH };
	/* Non-zero IDH with specific VID (0x1234), Product Type (2), and Modal
	 * bit (1) */
	uint32_t initial_idh = VDO_IDH(1, 0, 2, 1, 0x1234);

	vdo_req.raw_value = 0;
	vdo_req.num_vdos = 1;
	vdo_req.vdo_origin = 0; /* PDC origin */

	/* 1. Setup emulator with a specific ID Header */
	zassert_ok(emul_pdc_set_vdo(emul, 1, vdo_types, &initial_idh));

	/* 2. Trigger re-init of the driver to run surgical update */
	zassert_ok(pdc_reset(dev));
	zassert_ok(emul_pdc_idle_wait(emul));

	/* 3. Verify: bits 30, 29:27, and 22:21 should be updated, others
	 * preserved
	 */
	zassert_ok(pdc_get_vdo(dev, vdo_req, vdo_types, &idh));
	zassert_ok(emul_pdc_idle_wait(emul));

	bool usb_device_cap = true;
	uint32_t expected_idh = get_expected_idh(usb_device_cap);

	zassert_equal(
		idh, expected_idh,
		"IDH VDO should be surgically updated (0x%08x -> 0x%08x), got 0x%08x",
		initial_idh, expected_idh, idh);

	/* 4. Verify specific fields match expected values */
	zassert_equal(PD_IDH_PTYPE(idh), IDH_PTYPE_UFP_PERIPH,
		      "Product Type should be %d", IDH_PTYPE_UFP_PERIPH);
	zassert_equal(PD_IDH_IS_MODAL(idh), 0, "Modal bit should be 0");
	zassert_equal((idh >> 31) & 1, 1, "Host bit should be 1");
	zassert_equal((idh >> 21) & 3, USB_TYPEC_RECEPTACLE,
		      "Connector Type should be %d", USB_TYPEC_RECEPTACLE);
}

ZTEST_USER(rts54xx, test_usb_comm_capable_as_device)
{
	uint32_t idh;
	union get_vdo_t vdo_req;
	struct pdc_info_t info;
	uint8_t vdo_types[] = { VDO_INDEX_IDH };
	uint32_t initial_idh_0 = 0x8A001234;
	uint32_t initial_idh_1 = 0x0C005678;

	vdo_req.raw_value = 0;
	vdo_req.num_vdos = 1;
	vdo_req.vdo_origin = 0; /* PDC origin */

	/* 1. Setup emulators with specific ID Headers */
	zassert_ok(emul_pdc_set_vdo(emul, 1, vdo_types, &initial_idh_0));
	zassert_ok(emul_pdc_set_vdo(emul2, 1, vdo_types, &initial_idh_1));

	/* 2. Trigger re-init of the driver */
	zassert_ok(pdc_reset(dev));
	zassert_ok(pdc_reset(dev2));

	/* Wait for driver to finish initialization */
	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_ok(emul_pdc_idle_wait(emul2));

	/* Verify port 0 (pdc_emul1) has USB Device bit set (bit 30) AND
	 * others preserved/updated as expected
	 */
	zassert_ok(pdc_get_info(dev, &info, true));
	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_true(info.usb_comm_capable_as_device);

	zassert_ok(pdc_get_vdo(dev, vdo_req, vdo_types, &idh));
	zassert_ok(emul_pdc_idle_wait(emul));

	bool usb_device_cap = true;
	uint32_t expected_idh_device_cap = get_expected_idh(usb_device_cap);
	bool usb_device_not_cap = false;
	uint32_t expected_idh = get_expected_idh(usb_device_not_cap);

	zassert_equal(
		idh, expected_idh_device_cap,
		"Port 0 IDH should be surgically updated (0x%08x -> 0x%08x), got 0x%08x",
		initial_idh_0, expected_idh_device_cap, idh);

	/* Verify port 1 (pdc_emul2) was SKIPPED (remains original value) */
	zassert_ok(pdc_get_info(dev2, &info, true));
	zassert_ok(emul_pdc_idle_wait(emul2));
	zassert_false(info.usb_comm_capable_as_device);

	zassert_ok(pdc_get_vdo(dev2, vdo_req, vdo_types, &idh));
	zassert_ok(emul_pdc_idle_wait(emul2));
	zassert_equal(idh, expected_idh,
		      "Port 1 IDH should be unchanged (skipped) (0x%08x)", idh);
}

ZTEST_USER(rts54xx, test_alert_received)
{
	uint32_t ado = 0;
	union vendor_status_change_bits_t vendor_status = { 0 };

	/* Clear alert in PDC emulator */
	zassert_ok(emul_pdc_set_alert(emul, 0x0));

	/* Verify PDC reports no alert received */
	zassert_ok(pdc_get_vendor_status(dev, &vendor_status));
	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_equal(vendor_status.alert_received, 0);

	/* Verify GET_ALERT returns empty ADO */
	zassert_ok(pdc_get_alert(dev, &ado));
	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_equal(ado, 0x0);

	/* Set power button press alert in PDC emulator */
	zassert_ok(emul_pdc_set_alert(emul, 0x80000002));

	/* Verify PDC reports alert received */
	zassert_ok(pdc_get_vendor_status(dev, &vendor_status));
	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_equal(vendor_status.alert_received, 1);

	/* Verify GET_ALERT returns empty ADO */
	zassert_ok(pdc_get_alert(dev, &ado));
	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_equal(ado, 0x80000002);
}

/* UCSI command callback handler. */
void ucsi_cc_callback(const struct device *port, const struct pdc_callback *cb,
		      union cci_event_t cci_event)
{
}

ZTEST_USER(rts54xx, test_idh_vdo_rev3_helpers)
{
	union id_header_vdo_rev3 idh;

	/* Test all DFP product types */
	enum idh_ptype_dfp ptypes[] = { IDH_PTYPE_DFP_NOT_DFP,
					IDH_PTYPE_DFP_HUB, IDH_PTYPE_DFP_HOST,
					IDH_PTYPE_DFP_POWER_BRICK };

	for (int i = 0; i < ARRAY_SIZE(ptypes); i++) {
		idh.raw_value = 0;
		set_idh_product_type_dfp(&idh, ptypes[i]);
		zassert_equal(get_idh_product_type_dfp(&idh), ptypes[i],
			      "DFP product type mismatch for type %d",
			      ptypes[i]);

		/* Verify the bits are set correctly (lo in bit 23, hi in bits
		 * 25:24) relative to the start of the 32-bit VDO. struct
		 * id_header_vdo_rev3: usb_vendor_id : 16 reserved : 5
		 *   connector_type : 2
		 *   product_type_dfp_lo : 1 (bit 23)
		 *   product_type_dfp_hi : 2 (bits 25:24)
		 */
		uint32_t expected_bits = ((ptypes[i] & 0x1) << 23) |
					 ((ptypes[i] >> 1) << 24);
		zassert_equal(
			idh.raw_value & (0x7 << 23), expected_bits,
			"Raw bits mismatch for type %d (raw=0x%08x, expected=0x%08x)",
			ptypes[i], idh.raw_value, expected_bits);
	}
}

ZTEST_USER(rts54xx, test_ap_mode_override_off)
{
	struct capability_t caps_in, caps_out = { 0 };

	/* Set alt mode override to 1 in emulator */
	caps_in.bmOptionalFeatures.alt_mode_override = 1;
	emul_pdc_set_capability(emul, &caps_in);

	/* Use UCSI command to get capabilities */
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_GET_CAPABILITY,
					/*command specific=*/0, NULL,
					(uint8_t *)&caps_out, NULL));
	zassert_ok(emul_pdc_idle_wait(emul));

	/* Verify alt mode override is cleared when AP mode entry is disabled */
	if (IS_ENABLED(CONFIG_USBC_PDC_DISABLE_AP_MODE_ENTRY))
		zassert_false(caps_out.bmOptionalFeatures.alt_mode_override);
	else
		zassert_true(caps_out.bmOptionalFeatures.alt_mode_override);
}

ZTEST_USER(rts54xx, test_dfp_and_ufp_vdo_ack_programming)
{
	uint32_t dfp_vdo_raw = 0, ufp_vdo_raw = 0;
	union get_vdo_t vdo_req;

	vdo_req.raw_value = 0;
	vdo_req.num_vdos = 1;
	vdo_req.vdo_origin = 0; /* PDC origin */

	/* 1. Trigger driver init (runs rts54_set_vdo_id_ack) */
	zassert_ok(pdc_reset(dev));
	zassert_ok(emul_pdc_idle_wait(emul));

	/* 2. Retrieve DFP VDO (VDO_INDEX_PTYPE_DFP_VDO = 1) */
	uint8_t dfp_vdo_type[] = { VDO_INDEX_PTYPE_DFP_VDO };
	zassert_ok(pdc_get_vdo(dev, vdo_req, dfp_vdo_type, &dfp_vdo_raw));
	zassert_ok(emul_pdc_idle_wait(emul));

	union dfp_vdo_rev3 dfp;
	dfp.raw_value = dfp_vdo_raw;
	zassert_equal(dfp.version, DFP_VDO_VERSION_1_2,
		      "DFP VDO version should be 1.2 (got %d)", dfp.version);
	zassert_equal(dfp.usb4_cap, 1, "DFP VDO USB4 cap should be 1");
	zassert_equal(dfp.usb3_cap, 1, "DFP VDO USB3 cap should be 1");
	zassert_equal(dfp.usb2_cap, 1, "DFP VDO USB2 cap should be 1");

	/* 3. Retrieve UFP VDO (VDO_INDEX_PTYPE_UFP1_VDO = 2) */
	uint8_t ufp_vdo_type[] = { VDO_INDEX_PTYPE_UFP1_VDO };
	zassert_ok(pdc_get_vdo(dev, vdo_req, ufp_vdo_type, &ufp_vdo_raw));
	zassert_ok(emul_pdc_idle_wait(emul));

	union ufp_vdo_rev3 ufp;
	ufp.raw_value = ufp_vdo_raw;
	zassert_equal(ufp.version, UFP_VDO_VERSION_1_3,
		      "UFP VDO version should be 1.3 (got %d)", ufp.version);
	zassert_equal(ufp.usb4_cap, 0, "UFP VDO USB4 cap should be 0");
	zassert_equal(ufp.usb3_cap, 0, "UFP VDO USB3 cap should be 0");
	zassert_equal(ufp.usb2_cap, UFP_USB2_CAPABLE,
		      "UFP VDO USB2 cap should be UFP_USB2_CAPABLE");
	zassert_equal(ufp.tbt_support, 0, "UFP VDO TBT support should be 0");
	zassert_equal(ufp.speed, USB_R30_SS_U32_U40_GEN1,
		      "UFP VDO speed should be USB_R30_SS_U32_U40_GEN1");

	/* 4. Verify dev2 without USB4 capability */
	zassert_ok(pdc_reset(dev2));
	zassert_ok(emul_pdc_idle_wait(emul2));

	dfp_vdo_raw = 0;
	zassert_ok(pdc_get_vdo(dev2, vdo_req, dfp_vdo_type, &dfp_vdo_raw));
	zassert_ok(emul_pdc_idle_wait(emul2));

	dfp.raw_value = dfp_vdo_raw;
	zassert_equal(dfp.usb4_cap, 0, "DFP VDO USB4 cap for dev2 should be 0");
}
