#include <stdbool.h>
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

#define PRESS_CHECK_MS 5ULL
#define RELEASE_CHECK_MS 50ULL

static const char *TAG = "BUTTON";

/*
 * Подія від ISR.
 * true означає, що був зафіксований спадний фронт.
 */
static volatile bool button_event = false;

/*
 * Захищає button_event від одночасного доступу
 * з ISR та основної задачі.
 */
static portMUX_TYPE button_event_mux =
    portMUX_INITIALIZER_UNLOCKED;

static uint32_t counter = 0;
static uint32_t last_printed_counter = 0;

typedef enum
{
    BUTTON_STATE_RELEASED,
    BUTTON_STATE_PRESS_CHECK,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_RELEASE_CHECK
} button_state_t;

static button_state_t button_state =
    BUTTON_STATE_RELEASED;

static uint64_t state_start_time_ms = 0;

static uint64_t get_time_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static void IRAM_ATTR on_button_pressed(void *argument)
{
    (void)argument;

    portENTER_CRITICAL_ISR(&button_event_mux);

    button_event = true;

    portEXIT_CRITICAL_ISR(&button_event_mux);
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

    /*
     * Даємо сигналу кнопки стабілізуватися перед
     * увімкненням переривання.
     */
    vTaskDelay(pdMS_TO_TICKS(100));

    portENTER_CRITICAL(&button_event_mux);
    button_event = false;
    portEXIT_CRITICAL(&button_event_mux);

    ESP_ERROR_CHECK(
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            BUTTON_PIN,
            on_button_pressed,
            NULL));

    ESP_LOGI(TAG, "Task 3: State-based debounce");

    while (1)
    {
        bool event_detected = false;

        /*
         * Безпечно забираємо подію, встановлену ISR.
         */
        portENTER_CRITICAL(&button_event_mux);

        if (button_event)
        {
            event_detected = true;
            button_event = false;
        }

        portEXIT_CRITICAL(&button_event_mux);

        uint64_t now_ms = get_time_ms();

        switch (button_state)
        {
        case BUTTON_STATE_RELEASED:
            if (event_detected)
            {
                button_state =
                    BUTTON_STATE_PRESS_CHECK;

                state_start_time_ms = now_ms;
            }
            break;

        case BUTTON_STATE_PRESS_CHECK:
            if (now_ms - state_start_time_ms >=
                PRESS_CHECK_MS)
            {
                if (gpio_get_level(BUTTON_PIN) == 0)
                {
                    counter++;

                    button_state =
                        BUTTON_STATE_PRESSED;
                }
                else
                {
                    button_state =
                        BUTTON_STATE_RELEASED;
                }
            }
            break;

        case BUTTON_STATE_PRESSED:
            if (gpio_get_level(BUTTON_PIN) == 1)
            {
                button_state =
                    BUTTON_STATE_RELEASE_CHECK;

                state_start_time_ms = now_ms;
            }
            break;

        case BUTTON_STATE_RELEASE_CHECK:
            if (gpio_get_level(BUTTON_PIN) == 0)
            {
                /*
                 * Контакт знову став LOW до завершення
                 * перевірки — це брязкіт при відпусканні.
                 */
                button_state =
                    BUTTON_STATE_PRESSED;
            }
            else if (now_ms - state_start_time_ms >=
                     RELEASE_CHECK_MS)
            {
                button_state =
                    BUTTON_STATE_RELEASED;

                /*
                 * Видаляємо події, які могли з'явитися
                 * через брязкіт під час відпускання.
                 */
                portENTER_CRITICAL(&button_event_mux);
                button_event = false;
                portEXIT_CRITICAL(&button_event_mux);
            }
            break;
        }

        if (counter != last_printed_counter)
        {
            ESP_LOGI(
                TAG,
                "Counter: %" PRIu32,
                counter);

            last_printed_counter = counter;
        }

        /*
         * Віддаємо процесор іншим FreeRTOS-задачам.
         * Один tick може бути довшим за 1 мс, але перевірки
         * працюють за умовою "минуло не менше часу".
         */
        vTaskDelay(1);
    }
}