#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include "sensors.h"

#define STTS2004_I2C_TEMPERATURE_ADDRESS 0x18 // Example I2C address for STTS2004

#define I2C0_NODE DT_NODELABEL(i2c0)
#define SPI1_NODE DT_NODELABEL(spi1)

const struct device *i2c0 = DEVICE_DT_GET(I2C0_NODE);
const struct device *spi1 = DEVICE_DT_GET(SPI1_NODE);

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
double STTS2004_calculate_temperature(uint16_t raw_temp)
{
    // Convert the raw temperature value to Celsius
    // Adjust the conversion factor as per the sensor's datashee
    int16_t tmp;
    tmp = (raw_temp & 0x1000) ? -(raw_temp & 0x0FFF) : (raw_temp & 0x0FFF); // Sign extend for negative temperatures

    return ((double)tmp) * 0.0625; // Example conversion factor, adjust as needed
}

double stt2004_temperature()
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
uint8_t IIM42652_read_register(uint8_t reg)
{
    // Placeholder for reading a register from IIM42652
    // Implement the actual reading logic here
    if (!device_is_ready(spi1))
    {
        printk("SPI1 device not ready!\n");
        bt_nus_printf("SPI1 device not ready!\n");
        return 0; // Return an error value
    }
    uint8_t tx_buf[2] = {reg | 0x80, 0}; // Read command with MSB set
    uint8_t rx_buf[2] = {0};             // Buffer to store received data
    struct spi_buf tx = {
        .buf = tx_buf,
        .len = sizeof(tx_buf),
    };
    struct spi_buf rx = {
        .buf = rx_buf,
        .len = sizeof(rx_buf),
    };
    struct spi_buf_set tx_set = {
        .buffers = &tx,
        .count = 1,
    };
    struct spi_buf_set rx_set = {
        .buffers = &rx,
        .count = 1,
    };
    struct spi_config spi_cfg = {
        .frequency = 1000000U, // 1 MHz, adjust as needed
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = {
            .gpio = GPIO_DT_SPEC_GET(SPI1_NODE, cs_gpios),
        },
    };
    int rc = spi_transceive(spi1, &spi_cfg, &tx_set, &rx_set);
    if (rc < 0)
    {
        printk("SPI transfer failed: %d\n", rc);
        bt_nus_printf("SPI transfer failed: %d\n", rc);
        return 0; // Return an error value
    }
    return rx_buf[1]; // Return the read value
}

void IIM42652_write_register(uint8_t reg, uint8_t value)
{
    // Placeholder for writing a register to IIM42652
    // Implement the actual writing logic here
    if (!device_is_ready(spi1))
    {
        printk("SPI1 device not ready!\n");
        bt_nus_printf("SPI1 device not ready!\n");
        return;
    }
    uint8_t tx_buf[2] = {reg & 0x7F, value}; // Write command with MSB cleared
    struct spi_buf tx = {
        .buf = tx_buf,
        .len = sizeof(tx_buf),
    };
    struct spi_buf_set tx_set = {
        .buffers = &tx,
        .count = 1,
    };
    struct spi_config spi_cfg = {
        .frequency = 1000000U, // 1 MHz, adjust as needed
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = {
            .gpio = GPIO_DT_SPEC_GET(SPI1_NODE, cs_gpios),
        },
    };
    int rc = spi_write(spi1, &spi_cfg, &tx_set);
    if (rc < 0)
    {
        printk("SPI write failed: %d\n", rc);
        bt_nus_printf("SPI write failed: %d\n", rc);
    }
}

void IIM42652_init(void)
{
    uint8_t cfg = IIM42652_read_register(IIM42652_DEVICE_CONFIG);
    if (cfg == 0xFF) // Check if the device is not responding
    {
        printk("IIM42652 not responding!\n");
        bt_nus_printf("IIM42652 not responding!\n");
        return;
    }
    // Initialize IIM42652 with default settings
    IIM42652_write_register(IIM42652_DEVICE_CONFIG, cfg | 0x01); // Example: Set configuration register to a known state // Example: Set another register to a known state
                                                                 // delay for sensor initialization
    k_sleep(K_MSEC(100));

    uint8_t setting = 0x0c | 0x03; // LN mode, set gyro and accel to LN mode
    // Enable accelerometer and gyroscope
    IIM42652_write_register(IIM42652_PWR_MGMT0, setting); // Example: Enable accelerometer
    k_sleep(K_MSEC(100));
    printk("IIM42652 initialized successfully.\n");
    bt_nus_printf("IIM42652 initialized successfully.\n");
}

void IIM42652_read_accel(void)
{
    IIM42652_init();
    // Adjust the delay as needed for sensor initialization

    // check spi0 device ready
    if (!device_is_ready(spi1))
    {
        printk("SPI0 device not ready!\n");
        bt_nus_printf("SPI0 device not ready!\n");
        return;
    }

    uint8_t accel_rx_data[7] = {0}; // Assuming 6 bytes for accelerometer data (X, Y, Z)
    uint8_t accel_tx_data = 0x1f | 0x80;
    // Read accelerometer data from IIM42652
    struct spi_buf tx_buf = {
        .buf = &accel_tx_data, // No data to send
        .len = 1,
    };
    struct spi_buf rx_buf = {
        .buf = accel_rx_data,
        .len = sizeof(accel_rx_data),
    };
    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };
    struct spi_buf_set rx = {
        .buffers = &rx_buf,
        .count = 1,
    };
    struct spi_config spi_cfg = {
        .frequency = 1000000U, // 1 MHz, adjust as needed
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = {
            .gpio = GPIO_DT_SPEC_GET(SPI1_NODE, cs_gpios),
        },
    };
    int rc = spi_transceive(spi1, &spi_cfg, &tx, &rx);
    uint8_t *accel_data = accel_rx_data + 1; // Skip the first byte (command byte)
    if (rc < 0)
    {
        printk("SPI transfer failed: %d\n", rc);
        bt_nus_printf("SPI transfer failed: %d\n", rc);
        return;
    }
    // Process accelerometer data
    int16_t x = (int16_t)((accel_data[0] << 8) | accel_data[1]);
    int16_t y = (int16_t)((accel_data[2] << 8) | accel_data[3]);
    int16_t z = (int16_t)((accel_data[4] << 8) | accel_data[5]);
    printk("Accelerometer data: X=%d, Y=%d, Z=%d\n", x, y, z);
    bt_nus_printf("Accelerometer data: X=%d, Y=%d, Z=%d\n", x, y, z);
}

void sensor_task(void)
{
    stt2004_temperature();
    IIM42652_read_accel();
}