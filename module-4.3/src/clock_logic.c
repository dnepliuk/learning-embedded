#include "clock_logic.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

bool rtc_time_valid(const rtc_time_t *t)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!t || t->year < 2000 || t->year > 2099 || t->month < 1 || t->month > 12 ||
        t->hour < 0 || t->hour > 23 || t->minute < 0 || t->minute > 59 ||
        t->second < 0 || t->second > 59) return false;
    const int max_day = days[t->month - 1] + (t->month == 2 && t->year % 4 == 0);
    return t->day >= 1 && t->day <= max_day;
}

static int from_bcd(uint8_t value)
{
    if ((value & 15) > 9 || (value >> 4) > 9) return -1;
    return (value >> 4) * 10 + (value & 15);
}

static uint8_t to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

bool rtc_decode(const uint8_t r[7], rtc_time_t *t)
{
    /* Reject reserved bits and invalid BCD instead of displaying plausible junk. */
    if ((r[1] & 0x80) || (r[2] & 0x80) || (r[4] & 0xc0) || (r[5] & 0xe0))
        return false;
    rtc_time_t value = {
        .year = 2000 + from_bcd(r[6]), .month = from_bcd(r[5]),
        .day = from_bcd(r[4]), .minute = from_bcd(r[1]),
        .second = from_bcd(r[0] & 0x7f), .running = !(r[0] & 0x80),
    };
    if (r[2] & 0x40) {
        const int hour12 = from_bcd(r[2] & 0x1f);
        if (hour12 < 1 || hour12 > 12) return false;
        value.hour = hour12 % 12 + ((r[2] & 0x20) ? 12 : 0);
    } else {
        value.hour = from_bcd(r[2] & 0x3f);
    }
    if (!rtc_time_valid(&value)) return false;
    *t = value;
    return true;
}

