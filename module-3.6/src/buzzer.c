#include "buzzer.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define BUZZER_GPIO GPIO_NUM_4

#define AUDIO_TICK_MS 50U
#define AUDIO_TICK_US (AUDIO_TICK_MS * 1000U)

#define BUZZER_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER      LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL    LEDC_CHANNEL_0
#define BUZZER_LEDC_RESOLUTION LEDC_TIMER_10_BIT
#define BUZZER_DUTY            512U

typedef struct
{
    uint32_t frequency_hz;
    uint32_t duration_ms;
} note_t;

static const note_t success_melody[] = {
    {523, 100},
    {659, 100},
    {784, 200},
};

static const note_t alarm_melody[] = {
    {880, 200},
    {0,   100},
    {880, 200},
    {0,   100},
    {659, 300},
    {0,   100},
    {880, 400},
};

static portMUX_TYPE buzzer_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t audio_timer;
static const note_t *current_melody;
static size_t current_melody_length;
static size_t current_note_index;
static uint32_t ticks_left;
static bool is_playing;

static void buzzer_stop_tone(void)
{
    (void)ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    (void)ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

static void buzzer_set_frequency(uint32_t frequency_hz)
{
    if (frequency_hz == 0U)
    {
        buzzer_stop_tone();
        return;
    }

    (void)ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, frequency_hz);
    (void)ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY);
    (void)ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

static void buzzer_load_current_note(void)
{
    const note_t *note = &current_melody[current_note_index];

    buzzer_set_frequency(note->frequency_hz);
    ticks_left = (note->duration_ms + AUDIO_TICK_MS - 1U) / AUDIO_TICK_MS;

    if (ticks_left == 0U)
    {
        ticks_left = 1U;
    }
}

static void buzzer_play(const note_t *melody, size_t length)
{
    if ((melody == NULL) || (length == 0U))
    {
        return;
    }

    portENTER_CRITICAL(&buzzer_lock);
    current_melody = melody;
    current_melody_length = length;
    current_note_index = 0U;
    is_playing = true;
    buzzer_load_current_note();
    portEXIT_CRITICAL(&buzzer_lock);
}

static void audio_tick(void *arg)
{
    (void)arg;

    portENTER_CRITICAL(&buzzer_lock);

    if (!is_playing)
    {
        portEXIT_CRITICAL(&buzzer_lock);
        return;
    }

    if (ticks_left > 0U)
    {
        --ticks_left;
    }

    if (ticks_left == 0U)
    {
        ++current_note_index;

        if (current_note_index >= current_melody_length)
        {
            is_playing = false;
            buzzer_stop_tone();
        }
        else
        {
            buzzer_load_current_note();
        }
    }

    portEXIT_CRITICAL(&buzzer_lock);
}

void buzzer_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_LEDC_RESOLUTION,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    const ledc_channel_config_t channel_config = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    const esp_timer_create_args_t timer_args = {
        .callback = audio_tick,
        .arg = NULL,
        .name = "audio_tick",
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &audio_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(audio_timer, AUDIO_TICK_US));
}

void buzzer_play_success(void)
{
    buzzer_play(success_melody, sizeof(success_melody) / sizeof(success_melody[0]));
}

void buzzer_play_alarm(void)
{
    buzzer_play(alarm_melody, sizeof(alarm_melody) / sizeof(alarm_melody[0]));
}

bool buzzer_is_playing(void)
{
    bool playing;

    portENTER_CRITICAL(&buzzer_lock);
    playing = is_playing;
    portEXIT_CRITICAL(&buzzer_lock);

    return playing;
}
