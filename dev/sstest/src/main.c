#include "board.h"
#include "ble_nus.h"
#include "sensors.h"
#include "adc.h"
#include <zephyr/kernel.h>
#include "stts2004.h"
#include "iim42652.h"
#include <zephyr/drivers/gpio.h>

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
