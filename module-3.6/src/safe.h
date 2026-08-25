#pragma once

#include "encoder.h"

typedef enum
{
    SAFE_STATE_INPUT,
    SAFE_STATE_OPEN,
    SAFE_STATE_LOCKED
} safe_state_t;

void safe_init(void);
void safe_process_event(encoder_event_t event);
