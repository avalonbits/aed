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

#ifndef _CHAR_BUFFER_H_
#define _CHAR_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _char_buffer  {
    int size_;
    uint8_t* buf_;
    uint8_t* curr_;
    uint8_t* cend_;
} char_buffer;

// Setup ops.
char_buffer* cb_init(char_buffer* cb, int size);
void cb_destroy(char_buffer* cb);

// Info ops.
int cb_size(char_buffer* cb);
int cb_available(char_buffer* cb);
int cb_used(char_buffer* cb);

// Character ops.
void cb_put(char_buffer* cb, uint8_t ch);
bool cb_del(char_buffer* cb);
bool cb_bksp(char_buffer* cb);

// Cursor ops.
uint8_t cb_next(char_buffer* cb, int cnt);
uint8_t cb_prev(char_buffer* cb, int cnt);

// Char read.
uint8_t cb_peek_at(char_buffer* cb, int idx);
uint8_t* cb_prefix(char_buffer* cb, int* sz);
uint8_t* cb_suffix(char_buffer* cb, int* sz);


#endif  // _CHAR_BUFFER_H_
