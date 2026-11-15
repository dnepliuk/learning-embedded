#include "bme280_spi.h"

#include <math.h>
#include "bme280.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SENSOR_HOST SPI2_HOST
#define SENSOR_CS 10
#define SENSOR_MOSI 11
#define SENSOR_SCK 12
#define SENSOR_MISO 13
#define SENSOR_SPI_HZ 1000000
#define EXPECTED_CTRL_HUM 0x01  /* Humidity x1. */
#define EXPECTED_CTRL_MEAS 0x27 /* Temperature x1, pressure x1, normal mode. */
#define EXPECTED_CONFIG 0x60    /* Standby 250 ms, filter off, 4-wire SPI. */

static const char *TAG = "bme280_spi";
static spi_device_handle_t s_spi;
static struct bme280_dev s_sensor;
static esp_err_t s_io_error;
static bool s_ready;

static BME280_INTF_RET_TYPE spi_read(uint8_t reg, uint8_t *data,
                                    uint32_t length, void *context)
{
    spi_transaction_t transaction = {
        .cmd = reg | 0x80,
        .length = length * 8,
        .rxlength = length * 8,
        .rx_buffer = data,
    };
    /* The 8-bit command and data share a single, hardware-controlled CS pulse.
     * No extra dummy byte: the first received byte is the register value. */
    s_io_error = spi_device_transmit(*(spi_device_handle_t *)context, &transaction);
    return s_io_error == ESP_OK ? BME280_INTF_RET_SUCCESS : -1;
}

static BME280_INTF_RET_TYPE spi_write(uint8_t reg, const uint8_t *data,
                                     uint32_t length, void *context)
{
    spi_transaction_t transaction = {
        .cmd = reg & 0x7f,
        .length = length * 8,
        .tx_buffer = data,
    };
    /* Bosch already interleaves register addresses for multi-register writes. */
    s_io_error = spi_device_transmit(*(spi_device_handle_t *)context, &transaction);
    return s_io_error == ESP_OK ? BME280_INTF_RET_SUCCESS : -1;
}

static void startup_wait(uint32_t period_us, void *context)
{
    (void)context;
    /* Only used during initialization/reset, never to wait for a measurement.
     * The extra tick ensures the requested minimum even just before a tick. */
    const uint32_t tick_us = portTICK_PERIOD_MS * 1000;
    vTaskDelay((period_us + tick_us - 1) / tick_us + 1);
}

