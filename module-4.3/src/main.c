#include <stdio.h>
#include <string.h>

#include "clock_logic.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SDA_PIN GPIO_NUM_9
#define SCL_PIN GPIO_NUM_8
#define I2C_TIMEOUT_MS 100
#define UPDATE_PERIOD_MS 500
#define CONSOLE_UART UART_NUM_0 /* COM6 is the CH343 UART bridge. */

static const char *TAG = "module_test";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_rtc;
static i2c_master_dev_handle_t s_sensor;
static weather_calibration_t s_calibration;
static const char *s_sensor_name = "unknown";
static bool s_console_ready;

static esp_err_t read_registers(i2c_master_dev_handle_t dev, uint8_t reg,
                                uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, length, I2C_TIMEOUT_MS);
}

static esp_err_t write_register(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(dev, data, sizeof data, I2C_TIMEOUT_MS);
}

static esp_err_t add_device(uint8_t address, i2c_master_dev_handle_t *dev)
{
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_bus, address, I2C_TIMEOUT_MS), TAG,
                        "No response at 0x%02X", (unsigned)address);
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(s_bus, &config, dev);
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
    uint8_t registers[7];
    /* A single burst uses the DS1307 snapshot, including at second rollover. */
    esp_err_t err = read_registers(s_rtc, 0x00, registers, sizeof registers);
    if (err != ESP_OK) return err;
    return rtc_decode(registers, time) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t set_rtc(const rtc_time_t *time)
{
    if (!s_rtc) return ESP_ERR_NOT_FOUND;
    uint8_t message[8] = {0};
    if (!rtc_encode(time, message + 1)) return ESP_ERR_INVALID_ARG;
    /* Only this explicit TIME command writes the RTC. EEPROM is never written. */
    return i2c_master_transmit(s_rtc, message, sizeof message, I2C_TIMEOUT_MS);
}

