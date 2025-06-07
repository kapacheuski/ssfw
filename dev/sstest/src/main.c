/*
 * Copyright (c) 2024 Croxel, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/drivers/sensor.h>
#include <stdarg.h>
#include <zephyr/drivers/gpio.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
const struct gpio_dt_spec vddpctrl =
	GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, vddpctrl_gpios);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

static struct bt_conn *current_conn = NULL;

static void notif_enabled(bool enabled, void *ctx)
{
	ARG_UNUSED(ctx);

	printk("%s() - %s\n", __func__, (enabled ? "Enabled" : "Disabled"));
}

static void received(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
	char message[CONFIG_BT_L2CAP_TX_MTU + 1] = "";

	ARG_UNUSED(ctx);

	memcpy(message, data, MIN(sizeof(message) - 1, len));
	printk("%s() - Len: %d, Message: %s\n", __func__, len, message);

	// Echo received data back to the central
	int err = bt_nus_send(current_conn, message, strlen(message));
	printk("Echoed data back - Result: %d\n", err);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err)
	{
		printk("Connection failed (err %u)\n", err);
		return;
	}
	current_conn = bt_conn_ref(conn);
	printk("Central connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (current_conn)
	{
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	printk("Central disconnected (reason %u)\n", reason);
}

struct bt_nus_cb nus_listener = {
	.notif_enabled = notif_enabled,
	.received = received,
};

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

// Printf-like function to send a formatted message over Bluetooth NUS
static int bt_nus_printf(struct bt_conn *conn, const char *fmt, ...)
{
	if (!conn)
	{
		return -ENOTCONN;
	}
	char buf[512];
	va_list args;
	va_start(args, fmt);
	int len = vsnprintk(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len < 0)
	{
		return len;
	}
	// Ensure null-termination for safety, though bt_nus_send uses length
	buf[sizeof(buf) - 1] = '\0';

	int total_sent = 0;
	const int chunk_size = 253;
	for (int offset = 0; offset < len; offset += chunk_size)
	{
		int send_len = (len - offset > chunk_size) ? chunk_size : (len - offset);
		int rc = bt_nus_send(conn, &buf[offset], send_len);
		if (rc < 0)
		{
			return rc;
		}
		total_sent += send_len;
	}
	return total_sent;
}

int main(void)
{
	int err;

	printk("Sample - Bluetooth Peripheral NUS\n");

	err = bt_nus_cb_register(&nus_listener, NULL);
	if (err)
	{
		printk("Failed to register NUS callback: %d\n", err);
		return err;
	}

	bt_conn_cb_register(&conn_callbacks);

	err = bt_enable(NULL);
	if (err)
	{
		printk("Failed to enable bluetooth: %d\n", err);
		return err;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err)
	{
		printk("Failed to start advertising: %d\n", err);
		return err;
	}

	printk("Initialization complete\n");

	// --- STTS22H temperature read task ---
	const struct device *stts22h = DEVICE_DT_GET_ANY(st_stts22h);
	if (!device_is_ready(stts22h))
	{
		printk("STTS22H sensor not found!\n");
	}
	else
	{
		printk("STTS22H sensor ready\n");
	}

	gpio_pin_configure_dt(&vddpctrl, GPIO_OUTPUT_INACTIVE);
	// gpio_pin_set_dt(&vddpctrl, 1);

	while (true)
	{
		k_sleep(K_SECONDS(5));
		if (stts22h && device_is_ready(stts22h))
		{
			struct sensor_value temp;
			int rc = sensor_sample_fetch(stts22h);
			if (rc == 0)
			{
				rc = sensor_channel_get(stts22h, SENSOR_CHAN_AMBIENT_TEMP, &temp);
			}
			if (rc == 0)
			{
				double t = sensor_value_to_double(&temp);
				printk("STTS22H temperature: %.2f C\n", t);

				// Always call bt_nus_printf, it will check connection internally
				int send_rc = bt_nus_printf(current_conn, "Temp: %.2f C\n", t);
				printk("Sent temperature over BLE: %d\n", send_rc);
			}
			else
			{
				printk("Failed to read STTS22H temperature: %d\n", rc);
				// Always call bt_nus_printf, it will check connection internally
				int send_rc = bt_nus_printf(current_conn, "Temp read error: %d\n", rc);
				printk("Sent error over BLE: %d\n", send_rc);
			}
		}
		else
		{
			printk("STTS22H sensor not ready or not found!\n");
			int send_rc = bt_nus_printf(current_conn, "STTS22H sensor not ready or not found!\n");
			printk("Sent error over BLE: %d\n", send_rc);
		}
	}

	return 0;
}