static esp_err_t api_error(int8_t result)
{
    if (result == BME280_OK) return ESP_OK;
    if (result == BME280_E_COMM_FAIL && s_io_error != ESP_OK) return s_io_error;
    if (result == BME280_E_DEV_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (result == BME280_E_NVM_COPY_FAILED) return ESP_ERR_TIMEOUT;
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t check_settings(void)
{
    uint8_t control[4];
    esp_err_t err = api_error(bme280_get_regs(BME280_REG_CTRL_HUM, control,
                                             sizeof control, &s_sensor));
    if (err != ESP_OK) return err;
    /* Detect a reset/disconnection instead of displaying an old sample.
     * Measuring may be 1 in normal mode: shadowed data can still be read. */
    if (control[0] != EXPECTED_CTRL_HUM || control[2] != EXPECTED_CTRL_MEAS ||
        control[3] != EXPECTED_CONFIG || (control[1] & BME280_STATUS_IM_UPDATE))
        return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t bme280_spi_init(void)
{
    s_ready = false;
    if (!s_spi) {
        const spi_bus_config_t bus = {
            .mosi_io_num = SENSOR_MOSI, .miso_io_num = SENSOR_MISO,
            .sclk_io_num = SENSOR_SCK, .quadwp_io_num = -1, .quadhd_io_num = -1,
            .max_transfer_sz = 32,
        };
        esp_err_t err = spi_bus_initialize(SENSOR_HOST, &bus, SPI_DMA_DISABLED);
        if (err != ESP_OK) return err;
        const spi_device_interface_config_t device = {
            .command_bits = 8, .mode = 0, .clock_speed_hz = SENSOR_SPI_HZ,
            .spics_io_num = SENSOR_CS, .queue_size = 1,
        };
        err = spi_bus_add_device(SENSOR_HOST, &device, &s_spi);
        if (err != ESP_OK) {
            spi_bus_free(SENSOR_HOST);
            return err;
        }
    }

    s_sensor = (struct bme280_dev){
        .intf = BME280_SPI_INTF, .intf_ptr = &s_spi,
        .read = spi_read, .write = spi_write, .delay_us = startup_wait,
    };
    uint8_t id = 0;
    esp_err_t err = api_error(bme280_get_regs(BME280_REG_CHIP_ID, &id, 1, &s_sensor));
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "SPI2 CS=%d MOSI=%d SCK=%d MISO=%d: chip ID=0x%02X",
             SENSOR_CS, SENSOR_MOSI, SENSOR_SCK, SENSOR_MISO, (unsigned)id);
    if (id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Expected BME280 ID=0x60; check SPI wiring (BMP280 has no RH)");
        return ESP_ERR_NOT_FOUND;
    }
    err = api_error(bme280_init(&s_sensor));
    if (err != ESP_OK) return err;
    const struct bme280_calib_data *cal = &s_sensor.calib_data;
    if (!cal->dig_t1 || cal->dig_t1 == UINT16_MAX || !cal->dig_p1 ||
        cal->dig_p1 == UINT16_MAX || !cal->dig_h2)
        return ESP_ERR_INVALID_RESPONSE;

    const struct bme280_settings settings = {
        .osr_t = BME280_OVERSAMPLING_1X, .osr_p = BME280_OVERSAMPLING_1X,
        .osr_h = BME280_OVERSAMPLING_1X, .filter = BME280_FILTER_COEFF_OFF,
        .standby_time = BME280_STANDBY_TIME_250_MS,
    };
    err = api_error(bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &s_sensor));
    if (err == ESP_OK)
        err = api_error(bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &s_sensor));
    if (err == ESP_OK) err = check_settings();
    if (err != ESP_OK) return err;
    s_ready = true;
    ESP_LOGI(TAG, "T/RH/P enabled: normal mode, x1, standby 250 ms, SPI 1 MHz");
    return ESP_OK;
}

esp_err_t bme280_spi_read(weather_data_t *weather)
{
    if (!weather) return ESP_ERR_INVALID_ARG;
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    uint8_t id = 0;
    esp_err_t err = api_error(bme280_get_regs(BME280_REG_CHIP_ID, &id, 1, &s_sensor));
    if (err != ESP_OK) return err;
    if (id != BME280_CHIP_ID) return ESP_ERR_NOT_FOUND;
    err = check_settings();
    if (err != ESP_OK) return err;

    /* One burst is essential: all three readings belong to the same sample. */
    uint8_t raw[8];
    err = api_error(bme280_get_regs(BME280_REG_DATA, raw, sizeof raw, &s_sensor));
    if (err != ESP_OK) return err;
    const struct bme280_uncomp_data uncompensated = {
        .pressure = ((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | (raw[2] >> 4),
        .temperature = ((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | (raw[5] >> 4),
        .humidity = ((uint32_t)raw[6] << 8) | raw[7],
    };
    /* Reject reset/skipped-channel and stuck-bus patterns before compensation:
     * Bosch clamps results, so invalid raw bytes could otherwise look valid. */
    if (uncompensated.temperature == 0x80000 || uncompensated.pressure == 0x80000 ||
        uncompensated.humidity == 0x8000 ||
        (!uncompensated.temperature && !uncompensated.pressure && !uncompensated.humidity) ||
        (uncompensated.temperature == 0xfffff && uncompensated.pressure == 0xfffff &&
         uncompensated.humidity == 0xffff))
        return ESP_ERR_INVALID_RESPONSE;

    struct bme280_data data;
    err = api_error(bme280_compensate_data(BME280_ALL, &uncompensated, &data,
                                          &s_sensor.calib_data));
    if (err != ESP_OK) return err;
    if (!isfinite(data.temperature) || !isfinite(data.pressure) || !isfinite(data.humidity))
        return ESP_ERR_INVALID_RESPONSE;
    *weather = (weather_data_t){data.temperature, data.humidity, data.pressure / 100.0};
    return ESP_OK;
}
