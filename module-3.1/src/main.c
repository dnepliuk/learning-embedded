#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define ADC_CHAN ADC_CHANNEL_3

#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BIT_WIDTH ADC_BITWIDTH_DEFAULT

// Опорна напруга для ручного розрахунку (у мілівольтах)
#define V_REF_MANUAL_MV 3300.0
#define ADC_MAX_VAL_12BIT 4095.0

static const char *TAG = "POTENTIOMETER";

void app_main(void)
{
    // --------------------------------------------------
    // 1. Ініціалізація ADC OneShot
    // --------------------------------------------------
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BIT_WIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHAN, &config));

    // --------------------------------------------------
    // 2. Ініціалізація калібрування
    // --------------------------------------------------
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHAN,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BIT_WIDTH,
    };

    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    bool calibrated = (ret == ESP_OK);
    if (!calibrated)
    {
        ESP_LOGE(TAG, "Помилка ініціалізації калібрування! Перевір конфігурацію.");
    }

    // --------------------------------------------------
    // 3. Вивід заголовку таблиці у серійну консоль
    // --------------------------------------------------
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("\nRAW    U_manual(mV)  U_cali(mV)  Error(%%)\n");
    printf("-------------------------------------------\n");

    // --------------------------------------------------
    // 4. Основний цикл вимірювань (~100 мс)
    // --------------------------------------------------
    while (1)
    {
        int adc_raw = 0;
        int voltage_cali = 0;

        // Зчитування RAW
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHAN, &adc_raw));

        // Розрахунок напруги: U = (RAW / 4095) * 3300
        float voltage_manual = ((float)adc_raw / ADC_MAX_VAL_12BIT) * V_REF_MANUAL_MV;

        // Зчитування напруги за допомогою каліброваного АЦП
        if (calibrated)
        {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_raw, &voltage_cali));
        }

        // Обчислення похибки (%)
        float error = 0.0;
        if (voltage_cali > 0)
        {
            // Використовуємо модуль різниці для коректного відсотка
            error = (fabs(voltage_manual - (float)voltage_cali) / (float)voltage_cali) * 100.0;
        }
        else if (voltage_manual > 0.01)
        {
            // Уникаємо ділення на нуль, якщо каліброване 0, але RAW показує сигнал
            error = 100.0;
        }

        printf("%-6d %-13.1f %-11d %.2f\n", adc_raw, voltage_manual, voltage_cali, error);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}