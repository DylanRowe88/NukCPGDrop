#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"

i2c_master_bus_handle_t i2c_bus_init(int sda, int scl, int speed);
i2c_master_bus_handle_t i2c_bus_get(void);
