#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#define LINK_UART       UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_17
#define UART_RX_PIN     GPIO_NUM_16
#define LED_PIN         GPIO_NUM_5
#define ENCODER_SW_PIN  GPIO_NUM_4

#define DEBOUNCE_MS     25U
#define REFRESH_MS      1000U

static const char *TAG = "UART_LINK";
static QueueHandle_t uart_events;

static bool stm_led_state = false;
static bool tx_pending = true;
static uint32_t last_tx_at = 0;

static int button_raw;
static int button_stable;
static uint32_t button_changed_at;

static char rx_line[16];
static size_t rx_length = 0;
static bool rx_discard = false;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Отримана команда керує LED на ESP32. */
static void process_byte(uint8_t byte)
{
    if (byte == '\n' || byte == '\r') {
        if (!rx_discard && rx_length == 5) {
            bool turn_on = memcmp(rx_line, "LED:1", 5) == 0;
            bool turn_off = memcmp(rx_line, "LED:0", 5) == 0;

            if (turn_on || turn_off) {
                ESP_ERROR_CHECK(gpio_set_level(LED_PIN, turn_on));
                ESP_LOGI(TAG, "RX: LED:%d -> ESP32 LED", turn_on);
            }
        }

        rx_length = 0;
        rx_discard = false;
        return;
    }

    if (rx_discard)
        return;

    /* Некоректний рядок пропускаємо до кінця. */
    if (byte < 32 || byte > 126 ||
        rx_length >= sizeof(rx_line)) {
        rx_length = 0;
        rx_discard = true;
        return;
    }

    rx_line[rx_length++] = (char)byte;
}

static void receive_poll(void)
{
    uart_event_t event;

    /* Відновлення після переповнення або помилки UART. */
    while (xQueueReceive(uart_events, &event, 0) == pdTRUE) {
        if (event.type == UART_FIFO_OVF ||
            event.type == UART_BUFFER_FULL ||
            event.type == UART_FRAME_ERR ||
            event.type == UART_PARITY_ERR ||
            event.type == UART_BREAK) {

            ESP_LOGW(TAG, "UART RX error: %d", (int)event.type);

            ESP_ERROR_CHECK(uart_flush_input(LINK_UART));
            xQueueReset(uart_events);

            rx_length = 0;
            rx_discard = true;
        }
    }

    uint8_t buffer[128];
    int count = uart_read_bytes(
        LINK_UART, buffer, sizeof(buffer), 0);

    if (count < 0) {
        ESP_LOGE(TAG, "UART read failed");
        return;
    }

    for (int i = 0; i < count; i++)
        process_byte(buffer[i]);
}

/* Натискання SW перемикає бажаний стан LED на STM32. */
static void button_poll(void)
{
    uint32_t now = now_ms();
    int level = gpio_get_level(ENCODER_SW_PIN);

    if (level != button_raw) {
        button_raw = level;
        button_changed_at = now;
    }

    if ((uint32_t)(now - button_changed_at) >= DEBOUNCE_MS &&
        button_stable != button_raw) {

        button_stable = button_raw;

        if (button_stable == 0) {
            stm_led_state = !stm_led_state;
            tx_pending = true;

            ESP_LOGI(TAG, "SW pressed");
        }
    }
}

static void send_led_state(void)
{
    /* Початковий перенос завершує можливий неповний рядок. */
    const char *message =
        stm_led_state ? "\nLED:1\n" : "\nLED:0\n";

    int length = (int)strlen(message);
    int sent = uart_write_bytes(LINK_UART, message, length);

    if (sent == length) {
        ESP_LOGI(TAG, "TX: LED:%d -> STM32", stm_led_state);
    } else {
        ESP_LOGE(TAG, "UART write failed");
    }

    last_tx_at = now_ms();
    tx_pending = false;
}

static void io_init(void)
{
    const gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&led_config));
    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));

    const gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << ENCODER_SW_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&button_config));

    button_raw = gpio_get_level(ENCODER_SW_PIN);
    button_stable = button_raw;
    button_changed_at = now_ms();
}

static void uart_link_init(void)
{
    const uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(
        LINK_UART, 1024, 1024, 16, &uart_events, 0));

    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &config));

    ESP_ERROR_CHECK(uart_set_pin(
        LINK_UART,
        UART_TX_PIN,
        UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(
        gpio_set_pull_mode(UART_RX_PIN, GPIO_PULLUP_ONLY));

    ESP_ERROR_CHECK(uart_flush_input(LINK_UART));
}

void app_main(void)
{
    io_init();
    uart_link_init();

    ESP_LOGI(TAG, "Started: TX=17 RX=16 LED=5 SW=4, 115200 8N1");

    while (1) {
        receive_poll();
        button_poll();

        if (tx_pending ||
            (uint32_t)(now_ms() - last_tx_at) >= REFRESH_MS) {
            send_led_state();
        }

        vTaskDelay(1);
    }
}