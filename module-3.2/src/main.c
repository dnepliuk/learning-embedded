#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#define LDR_ADC_CHANNEL ADC_CHANNEL_3

#define LED_PIN GPIO_NUM_5

// Кількість значень у Simple Moving Average
#define SMA_SIZE 10

// Пороги освітленості
// Менше DARK_THRESHOLD — темно, LED вмикається
// Більше LIGHT_THRESHOLD — світло, LED вимикається
#define DARK_THRESHOLD 1200
#define LIGHT_THRESHOLD 1700

// Період вимірювання
#define SAMPLE_PERIOD_MS 100

static int sma_buffer[SMA_SIZE];
static int sma_index = 0;
static int32_t sma_sum = 0;
static bool sma_initialized = false;

static int sma_update(int new_sample)
{
    // Під час першого виклику заповнюємо весь буфер
    // першим вимірюванням, щоб уникнути середнього з нулями.
    if (!sma_initialized)
    {
        for (int i = 0; i < SMA_SIZE; i++)
        {
            sma_buffer[i] = new_sample;
        }

        sma_sum = new_sample * SMA_SIZE;
        sma_initialized = true;

        return new_sample;
    }

    // Видаляємо старе значення із суми
    sma_sum -= sma_buffer[sma_index];

    // Записуємо нове значення
    sma_buffer[sma_index] = new_sample;

    // Додаємо нове значення до суми
    sma_sum += new_sample;

    // Переходимо до наступної позиції кільцевого буфера
    sma_index++;

    if (sma_index >= SMA_SIZE)
    {
        sma_index = 0;
    }

    return sma_sum / SMA_SIZE;
}

static void led_init(void)
{
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&led_config));

    // Початково світлодіод вимкнений
    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));
}

static adc_oneshot_unit_handle_t adc_init(void)
{
    adc_oneshot_unit_handle_t adc_handle;

    adc_oneshot_unit_init_cfg_t adc_unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(&adc_unit_config, &adc_handle));

    adc_oneshot_chan_cfg_t adc_channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc_handle,
            LDR_ADC_CHANNEL,
            &adc_channel_config));

    return adc_handle;
}

void app_main(void)
{
    led_init();

    adc_oneshot_unit_handle_t adc_handle = adc_init();

    bool led_is_on = false;

    printf("\nLDR automatic lighting started\n");
    printf("GPIO4: LDR ADC input\n");
    printf("GPIO5: LED output\n");
    printf("Dark threshold: %d\n", DARK_THRESHOLD);
    printf("Light threshold: %d\n\n", LIGHT_THRESHOLD);

    printf("RAW\tSMA\tLED\n");

    while (1)
    {
        int raw_value = 0;

        esp_err_t result = adc_oneshot_read(
            adc_handle,
            LDR_ADC_CHANNEL,
            &raw_value);

        if (result != ESP_OK)
        {
            printf("ADC read error: %s\n", esp_err_to_name(result));

            vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
            continue;
        }

        int filtered_value = sma_update(raw_value);

        /*
         * Гістерезис:
         *
         * LED вимкнений:
         *   SMA <= DARK_THRESHOLD -> увімкнути LED
         *
         * LED увімкнений:
         *   SMA >= LIGHT_THRESHOLD -> вимкнути LED
         *
         * Між двома порогами стан LED не змінюється.
         */

        if (!led_is_on && filtered_value <= DARK_THRESHOLD)
        {
            led_is_on = true;
            ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 1));
        }
        else if (led_is_on && filtered_value >= LIGHT_THRESHOLD)
        {
            led_is_on = false;
            ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));
        }

        printf(
            "%d\t%d\t%s\n",
            raw_value,
            filtered_value,
            led_is_on ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}