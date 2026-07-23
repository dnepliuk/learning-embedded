#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GREEN_LED GPIO_NUM_4
#define YELLOW_LED GPIO_NUM_5
#define RED_LED GPIO_NUM_6

#define GREEN_TIME_MS 5000
#define YELLOW_TIME_MS 2000
#define RED_TIME_MS 5000
#define RED_YELLOW_TIME_MS 2000

#define GREEN_BLINK_COUNT 3
#define GREEN_BLINK_INTERVAL_MS 500

static void set_traffic_light(bool green, bool yellow, bool red)
{
    gpio_set_level(GREEN_LED, green);
    gpio_set_level(YELLOW_LED, yellow);
    gpio_set_level(RED_LED, red);
}

static void blink_green(void)
{
    // Червоний і жовтий під час миготіння вимкнені
    gpio_set_level(RED_LED, 0);
    gpio_set_level(YELLOW_LED, 0);

    for (int i = 0; i < GREEN_BLINK_COUNT; i++)
    {
        gpio_set_level(GREEN_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(GREEN_BLINK_INTERVAL_MS));

        gpio_set_level(GREEN_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(GREEN_BLINK_INTERVAL_MS));
    }
}

void app_main(void)
{
    gpio_config_t led_config = {
        .pin_bit_mask =
            (1ULL << GREEN_LED) |
            (1ULL << YELLOW_LED) |
            (1ULL << RED_LED),

        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    ESP_ERROR_CHECK(gpio_config(&led_config));

    // Початковий стан — усі світлодіоди вимкнені
    set_traffic_light(false, false, false);

    while (1)
    {
        // 1. Зелений — рух дозволено
        set_traffic_light(true, false, false);
        vTaskDelay(pdMS_TO_TICKS(GREEN_TIME_MS));

        // 2. Зелений миготливий
        blink_green();

        // 3. Жовтий — рух заборонено
        set_traffic_light(false, true, false);
        vTaskDelay(pdMS_TO_TICKS(YELLOW_TIME_MS));

        // 4. Червоний — рух заборонено
        set_traffic_light(false, false, true);
        vTaskDelay(pdMS_TO_TICKS(RED_TIME_MS));

        // 5. Червоний і жовтий одночасно
        set_traffic_light(false, true, true);
        vTaskDelay(pdMS_TO_TICKS(RED_YELLOW_TIME_MS));
    }
}