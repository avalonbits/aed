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
// Kept in RAM. A copy larger than the buffer is refused rather than truncated,
// because the caller may be about to cut the range and must not delete text
// this could not keep. Spilling a large copy to a scratch file beside the
// document, which removes that limit, is the next change; the interface here is
// already the one it will use, so nothing outside this file has to know.
#define CLIP_SIZE 8192

typedef struct _clipboard {
    char_buffer buf_;
    // Line breaks in the copy. Counted while it is taken rather than by
    // scanning it later, because a paste has to check the line index has room
    // and the line index is bounded separately from the characters.
    int lines_;
} clipboard;

clipboard* clip_init(clipboard* c, int size);
void clip_destroy(clipboard* c);

// Replaces the contents with the text between the two positions. The document
// is only read. Returns false if the range will not fit, leaving the clipboard
// empty rather than holding part of it.
bool clip_copy(clipboard* c, text_buffer* tb, tb_pos a, tb_pos b);

// Inserts the contents at the cursor. Refuses without writing anything when the
// document has no room -- for the characters or for the lines -- so a paste
// lands whole or not at all.
bool clip_paste(clipboard* c, text_buffer* tb);

// Whether anything has been copied yet. A paste with nothing to paste is not an
// error, it just does nothing.
bool clip_has(clipboard* c);

int clip_size(clipboard* c);
int clip_lines(clipboard* c);
int clip_capacity(clipboard* c);

#endif  // _CLIPBOARD_H_
