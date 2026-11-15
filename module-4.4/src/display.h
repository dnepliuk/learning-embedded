#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "screen_data.h"

esp_err_t display_init(i2c_master_bus_handle_t bus, uint8_t address);
esp_err_t display_update(const screen_data_t *screen);
