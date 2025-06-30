#pragma once

#include <zephyr/drivers/adc.h>

int adc_init(void);
int adc_measure(double *voltage);