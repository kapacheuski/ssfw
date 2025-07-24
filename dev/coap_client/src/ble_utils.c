/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/ring_buffer.h>

#include "ble_utils.h"

LOG_MODULE_REGISTER(ble_utils, CONFIG_BLE_UTILS_LOG_LEVEL);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// Ring buffer configuration
#ifndef CONFIG_BLE_MSG_RING_BUF_SIZE
#define BLE_MSG_RING_BUF_SIZE 2048
#else
#define BLE_MSG_RING_BUF_SIZE CONFIG_BLE_MSG_RING_BUF_SIZE
#endif

#ifndef CONFIG_BLE_MSG_MAX_SIZE
#define BLE_MSG_MAX_SIZE 512
#else
#define BLE_MSG_MAX_SIZE CONFIG_BLE_MSG_MAX_SIZE
#endif

#ifndef CONFIG_BLE_THREAD_STACK_SIZE
#define BLE_THREAD_STACK_SIZE 1024
#else
#define BLE_THREAD_STACK_SIZE CONFIG_BLE_THREAD_STACK_SIZE
#endif

#ifndef CONFIG_BLE_THREAD_PRIORITY
#define BLE_THREAD_PRIORITY 5
#else
#define BLE_THREAD_PRIORITY CONFIG_BLE_THREAD_PRIORITY
#endif

// Ring buffer for BLE messages
static uint8_t ble_msg_ring_buf_data[BLE_MSG_RING_BUF_SIZE];
static struct ring_buf ble_msg_ring_buf;

// Thread for handling BLE messages
static K_THREAD_STACK_DEFINE(ble_thread_stack, BLE_THREAD_STACK_SIZE);
static struct k_thread ble_thread_data;
static k_tid_t ble_thread_tid;

// Semaphore to signal new messages in ring buffer
static K_SEM_DEFINE(ble_msg_sem, 0, 1);

static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey);
static void auth_cancel(struct bt_conn *conn);
static void pairing_complete(struct bt_conn *conn, bool bonded);
static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason);
static void __attribute__((unused))
security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err);

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed};

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	COND_CODE_1(CONFIG_BT_SMP, (.security_changed = security_changed), ())};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static struct k_work on_connect_work;
static struct k_work on_disconnect_work;

static struct bt_conn *current_conn;

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err)
	{
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	LOG_INF("Connected");
	current_conn = bt_conn_ref(conn);

	k_work_submit(&on_connect_work);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)", reason);

	if (current_conn)
	{
		bt_conn_unref(current_conn);
		current_conn = NULL;

		// Clear any pending messages in the ring buffer when disconnected
		ble_utils_clear_ring_buffer();

		k_work_submit(&on_disconnect_work);
	}
}

static char *ble_addr(struct bt_conn *conn)
{
	static char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	return addr;
}

static void __attribute__((unused))
security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err)
{
	char *addr = ble_addr(conn);

	if (!err)
	{
		LOG_INF("Security changed: %s level %u", addr, level);
	}
	else
	{
		LOG_INF("Security failed: %s level %u err %d", addr, level, err);
	}
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char *addr = ble_addr(conn);

	LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char *addr = ble_addr(conn);

	LOG_INF("Pairing cancelled: %s", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char *addr = ble_addr(conn);

	LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char *addr = ble_addr(conn);

	LOG_INF("Pairing failed conn: %s, reason %d", addr, reason);
}

// BLE message sending thread
static void ble_thread_handler(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	uint8_t message_buffer[BLE_MSG_MAX_SIZE];
	uint16_t message_len;

	LOG_INF("BLE message thread started");

	while (1)
	{
		// Wait for semaphore indicating new message
		k_sem_take(&ble_msg_sem, K_FOREVER);

		// Process all messages in the ring buffer
		while (ring_buf_size_get(&ble_msg_ring_buf) > 0)
		{
			struct bt_conn *conn = current_conn;
			if (!conn)
			{
				// No connection, drain the ring buffer
				ring_buf_reset(&ble_msg_ring_buf);
				break;
			}

			// Get message length (first 2 bytes)
			if (ring_buf_get(&ble_msg_ring_buf, (uint8_t *)&message_len, sizeof(message_len)) != sizeof(message_len))
			{
				LOG_ERR("Failed to read message length from ring buffer");
				break;
			}

			// Validate message length
			if (message_len == 0 || message_len > BLE_MSG_MAX_SIZE)
			{
				LOG_ERR("Invalid message length: %d", message_len);
				break;
			}

			// Get message data
			if (ring_buf_get(&ble_msg_ring_buf, message_buffer, message_len) != message_len)
			{
				LOG_ERR("Failed to read message data from ring buffer");
				break;
			}

			// Send message in chunks
			const int chunk_size = 253;
			for (int offset = 0; offset < message_len; offset += chunk_size)
			{
				int send_len = (message_len - offset > chunk_size) ? chunk_size : (message_len - offset);
				int rc = bt_nus_send(conn, &message_buffer[offset], send_len);
				if (rc < 0)
				{
					LOG_ERR("BLE NUS send failed: %d", rc);
					break;
				}
				// Small delay between chunks to avoid overwhelming the BLE stack
				k_sleep(K_MSEC(10));
			}
		}
	}
}

// // Work handler for BLE printf (deprecated - keeping for compatibility)
// static void ble_printf_work_handler(struct k_work *item)
// {
// 	ARG_UNUSED(item);
// 	// This function is now deprecated as we use the thread-based approach
// 	LOG_WRN("Deprecated work handler called");
// }
int ble_utils_init(struct bt_nus_cb *nus_clbs, ble_connection_cb_t on_connect,
				   ble_disconnection_cb_t on_disconnect)
{
	int ret;

	k_work_init(&on_connect_work, on_connect);
	k_work_init(&on_disconnect_work, on_disconnect);

	bt_conn_cb_register(&conn_callbacks);

	if (IS_ENABLED(CONFIG_BT_SMP))
	{
		ret = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (ret)
		{
			LOG_ERR("Failed to register authorization callbacks.");
			goto end;
		}

		ret = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (ret)
		{
			LOG_ERR("Failed to register authorization info callbacks.");
			goto end;
		}
	}

	ret = bt_enable(NULL);
	if (ret)
	{
		LOG_ERR("Bluetooth initialization failed (error: %d)", ret);
		goto end;
	}

	LOG_INF("Bluetooth initialized");

	if (IS_ENABLED(CONFIG_SETTINGS))
	{
		settings_load();
	}

	ret = bt_nus_init(nus_clbs);
	if (ret)
	{
		LOG_ERR("Failed to initialize UART service (error: %d)", ret);
		goto end;
	}

	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd,
						  ARRAY_SIZE(sd));
	if (ret)
	{
		LOG_ERR("Advertising failed to start (error: %d)", ret);
		goto end;
	}

	// Initialize the ring buffer for BLE messages
	ring_buf_init(&ble_msg_ring_buf, sizeof(ble_msg_ring_buf_data), ble_msg_ring_buf_data);

	// Create and start the BLE message handling thread
	ble_thread_tid = k_thread_create(&ble_thread_data, ble_thread_stack,
									 K_THREAD_STACK_SIZEOF(ble_thread_stack),
									 ble_thread_handler, NULL, NULL, NULL,
									 BLE_THREAD_PRIORITY, 0, K_NO_WAIT);

	if (!ble_thread_tid)
	{
		LOG_ERR("Failed to create BLE message thread");
		ret = -ENOMEM;
		goto end;
	}

	k_thread_name_set(ble_thread_tid, "ble_msg_thread");
	LOG_INF("BLE message thread created successfully");

end:
	return ret;
}

