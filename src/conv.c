#include "conv.h"

#include <stdbool.h>
static void reverse(uint8_t* buf, int sz) {
    int start = 0;
    int end = sz -1;
    while (start < end) {
        uint8_t ch = buf[start];
        buf[start] = buf[end];
        buf[end] = ch;
        ++start;
        --end;
    }
}

uint8_t* i2s(int num, uint8_t* buf, int sz) {
    const bool is_neg = num < 0;

    if (sz < 2) {
        return NULL;
    }
    if (is_neg) {
        if (sz < 3) {
            return NULL;
        }
        num = -num;
    }
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    int i = 0;
    while (num != 0 && i < sz) {
        int rem = num % 10;
        buf[i++] = rem + '0';
        num = num / 10;
    }

    if (num != 0) {
        // Not enough buffer space.
        return NULL;
    }
    if (is_neg) {
        if (i > sz-2) {
            // Not enough space for negative sign.
            return NULL;
        }
        buf[i++] = '-';
    }

    reverse(buf, i);
    buf[i] = 0;
    return buf;
}

