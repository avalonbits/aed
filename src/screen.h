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

#ifndef _SCREEN_H_
#define _SCREEN_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _screen {
    uint8_t rows_;
    uint8_t cols_;

    uint8_t currX_;
    uint8_t currY_;

    uint8_t topY_;
    uint8_t bottomY_;

    char tab_size_;
    char cursor_;
    char fg_;
    char bg_;
} screen;

// Setup.
screen* scr_init(screen* scr, char cursor);
void scr_destroy(screen* scr);
void scr_clear(screen* scr);
void scr_footer(screen* scr, const char* fname, bool dirty, int x, int y);

// Input.
void scr_putc(screen* scr, uint8_t ch, uint8_t* prefix, int psz, uint8_t* suffix, int ssz);
void scr_del(screen* scr, uint8_t* suffix, int sz);
void scr_bksp(screen* scr, uint8_t* suffix, int sz);

// Navigation.
void scr_left(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t deltaX, uint8_t* suffix, int sz);
void scr_right(screen* scr, uint8_t from_ch, uint8_t to_c, uint8_t deltaX, uint8_t* prefix, int sz);
void scr_up(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t currX);
void scr_down(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t currX);
void scr_home(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t* prefix, int sz);
void scr_end(screen* scr, uint8_t from_ch, uint8_t to_ch, int deltaX, uint8_t* suffix, int sz);

// Screen management.
void scr_write_line(screen* scr, uint8_t ypos, uint8_t* buf, int sz);
void scr_overwrite_line(screen* scr, uint8_t ypos, uint8_t* buf, int sz, int psz);

void scr_show_cursor_ch(screen* scr, uint8_t ch);
void scr_erase(screen* scr, int sz);

#endif  // _SCREEN_H_
