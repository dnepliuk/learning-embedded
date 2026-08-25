#include "encoder.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"

#define ENCODER_CLK GPIO_NUM_5
#define ENCODER_DT  GPIO_NUM_6
#define ENCODER_SW  GPIO_NUM_7

#define BUTTON_DEBOUNCE_US 40000
#define QUADRATURE_TRANSITIONS_PER_STEP 4

/* Change to 1 if clockwise and counterclockwise are reversed on the hardware. */
#define ENCODER_INVERT_DIRECTION 0

static uint8_t previous_ab;
static int8_t quadrature_accumulator;

static bool button_candidate_pressed;
static bool button_stable_pressed;
static int64_t button_candidate_since_us;

/*
 * Index: previous AB in bits 3..2, current AB in bits 1..0.
 * Invalid two-bit changes are ignored. Contact bounce reverses the partial
 * accumulator instead of producing another complete encoder step.
 */
static const int8_t quadrature_transition[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

static uint8_t encoder_read_ab(void)
{
    const uint8_t clk = (uint8_t)gpio_get_level(ENCODER_CLK);
    const uint8_t dt = (uint8_t)gpio_get_level(ENCODER_DT);

    return (uint8_t)((clk << 1U) | dt);
}

static encoder_event_t encoder_read_rotation(void)
{
    const uint8_t current_ab = encoder_read_ab();
    const uint8_t transition_index = (uint8_t)((previous_ab << 2U) | current_ab);
    const int8_t movement = quadrature_transition[transition_index];

    previous_ab = current_ab;

    if ((movement == 0) && ((transition_index >> 2U) != current_ab))
    {
        quadrature_accumulator = 0;
        return ENCODER_EVENT_NONE;
    }

    quadrature_accumulator = (int8_t)(quadrature_accumulator + movement);

    if (quadrature_accumulator >= QUADRATURE_TRANSITIONS_PER_STEP)
    {
        quadrature_accumulator = 0;
        return ENCODER_INVERT_DIRECTION
                   ? ENCODER_EVENT_COUNTERCLOCKWISE
                   : ENCODER_EVENT_CLOCKWISE;
    }

    if (quadrature_accumulator <= -QUADRATURE_TRANSITIONS_PER_STEP)
    {
        quadrature_accumulator = 0;
        return ENCODER_INVERT_DIRECTION
                   ? ENCODER_EVENT_CLOCKWISE
                   : ENCODER_EVENT_COUNTERCLOCKWISE;
    }

    return ENCODER_EVENT_NONE;
}

static encoder_event_t encoder_read_button(void)
{
    const bool raw_pressed = gpio_get_level(ENCODER_SW) == 0;
    const int64_t now_us = esp_timer_get_time();

    if (raw_pressed != button_candidate_pressed)
    {
        button_candidate_pressed = raw_pressed;
        button_candidate_since_us = now_us;
    }

    if ((button_candidate_pressed != button_stable_pressed) &&
        ((now_us - button_candidate_since_us) >= BUTTON_DEBOUNCE_US))
    {
        button_stable_pressed = button_candidate_pressed;

        if (button_stable_pressed)
        {
            return ENCODER_EVENT_BUTTON;
        }
    }

    return ENCODER_EVENT_NONE;
}

void encoder_init(void)
{
    const gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << ENCODER_CLK) |
                        (1ULL << ENCODER_DT) |
                        (1ULL << ENCODER_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&input_config));

    previous_ab = encoder_read_ab();
    quadrature_accumulator = 0;

    button_candidate_pressed = gpio_get_level(ENCODER_SW) == 0;
    button_stable_pressed = button_candidate_pressed;
    button_candidate_since_us = esp_timer_get_time();
}

encoder_event_t encoder_get_event(void)
{
    const encoder_event_t rotation_event = encoder_read_rotation();
    const encoder_event_t button_event = encoder_read_button();

    if (button_event != ENCODER_EVENT_NONE)
    {
        return button_event;
    }

    return rotation_event;
}
