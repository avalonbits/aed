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

// Line ending the document came in with, and the one it goes back out as. The
// buffer itself is always CRLF -- every consumer of the line index subtracts 2
// for a break -- so this is purely about what reaches the file.
typedef enum _tb_eol_style {
    TB_EOL_CRLF = 0,
    TB_EOL_LF,
} tb_eol_style;

typedef struct _text_buffer {
    char_buffer cb_;
    line_buffer lb_;
    int x_;
    bool dirty_;
    tb_eol_style eol_;

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

// --- positions and ranges ---
//
// A position in the document is the pair (line, byte within that line). There
// is no absolute offset: tb_goto_offset moves within the current line only, and
// the bytes themselves are split across the gap. So anything that works on a
// span of text -- selecting, copying, cutting -- needs a position it can carry
// around and compare, which is what this is.
//
// `x` never includes the CRLF. A position at the end of a line's text and one
// at the start of the next are different positions with nothing between them
// but the line break, which is what makes a range unambiguous about whether it
// takes the newline with it.
typedef struct _tb_pos {
    int line;   // 1-based, as tb_ypos reports it
    int x;      // 0-based byte within the line, as tb_xpos reports it less one
} tb_pos;

tb_pos tb_tell(text_buffer* tb);

// Moves the cursor to `p`, clamping to the document: past the last line lands
// on the last line, past the end of a line lands at its end.
void tb_seek(text_buffer* tb, tb_pos p);

// Negative, zero or positive as `a` is before, at, or after `b`. Lets a caller
// hand ranges over in either order without sorting them first.
int tb_cmp(tb_pos a, tb_pos b);

// Bytes between the two positions, counting the CRLF of each line break
// crossed. Order does not matter.
int tb_range_size(text_buffer* tb, tb_pos a, tb_pos b);

// Where the bytes of a range are sent. Called with each run of text and with
// each line break separately; returns false to stop the walk.
typedef bool (*tb_sink)(void* ctx, const char* buf, int sz);

// Feeds the text between the two positions to `sink`, line breaks included as
// CRLF. The document is only read. Returns false if the sink stopped it.
//
// Exists so that a range can go somewhere other than memory -- a copy too large
// for the clipboard goes to a file -- without a second implementation of the
// walk that would have to agree with this one about where lines end.
bool tb_range_walk(text_buffer* tb, tb_pos a, tb_pos b, tb_sink sink, void* ctx);

// Replaces `out` with the text between the two positions, line breaks included
// as CRLF. Returns the number of bytes written, or -1 if the range will not fit
// -- in which case `out` is left empty rather than holding a truncated copy,
// since a caller that then deleted the range would destroy what it could not
// keep. The document is not modified.
int tb_range_copy(text_buffer* tb, tb_pos a, tb_pos b, char_buffer* out);

// Deletes the text between the two positions, leaving the cursor where the
// range began. Returns false only if nothing could be deleted.
bool tb_range_del(text_buffer* tb, tb_pos a, tb_pos b);

// Inserts `sz` bytes at the cursor, treating CRLF and a bare LF alike as a line
// break so that text from anywhere pastes correctly. Refuses without writing
// anything if the document has no room, so a paste either lands whole or not at
// all.
bool tb_insert(text_buffer* tb, const char* buf, int sz);

// Inserts a run of bytes without checking there is room, for a caller that has
// already checked the whole of what it is inserting. `pending_cr` carries a
// trailing CR across calls: text arriving in chunks can be split between the
// CR and the LF of one line break, and the two halves have to make one break
// rather than two. It starts false, and if it is still true at the end the
// caller owes one more tb_newline.
bool tb_insert_span(text_buffer* tb, const char* buf, int sz, bool* pending_cr);

// Whether `bytes` of text carrying `lines` line breaks would fit, once a range
// of `free_bytes` spanning `free_lines` breaks has been removed to make way for
// it. Both budgets are checked: the characters and the line index are bounded
// separately, and on a document of short lines the index runs out first.
//
// A caller that replaces a selection has to ask before deleting it. Finding out
// afterwards means the selection is already gone and there is nothing to paste
// in its place -- and nothing to put back, since there is no undo.
bool tb_can_insert(text_buffer* tb, int bytes, int lines,
                   int free_bytes, int free_lines);

#endif // _TEXT_BUFFER_H_
