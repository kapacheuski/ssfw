#include "board.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
const struct gpio_dt_spec vddpctrl =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, vddpctrl_gpios);

void brt_vddp_ctrl(bool on)
{
    // Control the VDDP pin based on the 'on' parameter
    if (on)
    {
        gpio_pin_set_dt(&vddpctrl, 1);
        printk("VDDP control pin set to ON.\n");
    }
    else
    {
        gpio_pin_set_dt(&vddpctrl, 0);
        printk("VDDP control pin set to OFF.\n");
    }
}
void brd_init(void)
{

    // Initialize the board-specific hardware and peripherals here
    // This function is called during the system initialization phase

    // Example: Initialize GPIOs, sensors, etc.
    // gpio_init();
    // sensor_init();
    gpio_pin_configure_dt(&vddpctrl, GPIO_OUTPUT_INACTIVE);

    printk("Board initialized successfully.\n");
}