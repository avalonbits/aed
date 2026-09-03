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

text_buffer* tb_init(text_buffer* tb, int mem_kb, const char* fname);
void tb_destroy(text_buffer* tb);

// Info ops.
int tb_size(text_buffer* tb);
int tb_available(text_buffer* tb);
int tb_used(text_buffer* tb);
bool tb_eol(text_buffer* tb);
bool tb_bol(text_buffer* tb);
char* tb_fname(text_buffer* tb);
bool tb_changed(text_buffer* tb);
void tb_set_fname(text_buffer* tb, const char* fname, int sz);

// Character ops.
bool tb_put(text_buffer* tb, char ch);
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
// Moves to `off` bytes from the start of the current line.
char tb_goto_offset(text_buffer* tb, int off);
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

bool tb_load(text_buffer* tb, const char* fname);

// Why tb_open reports rather than prints: tb_load is a startup path and writes
// its complaint straight to the screen, which there is fine because there is no
// editor on it yet. Opening a second file happens with a document on screen, so
// the caller has to be told what went wrong and given the chance to say so
// without scribbling over it.
typedef enum _tb_result {
    TB_OK = 0,
    TB_NO_FILE,     // could not be opened, and could not be created either
    TB_TOO_LARGE,   // will not fit in the buffer, whatever is in there now
} tb_result;

// Replaces the document with the contents of `fname`, as tb_load does for a
// fresh buffer. A name that does not exist is created, so this is also how a
// new document is started -- the same thing naming a missing file on the
// command line does.
//
// The current document survives every failure this can report: the size is
// checked against the whole buffer before a byte of it is discarded. Only an
// I/O error partway through the read can leave the buffer empty, and by then
// the file is gone as far as the editor is concerned anyway.
tb_result tb_open(text_buffer* tb, const char* fname, int sz);

// Empties the document, keeping the buffer and the file name. Cursor back to
// the start, and not dirty: nothing has been typed into it.
void tb_clear(text_buffer* tb);
bool tb_save(text_buffer* tb);
bool tb_valid_file(text_buffer* tb);
void tb_copy(text_buffer* dst, text_buffer* src);

#endif // _TEXT_BUFFER_H_
