#include "board.h"
#include "ble_nus.h"
#include "sensors.h"
#include <zephyr/kernel.h>
// Externs from main.c

int main(void)
{
	printk("Sample - Bluetooth Peripheral NUS\n");

	brd_init();
	ble_init();
	sensor_init();

	printk("Initialization complete\n");

	while (1)
	{
		k_sleep(K_SECONDS(1));
		sensor_task();
	}

	return 0;
}
