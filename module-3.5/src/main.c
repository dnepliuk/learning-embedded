#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

#include "esp_err.h"
#include "esp_log.h"

// ----------------------------------------------------
// SERVO CONFIG
// ----------------------------------------------------

#define SERVO_GPIO 18

#define SERVO_SPEED_MODE LEDC_LOW_SPEED_MODE
#define SERVO_CHANNEL LEDC_CHANNEL_0
#define SERVO_TIMER LEDC_TIMER_0

#define SERVO_FREQUENCY_HZ 50

#define SERVO_DUTY_RESOLUTION LEDC_TIMER_13_BIT
#define SERVO_DUTY_BITS 13
#define SERVO_DUTY_LEVELS (1UL << SERVO_DUTY_BITS)

#define SERVO_MIN_PULSE_US 1000
#define SERVO_MAX_PULSE_US 2000
#define SERVO_PERIOD_US 20000

#define SERVO_MAX_ANGLE_DEG 180.0f

// ----------------------------------------------------
// POTENTIOMETER CONFIG
// ----------------------------------------------------

#define POT_ADC_UNIT ADC_UNIT_1
#define POT_ADC_CHANNEL ADC_CHANNEL_3

#define POT_ADC_MIN_RAW 0
#define POT_ADC_MAX_RAW 4095

#define POT_MAX_ANGLE_DEG 270.0f

#define POT_REVERSED false

#define ADC_SAMPLE_COUNT 16

#define COMMON_MAX_ANGLE_DEG                   \
    ((POT_MAX_ANGLE_DEG < SERVO_MAX_ANGLE_DEG) \
         ? POT_MAX_ANGLE_DEG                   \
         : SERVO_MAX_ANGLE_DEG)

static const char *TAG = "SERVO_POT";

static adc_oneshot_unit_handle_t adc_handle;

// ----------------------------------------------------
// UTILITY
// ----------------------------------------------------

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return value;
}

// ----------------------------------------------------
// SERVO
// ----------------------------------------------------

static uint32_t servo_pulse_to_duty(uint32_t pulse_us)
{
    return (uint32_t)(((uint64_t)pulse_us * SERVO_DUTY_LEVELS) /
                      SERVO_PERIOD_US);
}

static esp_err_t servo_init(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = SERVO_SPEED_MODE,
        .duty_resolution = SERVO_DUTY_RESOLUTION,
        .timer_num = SERVO_TIMER,
        .freq_hz = SERVO_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false};

    esp_err_t err = ledc_timer_config(&timer_config);

    if (err != ESP_OK)
    {
        return err;
    }

    // Початкове положення — крайня ліва позиція
    uint32_t initial_duty =
        servo_pulse_to_duty(SERVO_MIN_PULSE_US);

    ledc_channel_config_t channel_config = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = SERVO_SPEED_MODE,
        .channel = SERVO_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = initial_duty,
        .hpoint = 0,
        .flags = {
            .output_invert = 0}};

    return ledc_channel_config(&channel_config);
}

static esp_err_t servo_set_angle(float angle_deg)
{
    angle_deg = clamp_float(
        angle_deg,
        0.0f,
        SERVO_MAX_ANGLE_DEG);

    float pulse_us =
        SERVO_MIN_PULSE_US +
        (angle_deg / SERVO_MAX_ANGLE_DEG) *
            (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US);

    uint32_t duty =
        servo_pulse_to_duty((uint32_t)(pulse_us + 0.5f));

    esp_err_t err = ledc_set_duty(
        SERVO_SPEED_MODE,
        SERVO_CHANNEL,
        duty);

    if (err != ESP_OK)
    {
        return err;
    }

    return ledc_update_duty(
        SERVO_SPEED_MODE,
        SERVO_CHANNEL);
}

// ----------------------------------------------------
// ADC
// ----------------------------------------------------

static esp_err_t potentiometer_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = POT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE};

    esp_err_t err = adc_oneshot_new_unit(
        &unit_config,
        &adc_handle);

    if (err != ESP_OK)
    {
        return err;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT};

    return adc_oneshot_config_channel(
        adc_handle,
        POT_ADC_CHANNEL,
        &channel_config);
}

static int potentiometer_read_average(void)
{
    int sum = 0;

    for (int i = 0; i < ADC_SAMPLE_COUNT; i++)
    {
        int raw = 0;

        ESP_ERROR_CHECK(
            adc_oneshot_read(
                adc_handle,
                POT_ADC_CHANNEL,
                &raw));

        sum += raw;
    }

    return sum / ADC_SAMPLE_COUNT;
}

static float potentiometer_raw_to_angle(int raw)
{
    if (raw < POT_ADC_MIN_RAW)
    {
        raw = POT_ADC_MIN_RAW;
    }
    else if (raw > POT_ADC_MAX_RAW)
    {
        raw = POT_ADC_MAX_RAW;
    }

    float normalized;

    if (POT_REVERSED)
    {
        normalized =
            (float)(POT_ADC_MAX_RAW - raw) /
            (float)(POT_ADC_MAX_RAW - POT_ADC_MIN_RAW);
    }
    else
    {
        normalized =
            (float)(raw - POT_ADC_MIN_RAW) /
            (float)(POT_ADC_MAX_RAW - POT_ADC_MIN_RAW);
    }

    return normalized * POT_MAX_ANGLE_DEG;
}

// ----------------------------------------------------
// MAIN
// ----------------------------------------------------

void app_main(void)
{
    ESP_ERROR_CHECK(servo_init());
    ESP_ERROR_CHECK(potentiometer_init());

    ESP_LOGI(TAG, "Servo GPIO: %d", SERVO_GPIO);
    ESP_LOGI(TAG, "Potentiometer GPIO: 34");
    ESP_LOGI(
        TAG,
        "Common angle range: 0...%.1f degrees",
        COMMON_MAX_ANGLE_DEG);

    float previous_logged_angle = -100.0f;

    while (true)
    {
        int adc_raw = potentiometer_read_average();

        // Фізичний кут потенціометра: 0...270°
        float potentiometer_angle =
            potentiometer_raw_to_angle(adc_raw);

        // Обрізаємо до спільного діапазону: 0...180°
        float servo_angle = clamp_float(
            potentiometer_angle,
            0.0f,
            COMMON_MAX_ANGLE_DEG);

        ESP_ERROR_CHECK(servo_set_angle(servo_angle));

        // Логуємо при зміні кута щонайменше на 1°
        float angle_difference =
            servo_angle - previous_logged_angle;

        if (angle_difference < 0.0f)
        {
            angle_difference = -angle_difference;
        }

        if (angle_difference >= 1.0f)
        {
            ESP_LOGI(
                TAG,
                "Angle from left position: %.1f deg | ADC: %d",
                servo_angle,
                adc_raw);

            previous_logged_angle = servo_angle;
        }

        // Частота оновлення приблизно 50 разів на секунду
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}