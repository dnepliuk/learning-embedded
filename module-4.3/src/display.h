#pragma once

#include "clock_logic.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t display_init(i2c_master_bus_handle_t bus, uint8_t address);
esp_err_t display_update(const rtc_time_t *time, const char *rtc_status,
                         bool weather_ok, double temperature, double pressure);
