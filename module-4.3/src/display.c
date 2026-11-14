#include "display.h"

#include <stdio.h>
#include <string.h>
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint8_t s_frame[128 * 64 / 8];

/* Five-bit rows, top to bottom. A small local font keeps this test independent
 * of a graphics library. Lowercase glyphs preserve "Sat 14.02.2026". */
typedef struct { char c; uint8_t rows[7]; } glyph_t;
static const glyph_t FONT[] = {
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{6,8,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,2,12}},
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,2,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'a',{0,0,14,1,15,17,15}}, {'b',{16,16,30,17,17,17,30}},
    {'c',{0,0,14,17,16,17,14}}, {'d',{1,1,15,17,17,17,15}},
    {'e',{0,0,14,17,31,16,14}}, {'f',{6,8,8,28,8,8,8}},
    {'g',{0,15,17,17,15,1,14}}, {'h',{16,16,30,17,17,17,17}},
    {'i',{4,0,12,4,4,4,14}}, {'j',{2,0,6,2,2,18,12}},
    {'k',{16,16,18,20,24,20,18}}, {'l',{12,4,4,4,4,4,14}},
    {'m',{0,0,26,21,21,21,21}}, {'n',{0,0,30,17,17,17,17}},
    {'o',{0,0,14,17,17,17,14}}, {'p',{0,30,17,17,30,16,16}},
    {'q',{0,15,17,17,15,1,1}}, {'r',{0,0,22,25,16,16,16}},
    {'s',{0,0,15,16,14,1,30}}, {'t',{8,8,28,8,8,9,6}},
    {'u',{0,0,17,17,17,19,13}}, {'v',{0,0,17,17,17,10,4}},
    {'w',{0,0,17,17,21,21,10}}, {'x',{0,0,17,10,4,10,17}},
    {'y',{0,17,17,17,15,1,14}}, {'z',{0,0,31,2,4,8,31}},
    {':',{0,4,4,0,4,4,0}}, {'.',{0,0,0,0,0,6,6}},
    {'-',{0,0,0,31,0,0,0}}, {'+',{0,4,4,31,4,4,0}},
    {'/',{1,2,2,4,8,8,16}}, {'?',{14,17,1,2,4,0,4}},
};

static void pixel(int x, int y)
{
    if (x >= 0 && x < 128 && y >= 0 && y < 64)
        s_frame[x + (y / 8) * 128] |= (uint8_t)(1u << (y % 8));
}

static void text(int x, int y, const char *str, int scale)
{
    for (; *str; ++str, x += 6 * scale) {
        const uint8_t *rows = NULL;
        for (unsigned i = 0; i < sizeof FONT / sizeof FONT[0]; ++i)
            if (FONT[i].c == *str) { rows = FONT[i].rows; break; }
        if (!rows) continue;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (rows[row] & (1u << (4 - col)))
                    for (int dy = 0; dy < scale; ++dy)
                        for (int dx = 0; dx < scale; ++dx)
                            pixel(x + col * scale + dx, y + row * scale + dy);
    }
}

esp_err_t display_init(i2c_master_bus_handle_t bus, uint8_t address)
{
    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = address, .scl_speed_hz = 100000,
        .control_phase_bytes = 1, .dc_bit_offset = 6,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .transaction_timeout_ms = 200,
    };
    esp_err_t err = esp_lcd_new_panel_io_i2c(bus, &io_config, &s_io);
    if (err != ESP_OK) return err;
    const esp_lcd_panel_ssd1306_config_t vendor = {.height = 64, .contrast = 0x7f};
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, .bits_per_pixel = 1, .vendor_config = (void *)&vendor,
    };
    err = esp_lcd_new_panel_ssd1306(s_io, &panel_config, &s_panel);
    if (err == ESP_OK) err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(s_panel);
    if (err == ESP_OK) err = esp_lcd_panel_mirror(s_panel, true, true);
    /* Clear RAM before enabling the panel, avoiding a flash of old/random pixels. */
    memset(s_frame, 0, sizeof s_frame);
    if (err == ESP_OK) err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, 128, 64, s_frame);
    if (err == ESP_OK) err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) {
        if (s_panel) { esp_lcd_panel_del(s_panel); s_panel = NULL; }
        esp_lcd_panel_io_del(s_io); s_io = NULL;
    }
    return err;
}

esp_err_t display_update(const rtc_time_t *time, const char *rtc_status,
                         bool weather_ok, double temperature, double pressure)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    memset(s_frame, 0, sizeof s_frame);
    char line[40];
    if (time && time->running) {
        snprintf(line, sizeof line, "%02d:%02d", time->hour, time->minute);
        text(1, 2, line, 3);
        snprintf(line, sizeof line, ":%02d", time->second);
        text(91, 9, line, 2);
        snprintf(line, sizeof line, "%s %02d.%02d.%04d", rtc_weekday_name(time),
                 time->day, time->month, time->year);
        text(22, 29, line, 1);
        for (int x = 1; x <= time->second * 126 / 59; ++x) {
            pixel(x, 62); pixel(x, 63);
        }
    } else {
        text(1, 4, rtc_status, 1);
        text(1, 18, "SET TIME VIA SERIAL", 1);
    }
    if (weather_ok) {
        snprintf(line, sizeof line, "T: %.1f C", temperature);
        text(1, 41, line, 1);
        snprintf(line, sizeof line, "P: %.1f hPa", pressure);
        text(1, 52, line, 1);
    } else {
        text(1, 41, "SENSOR ERROR", 1);
        text(1, 52, "CHECK SERIAL LOG", 1);
    }
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, 128, 64, s_frame);
}
