/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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

