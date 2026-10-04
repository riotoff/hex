#include "rtc.h"
#include "io.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int cmos_updating(void) {
    return cmos_read(0x0A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

int rtc_read(rtc_time_t* t) {
    while (cmos_updating()) { }

    uint8_t s1 = cmos_read(0x00);
    uint8_t m1 = cmos_read(0x02);
    uint8_t h1 = cmos_read(0x04);
    uint8_t d1 = cmos_read(0x07);
    uint8_t mo1 = cmos_read(0x08);
    uint8_t y1 = cmos_read(0x09);

    uint8_t s2 = cmos_read(0x00);
    uint8_t m2 = cmos_read(0x02);
    uint8_t h2 = cmos_read(0x04);
    uint8_t d2 = cmos_read(0x07);
    uint8_t mo2 = cmos_read(0x08);
    uint8_t y2 = cmos_read(0x09);

    if (s1 != s2 || m1 != m2 || h1 != h2 ||
        d1 != d2 || mo1 != mo2 || y1 != y2) {
        t->valid = 0;
        return 0;
    }

    uint8_t status_b = cmos_read(0x0B);
    int binary = status_b & 0x04;
    int h24    = status_b & 0x02;

    uint8_t s = s1, m = m1, h = h1, d = d1, mo = mo1, y = y1;

    if (!binary) {
        s  = bcd_to_bin(s);
        m  = bcd_to_bin(m);
        d  = bcd_to_bin(d);
        mo = bcd_to_bin(mo);
        y  = bcd_to_bin(y);

        uint8_t h_raw = h;
        uint8_t h_bin = bcd_to_bin(h_raw & 0x7F);

        if (!h24) {
            int pm = h_raw & 0x80;
            if (h_bin == 12) h_bin = 0;
            if (pm) h_bin = (uint8_t)(h_bin + 12);
        }
        h = h_bin;
    } else {
        if (!h24) {
            int pm = h & 0x80;
            uint8_t h_bin = h & 0x7F;
            if (h_bin == 12) h_bin = 0;
            if (pm) h_bin = (uint8_t)(h_bin + 12);
            h = h_bin;
        }
    }

    t->second = s;
    t->minute = m;
    t->hour   = h;
    t->day    = d;
    t->month  = mo;
    t->year   = (uint16_t)(2000 + y);
    t->valid  = 1;
    return 1;
}
