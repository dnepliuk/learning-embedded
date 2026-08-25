idf_component_register(
    SRCS
        "main.c"
        "encoder.c"
        "buzzer.c"
        "safe.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_ledc
        esp_timer
)
