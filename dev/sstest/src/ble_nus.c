#include "ble_nus.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <stdarg.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

void notif_enabled(bool enabled, void *ctx);
void received(struct bt_conn *conn, const void *data, uint16_t len, void *ctx);
void connected(struct bt_conn *conn, uint8_t err);
void disconnected(struct bt_conn *conn, uint8_t reason);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

static struct bt_conn *current_conn = NULL;

struct bt_nus_cb nus_listener = {
    .notif_enabled = notif_enabled,
    .received = received,
};

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

static void adv_restart(struct k_work *work);
static struct k_work_delayable adv_restart_work;

struct bt_conn *ble_connection(void)
{
    return current_conn;
}

int ble_init()
{
    int err;
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

    k_work_init_delayable(&adv_restart_work, adv_restart);

    return 0;
}
void notif_enabled(bool enabled, void *ctx)
{
    ARG_UNUSED(ctx);

    printk("%s() - %s\n", __func__, (enabled ? "Enabled" : "Disabled"));
}

void received(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
    char message[CONFIG_BT_L2CAP_TX_MTU + 1] = "";

    ARG_UNUSED(ctx);

    memcpy(message, data, MIN(sizeof(message) - 1, len));
    printk("%s() - Len: %d, Message: %s\n", __func__, len, message);

    // Echo received data back to the central
    int err = bt_nus_send(current_conn, message, strlen(message));
    printk("Echoed data back - Result: %d\n", err);
}

void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        printk("Connection failed (err %u)\n", err);
        return;
    }
    current_conn = bt_conn_ref(conn);
    printk("Central connected\n");
}

static void adv_restart(struct k_work *work)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err == -EADDRINUSE)
    {
        printk("Advertising busy, retrying...\n");
        k_work_schedule(&adv_restart_work, K_MSEC(300));
    }
    else if (err)
    {
        printk("Failed to restart advertising: %d\n", err);
        // Optionally, retry on other errors as well
        k_work_schedule(&adv_restart_work, K_MSEC(1000));
    }
    else
    {
        printk("Advertising restarted\n");
    }
}

void disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    printk("Central disconnected (reason %u)\n", reason);

    // Schedule advertising restart after a short delay
    k_work_schedule(&adv_restart_work, K_MSEC(300));
}

// Printf-like function to send a formatted message over Bluetooth NUS
int bt_nus_printf(const char *fmt, ...)
{
    struct bt_conn *conn = ble_connection();
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