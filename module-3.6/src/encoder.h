#pragma once

typedef enum
{
    ENCODER_EVENT_NONE,
    ENCODER_EVENT_CLOCKWISE,
    ENCODER_EVENT_COUNTERCLOCKWISE,
    ENCODER_EVENT_BUTTON
} encoder_event_t;

void encoder_init(void);
encoder_event_t encoder_get_event(void);
