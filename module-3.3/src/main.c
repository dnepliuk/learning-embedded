#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_adc/adc_oneshot.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "esp_err.h"

// ============================================================================
// Potentiometer
// ============================================================================

// GPIO4 = ADC1 Channel 3
#define POT_CHANNEL ADC_CHANNEL_3

#define ADC_MAX_VALUE 4095

// ============================================================================
// LED
// ============================================================================

#define LED_GPIO GPIO_NUM_6

#define LED_PWM_MODE LEDC_LOW_SPEED_MODE
#define LED_PWM_TIMER LEDC_TIMER_0
#define LED_PWM_CHANNEL LEDC_CHANNEL_0
#define LED_PWM_FREQ_HZ 5000
#define LED_PWM_RESOLUTION LEDC_TIMER_12_BIT

#define LED_PWM_MAX 4095

// ============================================================================
// Motor / MX1508
// ============================================================================

#define MOTOR_IN1 GPIO_NUM_7
#define MOTOR_IN2 GPIO_NUM_8

#define MOTOR_PWM_MODE LEDC_LOW_SPEED_MODE
#define MOTOR_PWM_TIMER LEDC_TIMER_1
#define MOTOR_PWM_CHANNEL LEDC_CHANNEL_1
#define MOTOR_PWM_FREQ_HZ 20000
#define MOTOR_PWM_RESOLUTION LEDC_TIMER_10_BIT

#define MOTOR_PWM_MIN 700
#define MOTOR_PWM_MAX 1023

// Нижче цього ADC мотор вимкнений
#define MOTOR_ADC_THRESHOLD 200

// ============================================================================
// ADC
// ============================================================================

static adc_oneshot_unit_handle_t adc_handle;

static void init_adc(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(
            &init_config,
            &adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc_handle,
            POT_CHANNEL,
            &channel_config));
}

// ============================================================================
// LED PWM
// ============================================================================

static void init_led_pwm(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = LED_PWM_MODE,
        .duty_resolution = LED_PWM_RESOLUTION,
        .timer_num = LED_PWM_TIMER,
        .freq_hz = LED_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ESP_ERROR_CHECK(
        ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = LED_GPIO,
        .speed_mode = LED_PWM_MODE,
        .channel = LED_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LED_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(
        ledc_channel_config(&channel_config));
}

// ============================================================================
// Motor PWM
// ============================================================================

static void init_motor_pwm(void)
{
    /*
     * IN2 тримаємо в LOW.
     * PWM подаємо на IN1.
     */

    gpio_config_t motor_in2_config = {
        .pin_bit_mask = (1ULL << MOTOR_IN2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(
        gpio_config(&motor_in2_config));

    ESP_ERROR_CHECK(
        gpio_set_level(MOTOR_IN2, 0));

    // PWM timer двигуна

    ledc_timer_config_t timer_config = {
        .speed_mode = MOTOR_PWM_MODE,
        .duty_resolution = MOTOR_PWM_RESOLUTION,
        .timer_num = MOTOR_PWM_TIMER,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ESP_ERROR_CHECK(
        ledc_timer_config(&timer_config));

    // PWM GPIO7 -> MX1508 IN1

    ledc_channel_config_t channel_config = {
        .gpio_num = MOTOR_IN1,
        .speed_mode = MOTOR_PWM_MODE,
        .channel = MOTOR_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = MOTOR_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(
        ledc_channel_config(&channel_config));
}

// ============================================================================
// LED brightness
// ============================================================================

static void set_led_brightness(int adc_raw)
{
    /*
     * ADC та LED PWM обидва 12-bit:
     *
     * ADC: 0...4095
     * PWM: 0...4095
     *
     */

    uint32_t duty = adc_raw;

    ESP_ERROR_CHECK(
        ledc_set_duty(
            LED_PWM_MODE,
            LED_PWM_CHANNEL,
            duty));

    ESP_ERROR_CHECK(
        ledc_update_duty(
            LED_PWM_MODE,
            LED_PWM_CHANNEL));
}

// ============================================================================
// Motor speed
// ============================================================================

static void set_motor_speed(int adc_raw)
{
    uint32_t duty = 0;

    /*
     * ADC 0...199 -> Motor OFF
     */
    if (adc_raw >= MOTOR_ADC_THRESHOLD)
    {
        /*
         * ADC:
         * 200 ----------------------- 4095
         *
         * PWM:
         * 700 ----------------------- 1023
         */

        duty =
            MOTOR_PWM_MIN +
            ((uint32_t)(adc_raw - MOTOR_ADC_THRESHOLD) *
             (MOTOR_PWM_MAX - MOTOR_PWM_MIN)) /
                (ADC_MAX_VALUE - MOTOR_ADC_THRESHOLD);
    }

    ESP_ERROR_CHECK(
        ledc_set_duty(
            MOTOR_PWM_MODE,
            MOTOR_PWM_CHANNEL,
            duty));

    ESP_ERROR_CHECK(
        ledc_update_duty(
            MOTOR_PWM_MODE,
            MOTOR_PWM_CHANNEL));
}

// ============================================================================
// Main
// ============================================================================

void app_main(void)
{
    init_adc();
    init_led_pwm();
    init_motor_pwm();

    while (1)
    {
        int adc_raw = 0;

        // Читаємо один потенціометр
        ESP_ERROR_CHECK(
            adc_oneshot_read(
                adc_handle,
                POT_CHANNEL,
                &adc_raw));

        // Те саме значення керує LED
        set_led_brightness(adc_raw);

        // І одночасно двигуном
        set_motor_speed(adc_raw);

        // Для контролю в Serial Monitor

        uint32_t motor_pwm = 0;

        if (adc_raw >= MOTOR_ADC_THRESHOLD)
        {
            motor_pwm =
                MOTOR_PWM_MIN +
                ((uint32_t)(adc_raw - MOTOR_ADC_THRESHOLD) *
                 (MOTOR_PWM_MAX - MOTOR_PWM_MIN)) /
                    (ADC_MAX_VALUE - MOTOR_ADC_THRESHOLD);
        }

        printf(
            "ADC: %4d | LED PWM: %4d | Motor PWM: %4lu\n",
            adc_raw,
            adc_raw,
            (unsigned long)motor_pwm);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}