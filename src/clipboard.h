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

#ifndef _CLIPBOARD_H_
#define _CLIPBOARD_H_

#include <stdbool.h>

#include "text_buffer.h"

// What a copy is held in. Text only: where it came from and where it is going
// are the caller's business, and the positions that describe a range mean
// nothing once the document changes.
//
// Small copies live in RAM. One too large for that is written to a scratch file
// beside the document -- <name>.scratch -- so there is no size a copy cannot
// be. Which of the two is in use is decided before a byte moves, from the size
// of the range, so nothing is ever half in one and half in the other.
//
// The scratch file belongs to the editing session: it is removed on exit, and
// it holds nothing but the copied text, so it can be opened and read like any
// other file if it is ever left behind by a crash.
#define CLIP_SIZE 8192

// Read and written a chunk at a time. A copy has no upper bound, so neither end
// of it can be held in memory whole.
#define CLIP_CHUNK 256

typedef struct _clipboard {
    char_buffer buf_;
    // Line breaks in the copy. Counted while it is taken rather than by
    // scanning it later, because a paste has to check the line index has room
    // and the line index is bounded separately from the characters -- and
    // scanning a file to find out would mean reading it twice.
    int lines_;
    // Bytes held, wherever they are held. Not derived from buf_, which is empty
    // when the copy went to the file.
    int size_;
    bool on_file_;
    // Fixed when the copy spills, not worked out again later: the document can
    // be renamed or replaced with CTRL+O afterwards, and the file still has to
    // be readable and still has to be removed.
    char path_[280];
} clipboard;

clipboard* clip_init(clipboard* c, int size);

// Removes the scratch file if there is one. The clipboard does not outlive the
// session that made it.
void clip_destroy(clipboard* c);

// Replaces the contents with the text between the two positions, in RAM if it
// fits and in a file beside the document if it does not. The document is only
// read. Returns false if nothing could be copied -- an empty range, or a
// scratch file that could not be written.
bool clip_copy(clipboard* c, text_buffer* tb, tb_pos a, tb_pos b);

// Inserts the contents at the cursor. The caller is expected to have checked
// there is room with tb_can_insert: a file-backed paste arrives a chunk at a
// time and cannot be undone half way through.
bool clip_paste(clipboard* c, text_buffer* tb);

// Whether anything has been copied yet. A paste with nothing to paste is not an
// error, it just does nothing.
bool clip_has(clipboard* c);

int clip_size(clipboard* c);
int clip_lines(clipboard* c);
int clip_capacity(clipboard* c);

// Where a spilled copy is being kept, or "" when it is in memory. For tests and
// for anything that needs to know a file exists.
const char* clip_path(clipboard* c);

#endif  // _CLIPBOARD_H_
