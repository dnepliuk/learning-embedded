#include <stdint.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PIN GPIO_NUM_7

static const char *TAG = "BUTTON";

static volatile uint32_t counter = 0;
static uint32_t last_printed_counter = 0;

static void IRAM_ATTR on_button_pressed(void *argument)
{
    counter++;
}

void app_main(void)
{
    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

    ESP_ERROR_CHECK(gpio_config(&button_config));

    ESP_ERROR_CHECK(
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            BUTTON_PIN,
            on_button_pressed,
            NULL));

    while (1)
    {
        uint32_t current_counter = counter;

        if (current_counter != last_printed_counter)
        {
            ESP_LOGI(
                TAG,
                "Counter: %" PRIu32,
                current_counter);

            last_printed_counter = current_counter;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}