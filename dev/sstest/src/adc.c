#include "adc.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <hal/nrf_saadc.h>
#include <zephyr/sys/printk.h>

#define ADC_NODE DT_NODELABEL(adc)
#define ADC_CHANNEL_ID 5
#define ADC_RESOLUTION 12
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME_DEFAULT

static const struct device *adc_dev = NULL;

int adc_init(void)
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

int adc_measure(double *voltage)
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