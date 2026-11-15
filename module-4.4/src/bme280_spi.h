#pragma once

#include "esp_err.h"
#include "weather_data.h"

/* SPI2: CS=GPIO10, MOSI=GPIO11, SCK=GPIO12, MISO=GPIO13.
 * All calls belong to the application task; no concurrent access. */
esp_err_t bme280_spi_init(void);
esp_err_t bme280_spi_read(weather_data_t *weather);
