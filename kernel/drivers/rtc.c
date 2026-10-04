/* kernel/drivers/rtc.c - leitura do RTC via portas CMOS 0x70/0x71 */
#include "drivers/rtc.h"
#include "io.h"
#include "kstring.h"

#include <stdbool.h>

struct raw_time {
    uint8_t second, minute, hour, day, month, year;
};

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    io_wait();
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

static void read_raw(struct raw_time *t)
{
    while (cmos_read(0x0A) & 0x80)         /* espera o fim de uma atualizacao em curso */
        ;
    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour   = cmos_read(0x04);
    t->day    = cmos_read(0x07);
    t->month  = cmos_read(0x08);
    t->year   = cmos_read(0x09);
}

void rtc_read(struct rtc_time *out)
{
    struct raw_time a;
    struct raw_time b;
    uint8_t status_b;
    bool pm;

    /* Le duas vezes ate os valores coincidirem (evita ler no meio de uma atualizacao). */
    read_raw(&a);
    do {
        b = a;
        read_raw(&a);
    } while (memcmp(&a, &b, sizeof(a)) != 0);

    status_b = cmos_read(0x0B);
    pm = (a.hour & 0x80) != 0;
    a.hour &= 0x7F;

    if (!(status_b & 0x04)) {              /* valores em BCD */
        a.second = bcd_to_bin(a.second);
        a.minute = bcd_to_bin(a.minute);
        a.hour   = bcd_to_bin(a.hour);
        a.day    = bcd_to_bin(a.day);
        a.month  = bcd_to_bin(a.month);
        a.year   = bcd_to_bin(a.year);
    }
    if (!(status_b & 0x02)) {              /* modo 12 horas */
        a.hour = (uint8_t)(a.hour % 12);
        if (pm)
            a.hour = (uint8_t)(a.hour + 12);
    }

    out->second = a.second;
    out->minute = a.minute;
    out->hour   = a.hour;
    out->day    = a.day;
    out->month  = a.month;
    out->year   = (uint16_t)(2000 + a.year);
}
