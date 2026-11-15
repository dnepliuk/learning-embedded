#pragma once

#include "clock_logic.h"
#include "weather_data.h"

/* UTF-8 degree symbol: the OLED renderer maps it to one glyph. */
#define DEGREE_C "\xC2\xB0" "C"

typedef struct {
    bool clock_running;
    int second;
    char clock[24];
    char date[24];
    char climate[32];
    char pressure[24];
} screen_data_t;

/* NULL weather means unavailable. No previous values are reused. */
void screen_format(screen_data_t *screen, const rtc_time_t *time,
                   const char *rtc_status, const weather_data_t *weather);
