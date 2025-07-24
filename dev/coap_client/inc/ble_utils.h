/**
 * @file
 * @defgroup ble_utils Bluetooth LE utilities API
 * @{
 */

/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __BLE_UTILS_H__
#define __BLE_UTILS_H__

#include <bluetooth/services/nus.h>

/** @brief Type indicates function called when Bluetooth LE connection
 *         is established.
 *
 * @param[in] item pointer to work item.
 */
typedef void (*ble_connection_cb_t)(struct k_work *item);

/** @brief Type indicates function called when Bluetooth LE connection is ended.
 *
 * @param[in] item pointer to work item.
 */
typedef void (*ble_disconnection_cb_t)(struct k_work *item);

/** @brief Initialize CoAP utilities.
 *
 * @param[in] on_nus_received function to call when NUS receives message
 * @param[in] on_nus_send     function to call when NUS sends message
 * @param[in] on_connect      function to call when Bluetooth LE connection
 *                            is established
 * @param[in] on_disconnect   function to call when Bluetooth LE connection
 *                            is ended
 * @retval 0    On success.
 * @retval != 0 On failure.
 */
int ble_utils_init(struct bt_nus_cb *nus_clbs,
				   ble_connection_cb_t on_connect,
				   ble_disconnection_cb_t on_disconnect);

int bt_nus_printf(const char *fmt, ...);

/** @brief ISR-safe version of bt_nus_printf that can be called from any context.
 *
 * @param[in] buffer    Pre-formatted string buffer to send
 * @param[in] len       Length of the buffer
 * @retval 0    On success.
 * @retval != 0 On failure.
 */
int bt_nus_printf_buffer(const char *buffer, size_t len);

/** @brief Ultra-safe printf for callback contexts (minimal formatting).
 *
 * @param[in] msg    Simple message string to send
 * @retval 0    On success.
 * @retval != 0 On failure.
 */
int bt_nus_printf_safe(const char *msg);

/** @brief Get ring buffer statistics for debugging.
 *
 * @param[out] used_bytes   Number of bytes currently used in ring buffer
 * @param[out] free_bytes   Number of bytes available in ring buffer
 * @param[out] total_bytes  Total ring buffer size
 */
void ble_utils_get_ring_buffer_stats(uint32_t *used_bytes, uint32_t *free_bytes, uint32_t *total_bytes);

/** @brief Clear the ring buffer (flush all pending messages).
 */
void ble_utils_clear_ring_buffer(void);

/**
 * @brief Usage example:
 *
 * // Initialize BLE utils with NUS callbacks
 * struct bt_nus_cb nus_callbacks = { ... };
 * ble_utils_init(&nus_callbacks, on_connect_handler, on_disconnect_handler);
 *
 * // Send messages using bt_nus_printf (messages will be queued in ring buffer)
 * bt_nus_printf("Hello BLE World!\n");
 * bt_nus_printf("Temperature: %d.%d°C\n", temp_int, temp_frac);
 *
 * // Check ring buffer status for debugging
 * uint32_t used, free, total;
 * ble_utils_get_ring_buffer_stats(&used, &free, &total);
 * printk("Ring buffer: %d/%d bytes used\n", used, total);
 *
 * // Clear buffer if needed
 * ble_utils_clear_ring_buffer();
 */

#endif

/**
 * @}
 */
