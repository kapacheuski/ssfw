/*
 * Copyright (c) 2024 Croxel, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board.h"
#include "ble_nus.h"
#include "sensors.h"
#include <zephyr/kernel.h>

// Externs from main.c
extern void brd_init(void);
extern void sensor_init(void);
extern int ble_init(void);

int main(void)
{
	printk("Sample - Bluetooth Peripheral NUS\n");

	brd_init();
	ble_init();
	sensor_init();

	printk("Initialization complete\n");

	while (1)
	{
		k_sleep(K_SECONDS(5));
		sensor_task();
	}

	return 0;
}
