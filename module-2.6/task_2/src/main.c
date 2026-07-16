#include <stdint.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PIN GPIO_NUM_7
#define DEBOUNCE_TIME_MS 50ULL

static const char *TAG = "BUTTON";

static volatile uint32_t interrupt_events = 0;

static uint32_t accepted_counter = 0;
static uint32_t ignored_counter = 0;

static uint64_t last_accepted_time_ms = 0;
static uint32_t last_printed_accepted_counter = 0;

/*
 * Spinlock захищає interrupt_events від одночасного доступу
 * з ISR та основної FreeRTOS-задачі.
 */
static portMUX_TYPE interrupt_events_mux =
    portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR on_button_pressed(void *argument)
{
    portENTER_CRITICAL_ISR(&interrupt_events_mux);

    interrupt_events++;

    portEXIT_CRITICAL_ISR(&interrupt_events_mux);
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

    ESP_LOGI(TAG, "Task 2: Time-based debounce");

    while (1)
    {
        uint32_t events_to_process = 0;

        /*
         * Безпечно копіюємо кількість переривань
         * і обнуляємо спільний лічильник.
         */
        portENTER_CRITICAL(&interrupt_events_mux);

        events_to_process = interrupt_events;
        interrupt_events = 0;

        portEXIT_CRITICAL(&interrupt_events_mux);

        for (uint32_t i = 0; i < events_to_process; i++)
        {
            uint64_t now_ms =
                (uint64_t)esp_timer_get_time() / 1000ULL;

            if (now_ms - last_accepted_time_ms >=
                DEBOUNCE_TIME_MS)
            {
                accepted_counter++;
                last_accepted_time_ms = now_ms;
            }
            else
            {
                ignored_counter++;
            }
        }

        if (accepted_counter !=
            last_printed_accepted_counter)
        {
            ESP_LOGI(
                TAG,
                "Accepted counter: %" PRIu32
                " | Ignored bounce: %" PRIu32,
                accepted_counter,
                ignored_counter);

            last_printed_accepted_counter =
                accepted_counter;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}