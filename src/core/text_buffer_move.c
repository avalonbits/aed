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

/*
 * Moving the cursor: by character, by word, by line, and to an offset.
 *
 * The line steps settle afterwards, because the window they move inside is
 * smaller than the document -- see text_buffer_page.c.
 */
#include "text_buffer.h"
#include "text_buffer_int.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// Cursor ops.
char tb_next(text_buffer* tb) {
    tb->x_++;
    return cb_next(&tb->cb_, 1);
}

#define KEEP_SKIPPING(from_stopch, ch) \
    (!IS_EOL(ch) && ( \
        (from_stopch && tbi_isstop(ch)) || \
        (!from_stopch && !tbi_isstop(ch)) \
    ))

char tb_w_next(text_buffer* tb, char from_ch) {
    const bool stopch = tbi_isstop(from_ch);
    char ch = 0;
    do {
        ch = cb_next(&tb->cb_, 1);
        tb->x_++;
    } while (KEEP_SKIPPING(stopch, ch));

    return ch;
}

char tb_prev(text_buffer* tb) {
    const char ch = cb_prev(&tb->cb_, 1);
    if (ch) {
        tb->x_--;
    }
    return ch;
}

char tb_w_prev(text_buffer* tb, char from_ch) {
    const bool stopch = tbi_isstop(from_ch);
    char ch = 0;
    do {
        ch = cb_prev(&tb->cb_, 1);
        tb->x_--;
    } while (tb->x_ > 0 && KEEP_SKIPPING(stopch, ch));

    return ch;
}

/*
 * A walker's line, and the bytes of it.
 *
 * tbi_walk_bytes maps a run of the document onto the buffer. The live text is two
 * runs, [lo_, curr_) and [cend_, hi_), and a document offset lands in one of
 * them; a run that spans the boundary between them is the only one that comes
 * back in two pieces, and at most one line in the buffer does.
 *
 * A single run is reported as the *suffix* with an empty prefix, which is the
 * shape every single-run caller already reads -- scr_paint_row and the rest
 * take a pair and an empty first half.
 */
void tbi_walk_bytes(text_buffer* tb, int from, int n,
                       char** pre, int* psz, char** suf, int* ssz) {
    char_buffer* cb = &tb->cb_;
    const int split = (int) (cb->curr_ - cb->lo_);

    *pre = NULL;
    *psz = 0;
    *suf = NULL;
    *ssz = 0;
    if (n <= 0 || from < 0) {
        return;
    }
    // Never past the live text. The byte after the last one is a position the
    // cursor can legitimately be in -- the end of the last line -- and reading
    // it is reading off the allocation, which is what cb_peek guards too.
    const int used = split + (int) (cb->hi_ - cb->cend_);
    if (from >= used) {
        return;
    }
    if (from + n > used) {
        n = used - from;
    }
    if (from >= split) {                // wholly above the gap
        *suf = cb->cend_ + (from - split);
        *ssz = n;

        return;
    }
    if (from + n <= split) {            // wholly below it
        *suf = cb->lo_ + from;
        *ssz = n;

        return;
    }
    *pre = cb->lo_ + from;              // the one line that spans it
    *psz = split - from;
    *suf = cb->cend_;
    *ssz = n - *psz;
}

// The text of a walker's line, without the break. lb_at reads the length out
// of the index by number; the last line in the buffer has no break to take off.
int tbi_walk_line_len(text_buffer* tb) {
    const int sz = lb_at(&tb->lb_, tb->wline_);

    return tb->wline_ + 1 >= lb_lines(&tb->lb_) ? sz : sz - eol_len(tb);
}

