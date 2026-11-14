idf_component_register(
    SRCS "main.c" "clock_logic.c" "display.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_uart esp_lcd
)
