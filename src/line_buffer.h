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

#ifndef _LINE_BUFFER_H_
#define _LINE_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _line_buffer  {
    int size_;
    int* buf_;
    int* curr_;
    int* cend_;
} line_buffer;

// Setup ops.
line_buffer* lb_init(line_buffer* lb, int size);
void lb_destroy(line_buffer* lb);

// Info ops
int lb_curr(line_buffer* lb);
int lb_avai(line_buffer* lb);
int lb_max(line_buffer* lb);
bool lb_last(line_buffer* lb);

// Line ops.
bool lb_cinc(line_buffer* lb);
bool lb_cdec(line_buffer* lb);
int lb_csize(line_buffer* lb);

// Cursor ops.
bool lb_up(line_buffer* lb);
bool lb_down(line_buffer* lb);
bool lb_new(line_buffer* lb, int size);
bool lb_del(line_buffer* lb);
bool lb_merge_next(line_buffer* lb);
int lb_merge_prev(line_buffer* lb);

#endif  // _LINE_BUFFER_H_