// Modified bt_nus_printf to use ring buffer and thread
int bt_nus_printf(const char *fmt, ...)
{
	if (!current_conn)
	{
		return -ENOTCONN;
	}

	char message_buffer[BLE_MSG_MAX_SIZE];
	va_list args;
	va_start(args, fmt);
	int len = vsnprintk(message_buffer, sizeof(message_buffer), fmt, args);
	va_end(args);

	if (len < 0)
	{
		return len;
	}

	// Ensure null-termination
	message_buffer[sizeof(message_buffer) - 1] = '\0';

	// Adjust length if truncated
	if (len >= sizeof(message_buffer))
	{
		len = sizeof(message_buffer) - 1;
	}

	return bt_nus_printf_buffer(message_buffer, len);
}

// ISR-safe version that can be called from any context
int bt_nus_printf_buffer(const char *buffer, size_t len)
{
	if (!current_conn)
	{
		return -ENOTCONN;
	}

	if (!buffer || len == 0)
	{
		return -EINVAL;
	}

	// Limit length to our maximum
	if (len > BLE_MSG_MAX_SIZE - 1)
	{
		len = BLE_MSG_MAX_SIZE - 1;
	}

	// Check if we're in ISR context
	bool in_isr = k_is_in_isr();

	// Check if ring buffer has enough space (message length + 2 bytes for length header)
	uint32_t space_needed = len + sizeof(uint16_t);
	if (ring_buf_space_get(&ble_msg_ring_buf) < space_needed)
	{
		if (!in_isr)
		{
			LOG_WRN("Ring buffer full, dropping message");
		}
		return -ENOMEM;
	}

	// Add message length header to ring buffer
	uint16_t msg_len = (uint16_t)len;
	if (ring_buf_put(&ble_msg_ring_buf, (uint8_t *)&msg_len, sizeof(msg_len)) != sizeof(msg_len))
	{
		if (!in_isr)
		{
			LOG_ERR("Failed to add message length to ring buffer");
		}
		return -ENOMEM;
	}

	// Add message data to ring buffer
	if (ring_buf_put(&ble_msg_ring_buf, (uint8_t *)buffer, len) != len)
	{
		if (!in_isr)
		{
			LOG_ERR("Failed to add message data to ring buffer");
		}
		return -ENOMEM;
	}

	// Signal the thread that a new message is available
	// k_sem_give is ISR-safe in Zephyr
	k_sem_give(&ble_msg_sem);

	return len;
}

// Ultra-safe printf for callback contexts
int bt_nus_printf_safe(const char *msg)
{
	if (!msg)
	{
		return -EINVAL;
	}

	size_t len = strlen(msg);
	return bt_nus_printf_buffer(msg, len);
}

// Get ring buffer statistics for debugging
void ble_utils_get_ring_buffer_stats(uint32_t *used_bytes, uint32_t *free_bytes, uint32_t *total_bytes)
{
	if (used_bytes)
	{
		*used_bytes = ring_buf_size_get(&ble_msg_ring_buf);
	}

	if (free_bytes)
	{
		*free_bytes = ring_buf_space_get(&ble_msg_ring_buf);
	}

	if (total_bytes)
	{
		*total_bytes = BLE_MSG_RING_BUF_SIZE;
	}
}

// Clear the ring buffer (flush all pending messages)
void ble_utils_clear_ring_buffer(void)
{
	ring_buf_reset(&ble_msg_ring_buf);
	LOG_INF("Ring buffer cleared");
}