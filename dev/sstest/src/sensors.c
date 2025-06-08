#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

const struct device *stts22h = DEVICE_DT_GET_ANY(st_stts22h);

extern int bt_nus_printf(const char *fmt, ...);

void sensor_init(void)
{
    // This function can be used to initialize any sensor-related configurations
    if (!device_is_ready(stts22h))
    {
        printk("STTS22H sensor not found!\n");
        bt_nus_printf("STTS22H sensor not found!\n");
    }
    else
    {
        printk("STTS22H sensor ready\n");
        bt_nus_printf("STTS22H sensor ready\n");
    }
}

void sensor_task(void)
{
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
            int send_rc = bt_nus_printf("Temp: %.2f C\n", t);
            printk("Sent temperature over BLE: %d\n", send_rc);
        }
        else
        {
            printk("Failed to read STTS22H temperature: %d\n", rc);
            int send_rc = bt_nus_printf("Temp read error: %d\n", rc);
            printk("Sent error over BLE: %d\n", send_rc);
        }
    }
    else
    {
        printk("STTS22H sensor not ready or not found!\n");
        int send_rc = bt_nus_printf("STTS22H sensor not ready or not found!\n");
        printk("Sent error over BLE: %d\n", send_rc);
    }
}