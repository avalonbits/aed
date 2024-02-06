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

#ifndef _TEXT_BUFFER_H_
#define _TEXT_BUFFER_H_

#include "char_buffer.h"
#include "line_buffer.h"

typedef struct _text_buffer {
    char_buffer cb_;
    line_buffer lb_;
    int x_;
    bool dirty_;

    char fname_[256];
} text_buffer;

text_buffer* tb_init(text_buffer* tb, char tab_size, int mem_kb, const char* fname);
void tb_destroy(text_buffer* tb);

// Info ops.
int tb_size(text_buffer* tb);
int tb_available(text_buffer* tb);
int tb_used(text_buffer* tb);
bool tb_eol(text_buffer* tb);
bool tb_bol(text_buffer* tb);
char* tb_fname(text_buffer* tb);
bool tb_changed(text_buffer* tb);
void tb_saved(text_buffer* tb);

// Character ops.
void tb_put(text_buffer* tb, char ch);
bool tb_del(text_buffer* tb);
bool tb_bksp(text_buffer* tb);
bool tb_newline(text_buffer* tb);
bool tb_del_line(text_buffer* tb);
bool tb_del_merge(text_buffer* tb);
bool tb_bksp_merge(text_buffer* tb);

// Cursor ops.
char tb_next(text_buffer* tb);
char tb_w_next(text_buffer* tb, char from_ch);
char tb_prev(text_buffer* tb);
char tb_w_prev(text_buffer* tb, char from_ch);
char tb_home(text_buffer* tb);
char tb_up(text_buffer* tb);
char tb_down(text_buffer* tb);
char tb_end(text_buffer* tb);
int tb_xpos(text_buffer* tb);
int tb_ypos(text_buffer* tb);
int tb_ymax(text_buffer* tb);

// Text read.
char tb_peek(text_buffer* tb);
char* tb_suffix(text_buffer* tb, int* sz);
char* tb_prefix(text_buffer* tb, int* sz);

typedef struct _split_line {
    int psz_;
    char* prefix_;
    int ssz_;
    char* suffix_;
} split_line;
split_line tb_curr_line(text_buffer* tb);

bool tb_load(text_buffer* tb, char tab_size, const char* fname);
void tb_content(text_buffer* tb, char** prefix, int* psz, char** suffix, int* ssz);
bool tb_valid_file(text_buffer* tb);
void tb_copy(text_buffer* dst, text_buffer* src);

#endif // _TEXT_BUFFER_H_
