#include <string.h>

#include "bme280_spi.h"
#include "clock_logic.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SDA_PIN GPIO_NUM_9
#define SCL_PIN GPIO_NUM_8
#define I2C_TIMEOUT_MS 100
#define UPDATE_PERIOD_US INT64_C(500000)
#define SENSOR_RETRY_US INT64_C(5000000)
#define CONSOLE_UART UART_NUM_0 /* CH343 USB-UART bridge, COM6 on this PC. */

static const char *TAG = "module_4_4";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_rtc;
static bool s_console_ready;

static esp_err_t add_device(uint8_t address, i2c_master_dev_handle_t *device)
{
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_bus, address, I2C_TIMEOUT_MS), TAG,
                        "No response at 0x%02X", (unsigned)address);
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = address,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(s_bus, &config, device);
}

static esp_err_t init_i2c(void)
{
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0, .sda_io_num = SDA_PIN, .scl_io_num = SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &s_bus), TAG, "I2C init failed");
    if (!gpio_get_level(SDA_PIN) || !gpio_get_level(SCL_PIN)) {
        ESP_LOGE(TAG, "I2C held LOW: SDA=%d SCL=%d. Check wiring and 3.3 V pull-ups.",
                 gpio_get_level(SDA_PIN), gpio_get_level(SCL_PIN));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t read_rtc(rtc_time_t *time)
{
    if (!s_rtc) return ESP_ERR_NOT_FOUND;
    const uint8_t first_register = 0;
    uint8_t registers[7];
    /* A single burst reads a consistent DS1307 time snapshot. */
    esp_err_t err = i2c_master_transmit_receive(s_rtc, &first_register, 1,
                                               registers, sizeof registers, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    return rtc_decode(registers, time) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t set_rtc(const rtc_time_t *time)
{
    if (!s_rtc) return ESP_ERR_NOT_FOUND;
    uint8_t message[8] = {0};
    if (!rtc_encode(time, message + 1)) return ESP_ERR_INVALID_ARG;
    /* Only the explicit TIME command writes the RTC; EEPROM is never written. */
    return i2c_master_transmit(s_rtc, message, sizeof message, I2C_TIMEOUT_MS);
}

static void handle_command(const char *line)
{
    rtc_time_t time;
    if (strncmp(line, "TIME ", 5) != 0 || !rtc_parse_text(line + 5, &time)) {
        ESP_LOGW(TAG, "Use TIME YYYY-MM-DD HH:MM:SS (local time, year 2000..2099)");
        return;
    }
    const esp_err_t err = set_rtc(&time);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "RTC set to %s; oscillator started.", line + 5);
    else
        ESP_LOGE(TAG, "RTC write failed: %s", esp_err_to_name(err));
}

static void poll_console(void)
{
    if (!s_console_ready) return;
    static char line[64];
    static size_t used;
    static bool overflow;
    uint8_t bytes[64];
    const int received = uart_read_bytes(CONSOLE_UART, bytes, sizeof bytes, 0);
    for (int i = 0; i < received; ++i) {
        const uint8_t c = bytes[i];
        if (c == '\r' || c == '\n') {
            if (overflow) ESP_LOGW(TAG, "Command too long; discarded");
            else if (used) { line[used] = '\0'; handle_command(line); }
            used = 0; overflow = false;
        } else if (c == 8 || c == 127) {
            if (used && !overflow) --used;
        } else if (c >= 32 && c < 127 && !overflow) {
            if (used < sizeof line - 1) line[used++] = (char)c;
            else overflow = true;
        }
    }
}

static bool init_display(void)
{
    uint8_t address = 0x3c;
    if (i2c_master_probe(s_bus, address, I2C_TIMEOUT_MS) != ESP_OK) address = 0x3d;
    esp_err_t err = i2c_master_probe(s_bus, address, I2C_TIMEOUT_MS);
    if (err == ESP_OK) err = display_init(s_bus, address);
    if (err == ESP_OK) ESP_LOGI(TAG, "SSD1306 128x64 ready at 0x%02X", (unsigned)address);
    else ESP_LOGE(TAG, "OLED init failed: %s; readings continue in Serial", esp_err_to_name(err));
    return err == ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Module 4.4: DS1307 + SSD1306 on I2C, BME280 on SPI2");
    if (!logic_self_test()) {
        ESP_LOGE(TAG, "Calendar/BCD self-test FAILED");
        return;
    }
    ESP_LOGI(TAG, "Calendar/BCD self-test PASS");

    bool display_ready = false;
    esp_err_t err = init_i2c();
    if (err == ESP_OK) {
        err = add_device(0x68, &s_rtc);
        if (err != ESP_OK) ESP_LOGE(TAG, "DS1307 init failed: %s", esp_err_to_name(err));
        display_ready = init_display();
    } else {
        ESP_LOGE(TAG, "I2C unavailable; SPI readings continue in Serial");
    }

    err = uart_is_driver_installed(CONSOLE_UART) ? ESP_OK
          : uart_driver_install(CONSOLE_UART, 256, 0, 0, NULL, 0);
    s_console_ready = err == ESP_OK;
    if (!s_console_ready) ESP_LOGE(TAG, "TIME command unavailable: %s", esp_err_to_name(err));
    ESP_LOGI(TAG, "Set RTC: TIME YYYY-MM-DD HH:MM:SS followed by Enter");
    ESP_LOGI(TAG, "RTC time is preserved on startup; EEPROM is not modified");

    esp_err_t sensor_result = bme280_spi_init();
    bool sensor_ready = sensor_result == ESP_OK;
    if (!sensor_ready) ESP_LOGE(TAG, "BME280 init failed: %s", esp_err_to_name(sensor_result));
    int64_t next_sensor_retry = esp_timer_get_time() + SENSOR_RETRY_US;
    /* First normal-mode conversion completes before the first 500 ms update. */
    int64_t next_update = esp_timer_get_time() + UPDATE_PERIOD_US;
    int64_t last_good_sample = 0;
    TickType_t wake = xTaskGetTickCount();

    while (1) {
        poll_console();
        int64_t now = esp_timer_get_time();
        if (!sensor_ready && now >= next_sensor_retry) {
            sensor_result = bme280_spi_init();
            sensor_ready = sensor_result == ESP_OK;
            now = esp_timer_get_time();
            next_sensor_retry = now + SENSOR_RETRY_US;
            if (sensor_ready) {
                /* Give the first conversion time without blocking the loop. */
                next_update = now + UPDATE_PERIOD_US;
                ESP_LOGI(TAG, "BME280 recovered");
            } else {
                ESP_LOGW(TAG, "BME280 retry failed: %s", esp_err_to_name(sensor_result));
            }
        }
        if (now >= next_update) {
            next_update += UPDATE_PERIOD_US;
            if (next_update <= now) next_update = now + UPDATE_PERIOD_US;

            weather_data_t weather;
            if (sensor_ready) {
                sensor_result = bme280_spi_read(&weather);
                if (sensor_result != ESP_OK) {
                    sensor_ready = false;
                    next_sensor_retry = esp_timer_get_time() + SENSOR_RETRY_US;
                    ESP_LOGW(TAG, "BME280 read failed: %s; retry in 5 s", esp_err_to_name(sensor_result));
                } else {
                    const int64_t sampled_at = esp_timer_get_time();
                    if (last_good_sample && sampled_at - last_good_sample > 1000000)
                        ESP_LOGW(TAG, "BME280 sampling gap: %lld ms",
                                 (long long)((sampled_at - last_good_sample) / 1000));
                    last_good_sample = sampled_at;
                }
            }

            rtc_time_t time = {0};
            const esp_err_t rtc_result = read_rtc(&time);
            const char *rtc_status = rtc_result == ESP_OK ? "RTC STOPPED" : "RTC READ ERROR";
            if (rtc_result == ESP_ERR_INVALID_RESPONSE) rtc_status = "RTC TIME INVALID";
            if (rtc_result == ESP_ERR_NOT_FOUND) rtc_status = "RTC NOT FOUND";
            screen_data_t screen;
            screen_format(&screen, rtc_result == ESP_OK ? &time : NULL, rtc_status,
                          sensor_ready ? &weather : NULL);
            if (display_ready) {
                err = display_update(&screen);
                if (err != ESP_OK) ESP_LOGE(TAG, "OLED update failed: %s", esp_err_to_name(err));
            }
            /* Exactly the strings sent to the OLED, including error states. */
            ESP_LOGI(TAG, "%s | %s | %s | %s", screen.clock, screen.date,
                     screen.climate, screen.pressure);
            if (rtc_result != ESP_OK)
                ESP_LOGW(TAG, "DS1307 read failed: %s", esp_err_to_name(rtc_result));
        }
        /* Yield to FreeRTOS. Sampling is scheduled by monotonic timestamps,
         * independently of RTC time changes; no measurement-wait loop. */
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
    }
}
