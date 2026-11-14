#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int year, month, day, hour, minute, second;
    bool running;
} rtc_time_t;

typedef struct {
    uint16_t t1, p1;
    int16_t t2, t3, p2, p3, p4, p5, p6, p7, p8, p9;
} weather_calibration_t;

bool rtc_time_valid(const rtc_time_t *time);
bool rtc_decode(const uint8_t registers[7], rtc_time_t *time);
bool rtc_encode(const rtc_time_t *time, uint8_t registers[7]);
bool rtc_parse_text(const char *text, rtc_time_t *time);
int rtc_weekday(const rtc_time_t *time); /* 0 = Sunday. */
const char *rtc_weekday_name(const rtc_time_t *time);
void weather_decode_calibration(const uint8_t bytes[24], weather_calibration_t *cal);
bool weather_compensate(const weather_calibration_t *cal, uint32_t raw_temp,
                        uint32_t raw_pressure, double *celsius, double *hpa);
bool logic_self_test(void);