static esp_err_t sensor_wait_ready(uint8_t mask)
{
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        uint8_t status;
        esp_err_t err = read_registers(s_sensor, 0xf3, &status, 1);
        if (err != ESP_OK) return err;
        if (!(status & mask)) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t init_sensor(void)
{
    uint8_t address = 0x76;
    if (i2c_master_probe(s_bus, address, I2C_TIMEOUT_MS) != ESP_OK) address = 0x77;
    ESP_RETURN_ON_ERROR(add_device(address, &s_sensor), TAG, "Sensor not found");
    uint8_t id;
    ESP_RETURN_ON_ERROR(read_registers(s_sensor, 0xd0, &id, 1), TAG, "Sensor ID read failed");
    ESP_LOGI(TAG, "HW-611 at 0x%02X: chip ID=0x%02X", (unsigned)address, (unsigned)id);
    if (id != 0x58 && id != 0x60) {
        ESP_LOGE(TAG, "Unsupported chip ID; expected BMP280=0x58 or BME280=0x60");
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_sensor_name = id == 0x58 ? "BMP280" : "BME280";
    ESP_RETURN_ON_ERROR(write_register(s_sensor, 0xe0, 0xb6), TAG, "Sensor reset failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(sensor_wait_ready(0x01), TAG, "Calibration copy timeout");
    uint8_t calibration[24];
    ESP_RETURN_ON_ERROR(read_registers(s_sensor, 0x88, calibration, sizeof calibration),
                        TAG, "Calibration read failed");
    weather_decode_calibration(calibration, &s_calibration);
    if (!s_calibration.t1 || !s_calibration.p1 || s_calibration.t1 == 0xffff ||
        s_calibration.p1 == 0xffff) return ESP_ERR_INVALID_RESPONSE;
    if (id == 0x60) {
        /* This test reads temperature and pressure only; humidity is disabled. */
        ESP_RETURN_ON_ERROR(write_register(s_sensor, 0xf2, 0), TAG, "Humidity config failed");
    }
    ESP_RETURN_ON_ERROR(write_register(s_sensor, 0xf5, 0), TAG, "Sensor config failed");
    ESP_LOGI(TAG, "%s ready: temperature + pressure, forced measurements", s_sensor_name);
    return ESP_OK;
}

static esp_err_t read_weather(double *temperature, double *pressure)
{
    /* Temperature x1, pressure x1, forced mode: one fresh measurement per update. */
    esp_err_t err = write_register(s_sensor, 0xf4, 0x25);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    err = sensor_wait_ready(0x09);
    if (err != ESP_OK) return err;
    uint8_t data[6];
    err = read_registers(s_sensor, 0xf7, data, sizeof data);
    if (err != ESP_OK) return err;
    const uint32_t raw_p = ((uint32_t)data[0] << 12) | ((uint32_t)data[1] << 4) | (data[2] >> 4);
    const uint32_t raw_t = ((uint32_t)data[3] << 12) | ((uint32_t)data[4] << 4) | (data[5] >> 4);
    return weather_compensate(&s_calibration, raw_t, raw_p, temperature, pressure)
               ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
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
        ESP_LOGI(TAG, "RTC set to %s; oscillator started. Time is preserved on reboot.", line + 5);
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

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "Module 4.3 test: SDA=GPIO9 SCL=GPIO8, all devices at 100 kHz");
    if (!logic_self_test()) {
        ESP_LOGE(TAG, "Calendar/BCD/sensor-compensation self-test FAILED");
        return;
    }
    ESP_LOGI(TAG, "Calendar, BCD and sensor-compensation self-tests PASS");
    if (init_i2c() != ESP_OK) return;

    esp_err_t err = add_device(0x68, &s_rtc);
    if (err != ESP_OK) ESP_LOGE(TAG, "DS1307 init failed: %s", esp_err_to_name(err));
    const bool sensor_ready = init_sensor() == ESP_OK;

    uint8_t oled_address = 0x3c;
    if (i2c_master_probe(s_bus, oled_address, I2C_TIMEOUT_MS) != ESP_OK) oled_address = 0x3d;
    err = i2c_master_probe(s_bus, oled_address, I2C_TIMEOUT_MS);
    if (err == ESP_OK) err = display_init(s_bus, oled_address);
    const bool display_ready = err == ESP_OK;
    if (display_ready) ESP_LOGI(TAG, "SSD1306 128x64 initialized at 0x%02X", (unsigned)oled_address);
    else ESP_LOGE(TAG, "OLED init failed: %s; readings continue in Serial", esp_err_to_name(err));

    err = uart_is_driver_installed(CONSOLE_UART) ? ESP_OK
          : uart_driver_install(CONSOLE_UART, 256, 0, 0, NULL, 0);
    s_console_ready = err == ESP_OK;
    if (!s_console_ready) ESP_LOGE(TAG, "TIME command unavailable: %s", esp_err_to_name(err));
    ESP_LOGI(TAG, "Set RTC once by sending: TIME YYYY-MM-DD HH:MM:SS followed by Enter");
    ESP_LOGI(TAG, "RTC time is NOT overwritten at startup. EEPROM is not modified.");

    TickType_t wake = xTaskGetTickCount();
    unsigned update = 0;
    for (;;) {
        poll_console();
        rtc_time_t time = {0};
        const esp_err_t rtc_result = read_rtc(&time);
        const char *rtc_status = rtc_result != ESP_OK ? "RTC READ ERROR" : "RTC STOPPED";
        if (rtc_result == ESP_ERR_INVALID_RESPONSE) rtc_status = "RTC TIME INVALID";
        if (rtc_result == ESP_ERR_NOT_FOUND) rtc_status = "RTC NOT FOUND";
        double temperature = 0, pressure = 0;
        const esp_err_t weather_result = sensor_ready ? read_weather(&temperature, &pressure)
                                                     : ESP_ERR_INVALID_STATE;
        if (display_ready) {
            err = display_update(rtc_result == ESP_OK ? &time : NULL, rtc_status,
                                 weather_result == ESP_OK, temperature, pressure);
            if (err != ESP_OK) ESP_LOGE(TAG, "OLED update failed: %s", esp_err_to_name(err));
        }
        if (update++ % 2 == 0) {
            if (rtc_result == ESP_OK && time.running)
                ESP_LOGI(TAG, "RTC %02d:%02d:%02d | %s %02d.%02d.%04d", time.hour,
                         time.minute, time.second, rtc_weekday_name(&time),
                         time.day, time.month, time.year);
            else ESP_LOGW(TAG, "%s; send TIME command if needed (%s)", rtc_status,
                          esp_err_to_name(rtc_result));
            if (weather_result == ESP_OK)
                ESP_LOGI(TAG, "%s: %.2f C | %.2f hPa", s_sensor_name, temperature, pressure);
            else ESP_LOGW(TAG, "Sensor read failed: %s", esp_err_to_name(weather_result));
        }
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(UPDATE_PERIOD_MS));
    }
}
