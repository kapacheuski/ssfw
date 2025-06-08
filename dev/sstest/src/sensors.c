#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define STTS2004_I2C_TEMPERATURE_ADDRESS 0x18 // Example I2C address for STTS2004

#define I2C0_NODE DT_NODELABEL(i2c0)

const struct device *i2c0 = DEVICE_DT_GET(I2C0_NODE);

extern int bt_nus_printf(const char *fmt, ...);

void sensor_init(void)
{
    // This function can be used to initialize any sensor-related configurations
    // if (!device_is_ready(stts22h))
    // {
    //     printk("STTS22H sensor not found!\n");
    //     bt_nus_printf("STTS22H sensor not found!\n");
    // }
    // else
    // {
    //     printk("STTS22H sensor ready\n");
    //     bt_nus_printf("STTS22H sensor ready\n");
    // }
}
float STTS2004_calculate_temperature(uint16_t raw_temp)
{
    // Convert the raw temperature value to Celsius
    // Adjust the conversion factor as per the sensor's datashee
    int16_t tmp;
    tmp = (raw_temp & 0x1000) ? -(raw_temp & 0x0FFF) : (raw_temp & 0x0FFF); // Sign extend for negative temperatures

    return ((float)tmp) * 0.0625; // Example conversion factor, adjust as needed
}

float stt2004_temperature()
{
    if (!device_is_ready(i2c0))
    {
        printk("I2C0 device not ready!\n");
        bt_nus_printf("I2C0 device not ready!\n");
        return -1.0f; // Return an error value
    }
    uint8_t reg = 0x05; // Command to read temperature, adjust as needed
    uint8_t reg_value[2] = {0};
    struct i2c_msg msgs[] = {
        {
            .buf = &reg,
            .len = 1,
            .flags = I2C_MSG_WRITE | I2C_MSG_STOP,
        },
        {
            .buf = reg_value,
            .len = 2,
            .flags = I2C_MSG_READ | I2C_MSG_STOP,
        },

    };
    int rc = i2c_transfer(i2c0, msgs, 2, STTS2004_I2C_TEMPERATURE_ADDRESS);

    if (rc < 0)
    {
        printk("I2C transfer failed: %d\n", rc);
        bt_nus_printf("I2C transfer failed: %d\n", rc);
        return -1.0f; // Return an error value
    }
    // Assuming the temperature is in the first byte of the response
    // Combine bytes
    double temperature = STTS2004_calculate_temperature((reg_value[0] << 8) | reg_value[1]); // Convert to Celsius, adjust scaling as needed
    printk("STTS2004 temperature: %.2f C\n", temperature);
    bt_nus_printf("STTS2004 temperature: %.2f C \n", temperature);
    return temperature; // Return the temperature value
}

void sensor_task(void)
{
    stt2004_temperature();
}