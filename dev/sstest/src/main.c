#include "board.h"
#include "ble_nus.h"
#include "sensors.h"
#include <zephyr/kernel.h>
#include "stts2004.h"
#include "iim42652.h"
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <hal/nrf_saadc.h>

#define ADC_NODE DT_NODELABEL(adc)
#define ADC_CHANNEL_ID 5
#define ADC_RESOLUTION 12
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME_DEFAULT

static const struct device *adc_dev = NULL;

static int adc_init(void)
{
	adc_dev = DEVICE_DT_GET(ADC_NODE);
	if (!device_is_ready(adc_dev))
	{
		printk("ADC device not ready\n");
		return -ENODEV;
	}

	struct adc_channel_cfg channel_cfg = {
		.gain = ADC_GAIN,
		.reference = ADC_REFERENCE,
		.acquisition_time = ADC_ACQUISITION_TIME,
		.channel_id = ADC_CHANNEL_ID,
		.differential = 0,
		.input_positive = NRF_SAADC_INPUT_AIN5, // Only if needed for your SoC
	};

	int ret = adc_channel_setup(adc_dev, &channel_cfg);
	if (ret)
	{
		printk("ADC channel setup failed (%d)\n", ret);
		return ret;
	}
	return 0;
}

static int adc_measure(double *voltage)
{
	if (!adc_dev)
	{
		printk("ADC not initialized\n");
		return -ENODEV;
	}

	int16_t buf;
	struct adc_sequence sequence = {
		.channels = BIT(ADC_CHANNEL_ID),
		.buffer = &buf,
		.buffer_size = sizeof(buf),
		.resolution = ADC_RESOLUTION,
	};

	int ret = adc_read(adc_dev, &sequence);
	if (ret)
	{
		printk("ADC read failed (%d)\n", ret);
		return ret;
	}

	const double vref = 0.6 * 6.0; // Vref  = 0.6 Gain = 1/6, adjust as needed
	*voltage = ((double)buf / (1 << ADC_RESOLUTION)) * vref;
	return 0;
}

// Function to send sensor data as JSON over BLE
static void send_sensor_json(double temp, const iim42652_data_t *iim_data)
{
	char json[512];
	double voltage = 0.0;
	if (adc_measure(&voltage) != 0)
	{
		voltage = -1.0; // Indicate error
	}

	if (iim_data)
	{
		snprintf(json, sizeof(json),
				 "{"
				 "\"temperature\":%.2f,"
				 "\"voltage\":%.3f,"
				 "\"acc\":[%.2f,%.2f,%.2f],"
				 "\"gyro\":[%.2f,%.2f,%.2f],"
				 "\"imu_temp\":%.2f"
				 "}\n",
				 temp,
				 voltage,
				 iim_data->acc[0], iim_data->acc[1], iim_data->acc[2],
				 iim_data->gyro[0], iim_data->gyro[1], iim_data->gyro[2],
				 iim_data->temp);
	}
	else
	{
		snprintf(json, sizeof(json),
				 "{"
				 "\"temperature\":%.2f,"
				 "\"voltage\":%.3f"
				 "}\n",
				 temp,
				 voltage);
	}
	bt_nus_printf("%s", json);
}

// Function to send an error message as JSON over BLE
static void send_error_json(const char *error_msg)
{
	char json[128];
	snprintf(json, sizeof(json),
			 "{"
			 "\"error\":\"%s\""
			 "}\n",
			 error_msg);
	bt_nus_printf("%s", json);
}

int main(void)
{
	iim42652_data_t iim_data;
	double temperature;
	printk("Sample - Bluetooth Peripheral NUS\n");

	brd_init();
	ble_init();
	adc_init();

	printk("Initialization complete\n");

	while (1)
	{
		k_sleep(K_MSEC(50));

		if (STTS2004_temperature(&temperature) != 0)
		{
			printk("Failed to read temperature\n");
			send_error_json("Failed to read temperature");
			continue;
		}
		if (IIM42652_data(&iim_data) != 0)
		{
			printk("Failed to read IIM42652 data\n");
			send_error_json("Failed to read IIM42652 data");
			send_sensor_json(temperature, NULL);
			continue;
		}
		send_sensor_json(temperature, &iim_data);
		// Add
	}

	return 0;
}
