#include "screen_data.h"

#include <stdio.h>

void screen_format(screen_data_t *screen, const rtc_time_t *time,
                   const char *rtc_status, const weather_data_t *weather)
{
    *screen = (screen_data_t){0};
    screen->clock_running = time && time->running && rtc_time_valid(time);
    if (screen->clock_running) {
        screen->second = time->second;
        snprintf(screen->clock, sizeof screen->clock, "%02d:%02d:%02d",
                 time->hour, time->minute, time->second);
        snprintf(screen->date, sizeof screen->date, "%s %02d.%02d.%04d",
                 rtc_weekday_name(time), time->day, time->month, time->year);
    } else {
        snprintf(screen->clock, sizeof screen->clock, "%s", rtc_status);
        snprintf(screen->date, sizeof screen->date, "SET TIME VIA SERIAL");
    }
    if (weather) {
        snprintf(screen->climate, sizeof screen->climate, "T:%.1f" DEGREE_C " RH:%.0f%%",
                 weather->temperature_c, weather->humidity_pct);
        snprintf(screen->pressure, sizeof screen->pressure, "P:%.1f hPa", weather->pressure_hpa);
    } else {
        snprintf(screen->climate, sizeof screen->climate, "BME280 ERROR");
        snprintf(screen->pressure, sizeof screen->pressure, "T/RH/P UNAVAILABLE");
    }
}