char tb_up(text_buffer* tb) {
    if (tb->walker_) {
        // By number. Nothing moves: the buffers stay exactly as the cursor
        // that owns them left them, which is what lets the gap be whatever
        // size tbi_prime_spare finds convenient rather than wider than a screen.
        if (tb->wline_ <= 0) {
            return 0;       // the top of the window, which a walker may not pass
        }
        tb->wline_--;
        tb->woff_ -= lb_at(&tb->lb_, tb->wline_);
        const int maxX = tbi_walk_line_len(tb);
        if (tb->x_ > maxX) {
            tb->x_ = maxX;
        }

        return tb_peek(tb);
    }
    if (!lb_up(&tb->lb_)) {
        // The top of the *window*, which on a paged document is not the top of
        // the document. Settling brings the chunk above it in.
        //
        // Here rather than in the callers because every one of them steps:
        // the arrow keys, page up and page down. Arrow-up from the bottom of a
        // 4,000 line document stopped at line 2,551, which is most of the file
        // unreachable by the ordinary way of moving through it.
        //
        // A walker settles to nothing -- it may not move the window -- so this
        // still stops at the edge for the copies that paint the screen, which
        // is what they want.
        if (!tb_settle(tb) || !lb_up(&tb->lb_)) {
            return 0;
        }
    }

    const int  sz = lb_csize(&tb->lb_);
    const int maxX = sz - eol_len(tb);
    int back = sz + tb->x_;
    if (maxX < tb->x_) {
        tb->x_ = maxX;
    }

    // Settled again after moving, for the same reason tb_seek settles: the
    // cursor must not come to rest on the index's last entry, the line that
    // carries on past memory, because its text is still in the store and the
    // line reads as empty. Stepping there is how a document walked with the
    // arrow keys shows a blank line and a NULL suffix.
    //
    // The margins make it cheap on a paged document -- settling does nothing
    // until the cursor is within one of the edge. The early return is what
    // makes it free on a document that is not paged, which is every ordinary
    // one: the call alone, doing nothing, cost 14% of a walk down a 64 KiB
    // file, and keeping the tail call for that path cost nothing at all.
    if (!tb->paged_) {
        return cb_prev(&tb->cb_, back - tb->x_);
    }
    const char ch = cb_prev(&tb->cb_, back - tb->x_);
    tb_settle(tb);

    return ch;
}

char tb_down(text_buffer* tb) {
    if (tb->walker_) {
        if (tb->wline_ + 1 >= lb_lines(&tb->lb_)) {
            return 0;       // the bottom of the window; see tb_up
        }
        tb->woff_ += lb_at(&tb->lb_, tb->wline_);
        tb->wline_++;
        const int maxX = tbi_walk_line_len(tb);
        if (tb->x_ > maxX) {
            tb->x_ = maxX;
        }

        return tb_peek(tb);
    }
    int move = lb_csize(&tb->lb_) - tb->x_;
    if (!lb_down(&tb->lb_)) {
        // The bottom of the window. See tb_up: settling is what gets past it,
        // and a walker settles to nothing.
        if (!tb_settle(tb)) {
            return 0;
        }
        // Measured again after the slide and before stepping off the line, so
        // that it is still the distance to the end of the line being left.
        move = lb_csize(&tb->lb_) - tb->x_;
        if (!lb_down(&tb->lb_)) {
            return 0;
        }
    }
    int cend = lb_csize(&tb->lb_);
    if (!lb_last(&tb->lb_)) {
        cend -= eol_len(tb);
    }
    if (tb->x_ > cend) {
        tb->x_ = cend;
    }
    if (!tb->paged_) {
        return cb_next(&tb->cb_, move + tb->x_);
    }
    const char ch = cb_next(&tb->cb_, move + tb->x_);
    tb_settle(tb);      // see tb_up

    return ch;
}

char tb_home(text_buffer* tb) {
    if (tb->walker_) {
        tb->x_ = 0;

        return tb_peek(tb);
    }
    const int back = tb->x_;
    tb->x_ = 0;
    return cb_prev(&tb->cb_, back);
}

char tb_goto_offset(text_buffer* tb, int off) {
    if (off < 0) {
        off = 0;
    }
    if (tb->walker_) {
        const int maxX = tbi_walk_line_len(tb);
        tb->x_ = off > maxX ? maxX : off;

        return tb_peek(tb);
    }
    if (off < tb->x_) {
        cb_prev(&tb->cb_, tb->x_ - off);
    } else if (off > tb->x_) {
        cb_next(&tb->cb_, off - tb->x_);
    }
    tb->x_ = off;

    return cb_peek(&tb->cb_);
}

char tb_end(text_buffer* tb) {
    if (tb->walker_) {
        tb->x_ = tbi_walk_line_len(tb);

        return 0;
    }
    char ch = cb_peek(&tb->cb_);
    while (!IS_EOL(ch)) {
        ch = cb_next(&tb->cb_, 1);
        tb->x_++;
    }
    return 0;
}

int tb_xpos(text_buffer* tb) {
    return tb->x_ + 1;
}

// Lines held in memory. The index counts breaks, so a document with none of them
// is one line.
static int mem_lines(text_buffer* tb) {
    return lb_max(&tb->lb_) - lb_avai(&tb->lb_) + 1;
}

int tb_ypos(text_buffer* tb) {
    return tb->head_lines_
        + (tb->walker_ ? tb->wline_ : lb_curr(&tb->lb_)) + 1;
}

int tb_ymax(text_buffer* tb) {
    return tb->head_lines_ + mem_lines(tb) + tb->tail_lines_;
}
