
#include "iim42652.h"
#include <zephyr/drivers/spi.h>
#define SPI1_NODE DT_NODELABEL(spi1)

const struct device *spi1 = DEVICE_DT_GET(SPI1_NODE);

typedef struct
{
    bool initialized;
    bool data_valid;
} iim42652_instance_t;

iim42652_instance_t iim42652_instance = {
    .initialized = false,
    .data_valid = false,
};

uint8_t IIM42652_read_register(uint8_t reg)
{

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
    if (!device_is_ready(spi1))
    {
        iim42652_instance.initialized = false;
        iim42652_instance.data_valid = false;
        return;
    }

    uint8_t cfg = IIM42652_read_register(IIM42652_DEVICE_CONFIG);
    if (cfg == 0xFF) // Check if the device is not responding
    {
        iim42652_instance.initialized = false;
        iim42652_instance.data_valid = false;
        return;
    }
    // Initialize IIM42652 with default settings
    IIM42652_write_register(IIM42652_DEVICE_CONFIG, cfg | 0x01); // Example: Set configuration register to a known state // Example: Set another register to a known state
                                                                 // delay for sensor initialization
    k_sleep(K_MSEC(100));

    uint8_t setting = 0x0c | 0x03;                        // LN mode, set gyro and accel to LN mode
    IIM42652_write_register(IIM42652_PWR_MGMT0, setting); // Example: Enable accelerometer

    iim42652_instance.initialized = true;
    iim42652_instance.data_valid = false;
}

int IIM42652_data(iim42652_data_t *iim_data)
{

    if (!iim42652_instance.initialized)
    {
        IIM42652_init();
        if (!iim42652_instance.initialized)
        {
            return -1; // Initialization failed
        }
        // Return an error code
    }
    uint8_t rx_data[1 + 2 + 6 + 6] = {0}; // Temp 2 bytes Acc 6 bytes Gyro 6 bytes
    uint8_t tx_data = IIM42652_TEMP_DATA1_UI | 0x80;
    // Read accelerometer data from IIM42652
    struct spi_buf tx_buf = {
        .buf = &tx_data, // No data to send
        .len = 1,
    };
    struct spi_buf rx_buf = {
        .buf = rx_data,
        .len = sizeof(rx_data),
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
    if (rc < 0)
    {
        iim42652_instance.initialized = false;
        iim42652_instance.data_valid = false;
        return -1; // SPI transfer failed
    }

    // assuming the data is in the format:
    // [Temp MSB, Temp LSB, Accel X MSB, Accel X LSB, Accel Y MSB, Accel Y LSB, Accel Z MSB, Accel Z LSB,
    // Gyro X MSB, Gyro X LSB, Gyro Y MSB, Gyro Y LSB, Gyro Z MSB, Gyro Z LSB]
    uint8_t temp_msb = rx_data[0];
    uint8_t temp_lsb = rx_data[1];
    uint8_t *accel_data = rx_data + 1 + 2;    // Skip the first byte (command byte)
    uint8_t *gyro_data = rx_data + 1 + 2 + 6; // Skip the first byte (command byte) and temperature bytes

    // Accelerometer data has maximum scale of 16g, so we need to convert it to mm/s^2
    // Gyroscope data has maximum scale of 2000dps, so we need to convert it to dps
    iim42652_instance.data_valid = true;
    iim42652_instance.initialized = true;
    iim_data->temp = ((int16_t)((temp_msb << 8) | temp_lsb)) / 256.0; // Convert to Celsius
    // Process accelerometer data
    iim_data->acc[0] = (double)((int16_t)((accel_data[0] << 8) | accel_data[1])) / 2048.0; // Convert to g
    iim_data->acc[1] = (double)((int16_t)((accel_data[2] << 8) | accel_data[3])) / 2048.0; // Convert to g
    iim_data->acc[2] = (double)((int16_t)((accel_data[4] << 8) | accel_data[5])) / 2048.0; // Convert to g

    // Process gyroscope data
    iim_data->gyro[0] = (double)((int16_t)((gyro_data[0] << 8) | gyro_data[1])) / 16.4; // Convert to dps
    iim_data->gyro[1] = (double)((int16_t)((gyro_data[2] << 8) | gyro_data[3])) / 16.4; // Convert to dps
    iim_data->gyro[2] = (double)((int16_t)((gyro_data[4] << 8) | gyro_data[5])) / 16.4; // Convert to dps

    return 0;
}