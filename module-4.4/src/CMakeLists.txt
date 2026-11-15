idf_component_register(
    SRCS "main.c" "clock_logic.c" "display.c" "screen_data.c"
         "bme280_spi.c" "vendor/bosch/bme280.c"
    INCLUDE_DIRS "." "vendor/bosch"
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_uart
                  esp_driver_spi esp_lcd esp_timer
)