int rtc_weekday(const rtc_time_t *t)
{
    static const int month_offset[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (!rtc_time_valid(t)) return -1;
    const int year = t->year - (t->month < 3);
    return (year + year / 4 - year / 100 + year / 400 +
            month_offset[t->month - 1] + t->day) % 7;
}

const char *rtc_weekday_name(const rtc_time_t *t)
{
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const int weekday = rtc_weekday(t);
    return weekday >= 0 ? names[weekday] : "???";
}

bool rtc_encode(const rtc_time_t *t, uint8_t r[7])
{
    if (!rtc_time_valid(t)) return false;
    r[0] = to_bcd(t->second); /* CH=0 starts the oscillator. */
    r[1] = to_bcd(t->minute);
    r[2] = to_bcd(t->hour);   /* Always write 24-hour mode. */
    r[3] = (uint8_t)(rtc_weekday(t) + 1);
    r[4] = to_bcd(t->day);
    r[5] = to_bcd(t->month);
    r[6] = to_bcd(t->year - 2000);
    return true;
}

bool rtc_parse_text(const char *text, rtc_time_t *t)
{
    if (!text || strlen(text) != 19) return false;
    static const int positions[] = {0,5,8,11,14,17};
    static const int widths[] = {4,2,2,2,2,2};
    if (text[4] != '-' || text[7] != '-' || text[10] != ' ' ||
        text[13] != ':' || text[16] != ':') return false;
    int numbers[6] = {0};
    for (int field = 0; field < 6; ++field) {
        for (int i = 0; i < widths[field]; ++i) {
            const char digit = text[positions[field] + i];
            if (digit < '0' || digit > '9') return false;
            numbers[field] = numbers[field] * 10 + digit - '0';
        }
    }
    rtc_time_t value = {numbers[0], numbers[1], numbers[2], numbers[3],
                        numbers[4], numbers[5], true};
    if (!rtc_time_valid(&value)) return false;
    *t = value;
    return true;
}

static uint16_t le16(const uint8_t *b)
{
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

void weather_decode_calibration(const uint8_t b[24], weather_calibration_t *c)
{
    c->t1 = le16(b); c->t2 = (int16_t)le16(b+2); c->t3 = (int16_t)le16(b+4);
    c->p1 = le16(b+6); c->p2 = (int16_t)le16(b+8); c->p3 = (int16_t)le16(b+10);
    c->p4 = (int16_t)le16(b+12); c->p5 = (int16_t)le16(b+14);
    c->p6 = (int16_t)le16(b+16); c->p7 = (int16_t)le16(b+18);
    c->p8 = (int16_t)le16(b+20); c->p9 = (int16_t)le16(b+22);
}

bool weather_compensate(const weather_calibration_t *c, uint32_t raw_t,
                        uint32_t raw_p, double *temperature, double *hpa)
{
    if (!c->p1 || !c->t1 || raw_t > 0xfffff || raw_p > 0xfffff ||
        raw_t == 0x80000 || raw_p == 0x80000) return false;
    /* Bosch BMP280 data sheet, section 8.1. BME280 uses the same T/P equations.
     * https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf
     * Preserve the documented integer t_fine before pressure compensation. */
    const double delta = raw_t / 131072.0 - c->t1 / 8192.0;
    const double fine = (raw_t / 16384.0 - c->t1 / 1024.0) * c->t2 +
                        delta * delta * c->t3;
    const double t = fine / 5120.0;
    const double offset = (int32_t)fine / 2.0 - 64000.0;
    const double correction = (offset * offset * c->p6 / 32768.0 +
                               2.0 * offset * c->p5) / 4.0 + c->p4 * 65536.0;
    const double divisor = (1.0 + (c->p3 * offset * offset / 524288.0 +
                                    c->p2 * offset) / 524288.0 / 32768.0) * c->p1;
    if (fabs(divisor) < 1e-12) return false;
    const double base = (1048576.0 - raw_p - correction / 4096.0) * 6250.0 / divisor;
    const double pa = base + (c->p9 * base * base / 2147483648.0 +
                             c->p8 * base / 32768.0 + c->p7) / 16.0;
    if (!isfinite(t) || !isfinite(pa) || t < -40.0 || t > 85.0 ||
        pa < 30000.0 || pa > 110000.0) return false;
    *temperature = t;
    *hpa = pa / 100.0;
    return true;
}

bool logic_self_test(void)
{
    rtc_time_t t;
    uint8_t r[7] = {0x59,0x33,0x15,0x07,0x14,0x02,0x26};
    if (!rtc_decode(r, &t) || t.hour != 15 || t.second != 59 ||
        rtc_weekday(&t) != 6 || !t.running) return false;
    r[2] = 0x52; /* 12 AM */
    if (!rtc_decode(r, &t) || t.hour != 0) return false;
    r[2] = 0x72; /* 12 PM */
    if (!rtc_decode(r, &t) || t.hour != 12) return false;
    r[0] |= 0x80;
    if (!rtc_decode(r, &t) || t.running) return false;
    r[0] = 0x6a;
    if (rtc_decode(r, &t)) return false;
    if (!rtc_parse_text("2024-02-29 23:59:59", &t) || !rtc_encode(&t, r) ||
        !rtc_decode(r, &t) || t.day != 29 || t.hour != 23) return false;
    if (rtc_parse_text("2026-02-29 00:00:00", &t) ||
        rtc_parse_text("2026-04-31 00:00:00", &t) ||
        rtc_parse_text("2026-11-14 24:00:00", &t) ||
        rtc_parse_text("2026-11-14 00:00:00junk", &t)) return false;
    const weather_calibration_t cal = {
        .t1=27504, .t2=26435, .t3=-1000, .p1=36477, .p2=-10685, .p3=3024,
        .p4=2855, .p5=140, .p6=-7, .p7=15500, .p8=-14600, .p9=6000,
    };
    double temperature, hpa;
    if (!weather_compensate(&cal, 519888, 415148, &temperature, &hpa) ||
        fabs(temperature - 25.08248) > 0.001 || fabs(hpa - 1006.5326) > 0.01)
        return false;
    if (weather_compensate(&cal, 0x80000, 415148, &temperature, &hpa)) return false;
    return true;
}
