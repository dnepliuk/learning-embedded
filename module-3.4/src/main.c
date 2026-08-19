#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "esp_timer.h"
#include "esp_err.h"

#define BUZZER_GPIO GPIO_NUM_4

#define AUDIO_TICK_MS 50
#define AUDIO_TICK_US (AUDIO_TICK_MS * 1000)

#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_RESOLUTION LEDC_TIMER_10_BIT

#define BUZZER_DUTY 512

typedef struct
{
    uint32_t frequency_hz;
    uint32_t duration_ms;
} note_t;

static const note_t melody[] =
    {
        {262, 500}, // C4
        {294, 500}, // D4
        {330, 500}, // E4

        {0, 200}, // pause

        {349, 500}, // F4
        {392, 500}, // G4

        {0, 200}, // pause

        {440, 1000}, // A4
};

#define MELODY_LENGTH \
    (sizeof(melody) / sizeof(melody[0]))

static const note_t *current_melody = NULL;

static size_t melody_length = 0;
static size_t current_note = 0;

static uint32_t ticks_left = 0;

static bool is_playing = false;

static void buzzer_stop(void)
{
    ledc_set_duty(
        LEDC_MODE,
        LEDC_CHANNEL,
        0);

    ledc_update_duty(
        LEDC_MODE,
        LEDC_CHANNEL);
}

static void buzzer_set_frequency(uint32_t frequency)
{
    if (frequency == 0)
    {
        buzzer_stop();
        return;
    }

    ledc_set_freq(
        LEDC_MODE,
        LEDC_TIMER,
        frequency);

    ledc_set_duty(
        LEDC_MODE,
        LEDC_CHANNEL,
        BUZZER_DUTY);

    ledc_update_duty(
        LEDC_MODE,
        LEDC_CHANNEL);
}

// LOAD NOTE

static void audio_load_note(void)
{
    const note_t *note =
        &current_melody[current_note];

    buzzer_set_frequency(
        note->frequency_hz);

    ticks_left =
        (note->duration_ms + AUDIO_TICK_MS - 1) / AUDIO_TICK_MS;

    if (ticks_left == 0)
    {
        ticks_left = 1;
    }
}

// PLAY

static void audio_play(
    const note_t *melody_data,
    size_t length)
{
    if (melody_data == NULL || length == 0)
    {
        return;
    }

    current_melody = melody_data;
    melody_length = length;

    current_note = 0;

    is_playing = true;

    audio_load_note();
}

// STOP

static void audio_stop(void)
{
    is_playing = false;

    current_note = 0;
    ticks_left = 0;

    buzzer_stop();
}

// AUDIO TICK

static void audio_tick(void *arg)
{
    if (!is_playing)
    {
        return;
    }

    if (ticks_left > 0)
    {
        ticks_left--;
    }

    if (ticks_left > 0)
    {
        return;
    }

    current_note++;

    if (current_note >= melody_length)
    {
        current_note = 0;
    }

    audio_load_note();
}

static void buzzer_init(void)
{
    ledc_timer_config_t timer_config =
        {
            .speed_mode = LEDC_MODE,
            .duty_resolution = LEDC_RESOLUTION,
            .timer_num = LEDC_TIMER,

            .freq_hz = 1000,

            .clk_cfg = LEDC_AUTO_CLK};

    ESP_ERROR_CHECK(
        ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config =
        {
            .gpio_num = BUZZER_GPIO,

            .speed_mode = LEDC_MODE,
            .channel = LEDC_CHANNEL,

            .intr_type = LEDC_INTR_DISABLE,

            .timer_sel = LEDC_TIMER,

            .duty = 0,
            .hpoint = 0};

    ESP_ERROR_CHECK(
        ledc_channel_config(&channel_config));
}

// AUDIO TIMER INIT

static void audio_timer_init(void)
{
    static esp_timer_handle_t audio_timer;

    esp_timer_create_args_t timer_args =
        {
            .callback = audio_tick,
            .arg = NULL,
            .name = "audio_tick"};

    ESP_ERROR_CHECK(
        esp_timer_create(
            &timer_args,
            &audio_timer));

    ESP_ERROR_CHECK(
        esp_timer_start_periodic(
            audio_timer,
            AUDIO_TICK_US));
}

void app_main(void)
{
    buzzer_init();

    audio_timer_init();

    audio_play(
        melody,
        MELODY_LENGTH);

    printf("Audio player started\n");
}