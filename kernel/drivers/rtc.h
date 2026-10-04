/* kernel/drivers/rtc.h - relogio de tempo real (CMOS) */
#ifndef RUBYOS_DRIVER_RTC_H
#define RUBYOS_DRIVER_RTC_H

#include <stdint.h>

struct rtc_time {
    uint16_t year;     /* assume seculo 20xx */
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;     /* 0..23 */
    uint8_t  minute;
    uint8_t  second;
};

/* Le a data/hora do RTC (o fuso e o que a BIOS/VM configurou; normalmente UTC). */
void rtc_read(struct rtc_time *t);

#endif
