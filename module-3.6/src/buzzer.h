#pragma once

#include <stdbool.h>

void buzzer_init(void);
void buzzer_play_success(void);
void buzzer_play_alarm(void);
bool buzzer_is_playing(void);
