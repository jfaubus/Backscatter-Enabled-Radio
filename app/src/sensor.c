#include "sensor.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

const struct device *dev = DEVICE_DT_GET_ANY(maxim_max30101);

int init_sensor() {
    if (dev == NULL || !device_is_ready(dev)) {
        printf("MAX30101 not ready\n");
        return 0;
    }
    return 0;
}

int read_sensor(sensor_response_t *response) {
    struct sensor_value red, ir, green;
    sensor_channel_get(dev, SENSOR_CHAN_RED, &red);
    sensor_channel_get(dev, SENSOR_CHAN_IR, &ir);
    sensor_channel_get(dev, SENSOR_CHAN_GREEN, &green);
    
    response->red = red.val1;
    response->ir = ir.val1;
    response->green = green.val1;
    return 0;
}