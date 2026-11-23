#pragma once
#include <stdint.h>

typedef struct {
    int32_t red;
    int32_t ir;
    int32_t green;
} sensor_response_t;

int read_sensor(sensor_response_t *response);
int init_sensor();