#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int year, month, day, hour, minute, second;
    bool running;
} rtc_time_t;

bool rtc_time_valid(const rtc_time_t *time);
bool rtc_decode(const uint8_t registers[7], rtc_time_t *time);
bool rtc_encode(const rtc_time_t *time, uint8_t registers[7]);
bool rtc_parse_text(const char *text, rtc_time_t *time);
int rtc_weekday(const rtc_time_t *time); /* 0 = Sunday. */
const char *rtc_weekday_name(const rtc_time_t *time);
bool logic_self_test(void);
