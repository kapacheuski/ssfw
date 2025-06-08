#include <zephyr/drivers/i2c.h>
#define STTS2004_I2C_TEMPERATURE_ADDRESS 0x18
#define I2C0_NODE DT_NODELABEL(i2c0)
const struct device *i2c0 = DEVICE_DT_GET(I2C0_NODE);
typedef struct
{
    bool initialized;
    bool data_valid;
    double temperature;
} stts2004_instance_t;

stts2004_instance_t stts2004_instance;

double STTS2004_calculate_temperature(uint16_t raw_temp)
{
    // Convert the raw temperature value to Celsius
    // Adjust the conversion factor as per the sensor's datashee
    int16_t tmp;
    tmp = (raw_temp & 0x1000) ? -(raw_temp & 0x0FFF) : (raw_temp & 0x0FFF); // Sign extend for negative temperatures

    return ((double)tmp) * 0.0625; // Example conversion factor, adjust as needed
}
void stt2004_init()
{
    if (!device_is_ready(i2c0))
    {
        stts2004_instance.initialized = false;
        stts2004_instance.data_valid = false;
        return;
    }

    stts2004_instance.initialized = true;
}
void stt2004_update()
{
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
        stts2004_instance.data_valid = false;
        stts2004_instance.initialized = false;
        return;
    }
    double temperature = STTS2004_calculate_temperature((reg_value[0] << 8) | reg_value[1]); // Convert to Celsius, adjust scaling as needed
    stts2004_instance.temperature = temperature;
    stts2004_instance.data_valid = true;
}
int STTS2004_temperature(double *temp)
{

    if (!stts2004_instance.initialized)
    {
        stt2004_init();
    }

    stt2004_update();
    if (!stts2004_instance.data_valid)
    {
        return -1; // Error: Data not valid
    }
    *temp = stts2004_instance.temperature;
    return 0; // Success
};