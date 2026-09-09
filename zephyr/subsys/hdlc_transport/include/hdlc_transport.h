/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SUBSYS_HDLC_TRANSPORT_H_
#define ZEPHYR_SUBSYS_HDLC_TRANSPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Standard HDLC channel addresses.
 *
 * HDLC_ADDR_LOG: Dedicated log address matching pw::hdlc::kDefaultLogAddress
 * (1). HDLC_ADDR_SHELL: Dedicated Zephyr shell interactive session address (2).
 * HDLC_ADDR_RPC: Dedicated Pigweed RPC address matching
 * pw::hdlc::kDefaultRpcAddress ('R' = 0x52).
 */
#define HDLC_ADDR_LOG 1
#define HDLC_ADDR_SHELL 2
#define HDLC_ADDR_RPC 0x52

/**
 * Callback function for received HDLC frames.
 *
 * @param data Pointer to decoded frame payload.
 * @param len Length of decoded frame payload.
 * @param user_data User-provided context pointer.
 */
typedef void (*hdlc_rx_callback_t)(const uint8_t *data, size_t len,
				   void *user_data);

/**
 * Send an unnumbered information (UI) HDLC frame over UART.
 *
 * This function is thread-safe. Multiple threads sending frames will be
 * serialized so frame bytes are never interleaved.
 *
 * @param address Destination HDLC address (e.g., HDLC_ADDR_SHELL,
 * HDLC_ADDR_LOG).
 * @param data Frame payload.
 * @param len Length of payload in bytes.
 * @return 0 on success, or negative errno on failure.
 */
int hdlc_send_frame(uint64_t address, const void *data, size_t len);

/**
 * Register a handler for received frames directed to a specific address.
 *
 * @param address HDLC address to subscribe to.
 * @param callback Callback invoked when a valid frame with this address
 * arrives.
 * @param user_data User context passed to callback.
 * @return 0 on success, -EEXIST if address already registered, -ENOMEM if table
 * full.
 */
int hdlc_register_handler(uint64_t address, hdlc_rx_callback_t callback,
			  void *user_data);

/**
 * Unregister a handler for an HDLC address.
 *
 * @param address HDLC address to unsubscribe.
 * @return 0 on success, -ENOENT if not found.
 */
int hdlc_unregister_handler(uint64_t address);

/**
 * Notify the HDLC transport that the system is entering panic mode.
 * In panic mode, mutex locking is bypassed to prevent deadlocks while
 * flushing panic logs over HDLC.
 */
void hdlc_set_panic_mode(void);

struct shell;

/**
 * Get pointer to HDLC shell instance.
 */
const struct shell *shell_backend_hdlc_get_ptr(void);

/**
 * Restore HDLC transport UART interrupt callback and RX interrupt.
 *
 * Used after raw UART access (such as packet mode in efs2) is finished
 * to re-attach the HDLC receive interrupt handler.
 */
void hdlc_transport_restore_irq(void);

#ifdef CONFIG_ZTEST
/**
 * Inject raw bytes into the HDLC decoder for testing purposes.
 *
 * @param data Raw byte stream (including flags, escaped bytes, CRC).
 * @param len Length of byte stream.
 */
void hdlc_test_inject_rx(const uint8_t *data, size_t len);

/**
 * Reset panic mode state for testing purposes.
 */
void hdlc_test_reset_panic(void);

/**
 * Get dropped frames / bytes counters for testing purposes.
 */
uint32_t hdlc_test_get_isr_dropped_frames(void);
uint32_t hdlc_test_get_rx_dropped_bytes(void);
void hdlc_test_set_tx_busy(bool busy);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_HDLC_TRANSPORT_H_ */
