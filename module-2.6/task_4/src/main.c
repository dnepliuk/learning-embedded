#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PIN GPIO_NUM_7

#define POLL_INTERVAL_US 5000ULL
#define DEBOUNCE_TIME_MS 50ULL

static const char *TAG = "BUTTON";

static uint32_t counter = 0;
static uint32_t last_printed_counter = 0;

static uint64_t last_debounce_time_ms = 0;

static bool last_raw_state = true;
static bool stable_state = true;
static bool previous_stable_state = true;

static uint64_t get_time_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

/*
 * Викликається системою esp_timer кожні 5 мс.
 *
 * Тут кнопка не читається і debounce не виконується.
 * Callback лише повідомляє основну задачу, що настав
 * час виконати наступне опитування.
 */
static void poll_timer_callback(void *argument)
{
    TaskHandle_t task_handle = (TaskHandle_t)argument;

    xTaskNotifyGive(task_handle);
}

void app_main(void)
{
    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&button_config));

    /*
     * Даємо сигналу на GPIO стабілізуватися.
     */
    vTaskDelay(pdMS_TO_TICKS(100));

    bool initial_state =
        gpio_get_level(BUTTON_PIN) != 0;

    last_raw_state = initial_state;
    stable_state = initial_state;
    previous_stable_state = initial_state;

    last_debounce_time_ms = get_time_ms();

    /*
     * esp_timer буде повідомляти поточну задачу
     * про необхідність опитати кнопку.
     */
    TaskHandle_t app_task_handle =
        xTaskGetCurrentTaskHandle();

    esp_timer_handle_t poll_timer;

    const esp_timer_create_args_t timer_arguments = {
        .callback = poll_timer_callback,
        .arg = app_task_handle,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "button_poll_timer",
    };

    ESP_ERROR_CHECK(
        esp_timer_create(
            &timer_arguments,
            &poll_timer));

    /*
     * Запуск періодичного таймера:
     * 5000 мкс = 5 мс.
     */
    ESP_ERROR_CHECK(
        esp_timer_start_periodic(
            poll_timer,
            POLL_INTERVAL_US));

    ESP_LOGI(
        TAG,
        "Task 4: Polling + debounce without GPIO interrupts");

    while (1)
    {
        /*
         * Очікуємо наступного 5-мс інтервалу.
         * Поки повідомлення немає, задача не займає процесор.
         */
        ulTaskNotifyTake(
            pdTRUE,
            portMAX_DELAY);

        uint64_t now_ms = get_time_ms();

        bool raw_state =
            gpio_get_level(BUTTON_PIN) != 0;

        /*
         * Сирий сигнал змінився.
         * Запускаємо 50-мс перевірку стабільності заново.
         */
        if (raw_state != last_raw_state)
        {
            last_raw_state = raw_state;
            last_debounce_time_ms = now_ms;
        }

        /*
         * Сирий сигнал не змінювався щонайменше 50 мс.
         */
        if (now_ms - last_debounce_time_ms >=
            DEBOUNCE_TIME_MS)
        {
            /*
             * Стабільний стан справді змінився.
             */
            if (raw_state != stable_state)
            {
                previous_stable_state =
                    stable_state;

                stable_state = raw_state;

                /*
                 * Зараховуємо лише підтверджений перехід:
                 *
                 * HIGH → LOW
                 *
                 * Тобто підтверджене натискання.
                 */
                if (previous_stable_state == true &&
                    stable_state == false)
                {
                    counter++;
                }
            }
        }

        if (counter != last_printed_counter)
        {
            ESP_LOGI(
                TAG,
                "Counter: %" PRIu32,
                counter);

            last_printed_counter = counter;
        }
    }
}