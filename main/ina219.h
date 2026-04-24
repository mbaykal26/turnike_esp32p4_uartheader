#pragma once
#include "esp_err.h"

esp_err_t ina219_init(void);
esp_err_t ina219_read(float *voltage_v, float *current_ma, float *power_w);
