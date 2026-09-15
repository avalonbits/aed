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
 * Positions, and the spans between them.
 *
 * Seeking to a position, comparing two, and the operations that work on
 * everything between a pair: measuring, walking, copying, deleting and
 * inserting. A range may cover more of the document than memory holds, so the
 * ones that read stream rather than address bytes.
 */
#include "text_buffer.h"
#include "text_buffer_int.h"

#include "undo.h"

#include <stdbool.h>
#include <string.h>

// The line's text, not counting the CRLF that ends it. The last line has none.
static int line_len(text_buffer* tb) {
    if (tb->walker_) {
        return tbi_walk_line_len(tb);
    }
    const int sz = lb_csize(&tb->lb_);

    return lb_last(&tb->lb_) ? sz : sz - eol_len(tb);
}

tb_pos tb_tell(text_buffer* tb) {
    tb_pos p;
    p.line = tb_ypos(tb);
    p.x = tb->x_;

    return p;
}

int tb_cmp(tb_pos a, tb_pos b) {
    if (a.line != b.line) {
        return a.line < b.line ? -1 : 1;
    }
    if (a.x != b.x) {
        return a.x < b.x ? -1 : 1;
    }

    return 0;
}

void tb_seek(text_buffer* tb, tb_pos p) {
    if (p.line < 1) {
        p.line = 1;
    }

    // tb_up and tb_down report the character they land on, which is 0 for
    // several legitimate positions, so progress is judged by the line number
    // instead -- the same way the repaint loop decides it has run out of lines.
    //
    // Read once per line rather than three times. tb_ypos is a call, and the
    // lb_curr inside it is another, and the loop asked for it twice in the
    // condition on top of the copy kept to notice standing still. A seek across
    // a document was paying six calls a line to answer the same question.
    // Running out of memory is not running out of document. When the step
    // stops making progress and the line wanted is further on, the rest of the
    // document is on disk -- so slide and carry on. A seek is the one movement
    // that has to be able to reach anywhere.
    int y = tb_ypos(tb);
    while (y < p.line) {
        tb_down(tb);
        int now = tb_ypos(tb);
        if (now == y) {
            if (!tb_slide_down(tb)) {
                break;          // the end of the document; nothing below it
            }
            tb_down(tb);
            now = tb_ypos(tb);
            if (now == y) {
                break;
            }
        }
        y = now;
    }
    while (y > p.line) {
        tb_up(tb);
        int now = tb_ypos(tb);
        if (now == y) {
            if (!tb_slide_up(tb)) {
                break;
            }
            tb_up(tb);
            now = tb_ypos(tb);
            if (now == y) {
                break;
            }
        }
        y = now;
    }

    // A seek lands somewhere usable. Without this the cursor can come to rest
    // on the index's last entry -- the line that carries on past memory --
    // and read it as empty, because its text is still in the tail. Callers
    // should not have to know that: find and the range walkers all seek.
    //
    // On a walker this is a no-op, which is right. A walker sees what is in
    // memory and may not move the window; reaching past it is what the
    // streaming range operations are for.
    tb_settle(tb);

    const int len = line_len(tb);
    int x = p.x;
    if (x < 0) {
        x = 0;
    }
    if (x > len) {
        x = len;
    }
    tb_goto_offset(tb, x);
}

static void order(tb_pos* a, tb_pos* b) {
    if (tb_cmp(*a, *b) > 0) {
        const tb_pos t = *a;
        *a = *b;
        *b = t;
    }
}

/*
 * Streaming a range out of a paged document.
 *
 * The walker the in-memory path uses cannot leave MEM, so a range with an end
 * outside the window came back short -- and came back short quietly, which is
 * how a select-all copy on a 160,000 byte document returned 59,324 bytes with
 * tb_range_size agreeing with it the whole way.
 *
 * tbi_doc_stream gives the document from the beginning, in order, so this counts
 * lines and columns as the bytes go past and keeps the ones inside the range.
 * A byte at (line, x) is in [a, b) when it is at or after `a` and before `b`;
 * the two bytes of the break that ends a line belong to that line, so they are
 * in when the line is at or after a.line and before b.line. That is the same
 * arithmetic tb_range_size does in memory -- the rest of the first line, two
 * for each break crossed, whole lines in between, and b.x of the last.
 *
 * A break counts as two whatever it is stored as, because the contract says a
 * range carries CRLF. Nothing else has to agree with the store's bytes.
 */
typedef struct _range_pass {
    tb_pos a;
    tb_pos b;
    int line;           // the document line the next byte belongs to
    int x;              // its column
    bool held_cr;       // the last byte was a carriage return, still undecided
    int count;          // bytes taken so far
    tb_sink out;        // where they go, or NULL to count them only
    void* ctx;
    bool done;          // past the end of the range; stop the stream
    bool stopped;       // the sink said stop, which is not an error either
} range_pass;

static bool rp_take(range_pass* rp, const char* buf, int n) {
    rp->count += n;
    if (rp->out == NULL) {
        return true;
    }
    if (!rp->out(rp->ctx, buf, n)) {
        rp->stopped = true;

        return false;
    }

    return true;
}

// One ordinary character, at the position the pass is sitting on.
static bool rp_char(range_pass* rp, char c) {
    const bool after_a = rp->line > rp->a.line
                         || (rp->line == rp->a.line && rp->x >= rp->a.x);
    const bool before_b = rp->line < rp->b.line
                          || (rp->line == rp->b.line && rp->x < rp->b.x);
    rp->x++;
    if (!after_a || !before_b) {
        return true;
    }

    return rp_take(rp, &c, 1);
}

/*
 * A break is two bytes to a range, whatever the document keeps it as.
 *
 * The range stream emits CRLF by contract -- see range_sink -- so
 * tb_range_size measures the CRLF-normalised text and not the bytes in the
 * buffer. Anything that compares a count against what tb_range_size returned
 * has to count in the same units, or the two drift apart on a document whose
 * own breaks are one byte.
 */
#define TB_RANGE_EOL 2

static bool range_sink(void* ctx, const char* buf, int sz) {
    static const char crlf[TB_RANGE_EOL] = { '\r', '\n' };
    range_pass* rp = (range_pass*) ctx;

    // A whole chunk inside the range, which is most of them on a select-all:
    // every byte of it is taken, so the only thing the walk really does is
    // count line feeds, and memchr does that in CPIR. Per byte the slow path
    // below is a call to rp_char, two position comparisons and a call to
    // rp_take.
    //
    // Only when the chunk cannot straddle either end of the range. The first
    // line of it and the last need the column arithmetic, and a break lands in
    // the middle of a chunk as often as not, so the test is on lines: every
    // line this chunk touches has to be strictly inside.
    if (!rp->held_cr && rp->line > rp->a.line && sz > 0) {
        // One pass for how many breaks there are and where the last one is.
        //
        // This sends the store's own bytes where the slow path sends a CRLF of
        // its own for each break, so the two agree only because every break in
        // the document is a CRLF: the loader converts on the way in, and every
        // other way a break is made goes through tb_newline, which writes the
        // pair. There was a check here that each break really was one, and it
        // could not fire -- but it was catching the chunk whose first byte is
        // the line feed of a break split across the boundary, and so hiding
        // the held_cr test above, which is the thing that actually handles it.
        int lines = 0;
        const char* last = NULL;
        for (const char* p = buf; p < buf + sz; ) {
            const char* nl = (const char*) memchr(p, '\n',
                                                  (size_t) (buf + sz - p));
            if (nl == NULL) {
                break;
            }
            lines++;
            last = nl;
            p = nl + 1;
        }
        if (rp->line + lines < rp->b.line) {
            // A break split across the chunk boundary -- its carriage return
            // the last byte here, its line feed the first byte of the next --
            // leaves the return for the next chunk, which is what the slow
            // path does: a return is not emitted until a feed says it was a
            // break. Emitting it here and the pair there would send three
            // bytes for a two byte break.
            const int take = buf[sz - 1] == '\r' ? sz - 1 : sz;
            if (take > 0 && !rp_take(rp, buf, take)) {
                return false;
            }
            rp->held_cr = take < sz;
            rp->line += lines;
            // Whatever follows the last break is the start of a line, and the
            // column is how far into it the taken bytes reach. `take`, not
            // `sz`, so a deferred carriage return is not counted as a column.
            //
            // Nothing can currently see the difference: a deferred return is
            // always followed by the line feed that completes it, and handling
            // that break sets the column back to zero before anything reads
            // it. This is the value being right rather than merely unused.
            rp->x = last != NULL ? (int) (buf + take - last - 1) : rp->x + take;

            return true;
        }
    }

    for (int i = 0; i < sz; i++) {
        const char c = buf[i];
        if (c == '\r' && !rp->held_cr) {
            rp->held_cr = true;     // a break, or a stray -- the next byte says
            continue;
        }
        if (c == '\n') {
            // The break that ends this line. Two bytes by contract, whatever
            // the store holds.
            rp->held_cr = false;
            if (rp->line >= rp->a.line && rp->line < rp->b.line
                    && !rp_take(rp, crlf, TB_RANGE_EOL)) {
                return false;
            }
            rp->line++;
            rp->x = 0;
            if (rp->line > rp->b.line) {
                rp->done = true;    // nothing after this can be in the range

                return false;
            }
            continue;
        }
        if (rp->held_cr) {
            // Not a break after all: a carriage return on its own is a
            // character like any other.
            rp->held_cr = false;
            if (!rp_char(rp, '\r')) {
                return false;
            }
        }
        if (!rp_char(rp, c)) {
            return false;
        }
    }

    return true;
}

// Runs a pass over the whole document. Returns false only on a real failure --
// running off the end of the range and a sink that asked to stop are both
// ordinary ways to finish early.
static bool range_stream(text_buffer* tb, tb_pos a, tb_pos b,
                         tb_sink out, void* ctx, int* count) {
    range_pass rp;
    rp.a = a;
    rp.b = b;
    rp.line = 1;
    rp.x = 0;
    rp.held_cr = false;
    rp.count = 0;
    rp.out = out;
    rp.ctx = ctx;
    rp.done = false;
    rp.stopped = false;

    const bool ok = tbi_doc_stream(tb, range_sink, &rp);
    if (count != NULL) {
        *count = rp.count;
    }
    if (rp.stopped) {
        return false;
    }

    return ok || rp.done;
}

int tb_range_size(text_buffer* tb, tb_pos a, tb_pos b) {
    order(&a, &b);
    if (a.line == b.line) {
        const int n = b.x - a.x;

        return n > 0 ? n : 0;
    }

    // Streamed, always, rather than walked on a tb_copy down from `a`.
    //
    // The walk was wrong. On a 6,000 line document it gave the wrong answer for
    // 3,130 ranges of the 6,000 tried -- every one from line 2,194 on, by a
    // little more each time -- and what it gave depended on where the real
    // cursor happened to be sitting, because a copy shares the buffers and
    // moving one moves the other's gap. The streaming pass gets all 6,000
    // right, and a grid of 324 ranges on top of that, against an oracle worked
    // out from the document rather than from a second implementation.
    //
    // It is not obviously slower either: the walk moved the character gap a
    // line at a time, where this is a memchr over what memory holds, and a
    // chunk lying wholly inside the range is taken whole.
    int n = 0;
    if (!range_stream(tb, a, b, NULL, NULL, &n)) {
        return 0;
    }

    return n;
}

bool tb_range_walk(text_buffer* tb, tb_pos a, tb_pos b, tb_sink sink, void* ctx) {
    order(&a, &b);

    // The same streaming pass tb_range_size counts with, so the two cannot
    // disagree about what a range is -- which is the failure that made a
    // select-all copy come back with 37% of a document and nothing notice.
    return range_stream(tb, a, b, sink, ctx, NULL);
}

// cb_put is what enforces the destination's size. Running out stops the walk,
// and the caller empties the buffer rather than leaving a truncated copy: one
// that went on to cut the range would otherwise delete text it could not keep.
static bool cb_sink(void* ctx, const char* buf, int sz) {
    return cb_write((char_buffer*) ctx, buf, sz);
}

int tb_range_copy(text_buffer* tb, tb_pos a, tb_pos b, char_buffer* out) {
    if (out == NULL) {
        return -1;
    }
    cb_clear(out);

    if (!tb_range_walk(tb, a, b, cb_sink, out)) {
        cb_clear(out);

        return -1;
    }

    int written = 0;
    cb_prefix(out, &written);

    return written;
}

bool tb_range_del(text_buffer* tb, tb_pos a, tb_pos b) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    order(&a, &b);
    int left = tb_range_size(tb, a, b);
    if (left <= 0) {
        return false;
    }

    tb_seek(tb, a);

    // Deleted one character at a time through the same primitives the DELETE
    // key uses, so the line index is maintained by code that already gets it
    // right rather than by a second implementation that has to agree with it.
    // A paged document runs out of memory part way through: the deleting
    // empties the window and the rest of the range is still in the store.
    // Settling pulls the next chunk in, so the loop only has to notice that a
    // delete failed and try once more.
    //
    // Only on failure, because a settle on every character would be a pair of
    // buffer measurements per byte deleted -- and on a document that is not
    // paged it can never do anything at all.
    bool any = false;
    int stalls = 0;
    while (left > 0) {
        const bool eol = tb_eol(tb);
        if (eol ? tb_del_merge(tb) : tb_del(tb)) {
            // In the units tb_range_size handed over, which counts a break as
            // CRLF however long this document's breaks are. Counting the
            // document's own length here instead left one byte of the range
            // unaccounted for on every break, and the loop deleted the first
            // character of the following line to make it up.
            left -= eol ? TB_RANGE_EOL : 1;
            stalls = 0;
            any = true;
            continue;
        }

        // A paged document runs out of memory part way through: the deleting
        // empties the window and the rest of the range is still in the store.
        // Settling brings the next chunk in, and then the loop starts over
        // rather than retrying what just failed -- the window has moved, so
        // the cursor that was at the end of the last line in memory is now in
        // the middle of one and wants tb_del rather than tb_del_merge.
        //
        // Bounded, because settling says whether it moved anything and not
        // whether that helped. Each move consumes the store, so this ends
        // either way; a handful of passes with nothing deleted is enough to
        // know the rest of the range is not coming.
        if (stalls++ < 4 && tb_settle(tb)) {
            continue;
        }

        break;
    }

    return any;
}

bool tb_can_insert(text_buffer* tb, int bytes, int lines,
                   int free_bytes, int free_lines) {
    if (bytes < 0 || lines < 0 || free_bytes < 0 || free_lines < 0) {
        return false;
    }
    const int chars = cb_available(&tb->cb_) + free_bytes;
    // A split costs a slot and needs a spare, so N breaks need N + 1 free.
    const int slots = lb_avai(&tb->lb_) + free_lines;

    return bytes <= chars && lines + 1 <= slots;
}

bool tb_insert_span(text_buffer* tb, const char* buf, int sz, bool* pending_cr) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    for (int i = 0; i < sz; i++) {
        // A CR held back from the last call, or from the byte before this one.
        // Whatever follows it, the break belongs to the CR; an LF right after
        // just completes it rather than starting a second one.
        if (*pending_cr) {
            *pending_cr = false;
            if (!tb_newline(tb)) {
                return false;
            }
            if (buf[i] == '\n') {
                continue;
            }
        }
        if (buf[i] == '\r') {
            *pending_cr = true;
            continue;
        }
        if (buf[i] == '\n') {
            if (!tb_newline(tb)) {
                return false;
            }
            continue;
        }
        if (!tb_put(tb, buf[i])) {
            return false;
        }
    }

    return true;
}

bool tb_insert(text_buffer* tb, const char* buf, int sz) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    if (buf == NULL || sz <= 0) {
        return false;
    }

    // A bare LF becomes a CRLF, so the text can be a byte longer than it
    // arrived. Counted up front, along with the lines it brings: a paste that
    // ran out of room half way would leave the document holding an arbitrary
    // prefix of it.
    int needed = 0;
    int breaks = 0;
    for (int i = 0; i < sz; i++) {
        if (buf[i] == '\r' && i + 1 < sz && buf[i + 1] == '\n') {
            needed += eol_len(tb);
            breaks++;
            i++;
        } else if (buf[i] == '\n' || buf[i] == '\r') {
            needed += eol_len(tb);
            breaks++;
        } else {
            needed += 1;
        }
    }
    if (!tb_can_insert(tb, needed, breaks, 0, 0)) {
        return false;
    }

    bool pending_cr = false;
    if (!tb_insert_span(tb, buf, sz, &pending_cr)) {
        return false;
    }
    if (pending_cr) {
        return tb_newline(tb);
    }

    return true;
}

void tb_copy(text_buffer* dst, text_buffer* src) {
    // Whole structs rather than field by field. A copy that names its fields
    // goes stale the moment either buffer gains one, and quietly: the walker
    // carries on with whatever was on the stack in the field nobody copied.
    //
    // They alias the original's memory either way; what makes a copy a walker
    // is the flag below, and every mutator refuses on it.
    dst->lb_ = src->lb_;
    dst->cb_ = src->cb_;

    dst->x_ = src->x_;
    dst->walker_ = true;
    dst->paged_ = src->paged_;
    dst->store_ = src->store_;      // shared, and a walker may not slide it
    // A walker reports the same line numbers the cursor does, so it needs the
    // same idea of how much of the document is not in memory.
    dst->head_lines_ = src->head_lines_;
    dst->tail_lines_ = src->tail_lines_;
    dst->fname_ = NULL;
    dst->dirty_ = false;
    dst->load_dirty_ = false;
    // Every piece of line arithmetic subtracts this, so a walker without it
    // reads every line at the wrong length. Third time a field added to one of
    // these structs has been missed here; the two buffers above are assigned
    // whole for that reason, and this list is the part that still has to be
    // kept by hand.
    dst->elen_ = src->elen_;
    // Where the walk starts, in the numbers a walker moves by. The owner knows
    // both: its line is what lb_curr reports, and its line began x_ bytes
    // before the cursor.
    dst->wline_ = lb_curr(&src->lb_);
    dst->woff_ = (int) (src->cb_.curr_ - src->cb_.lo_) - src->x_;
    // Copies are walked, never written to -- refresh_screen and
    // cmd_repaint_rows only move and read. Carrying the log would mean a paint
    // could record an edit.
    dst->undo_ = NULL;
}

void tb_set_undo(text_buffer* tb, undo* u) {
    tb->undo_ = u;
}

// Char read.
char tb_peek(text_buffer* tb) {
    if (tb->walker_) {
        char* pre = NULL;
        char* suf = NULL;
        int psz = 0;
        int ssz = 0;
        // One byte, so it is never the run that spans the gap.
        tbi_walk_bytes(tb, tb->woff_ + tb->x_, 1, &pre, &psz, &suf, &ssz);

        return ssz > 0 ? *suf : 0;
    }
    return cb_peek(&tb->cb_);
}

char* tb_prefix(text_buffer* tb, int* sz) {
    if (tb->walker_) {
        // A walker's line sits where the owner's gap left it, so the bytes
        // before its column can be two runs and there is no one pointer to
        // give. tb_curr_line is what reads a walker's line; nothing asks a
        // walker for half of one.
        *sz = 0;

        return NULL;
    }
    int psz = 0;
    char* prefix = cb_prefix(&tb->cb_, &psz);
    if (prefix == NULL) {
        // Said out loud rather than left to the caller's own initialiser.
        // cb_prefix reports an empty prefix through its own `sz`, not through
        // this one, so returning here without writing it handed tb_curr_line
        // an uninitialised psz_ every time the cursor was at the start of the
        // buffer. Every caller happens to test the pointer before the size, so
        // it never showed -- but it is a garbage length in a struct the view
        // reads.
        *sz = 0;

        return NULL;
    }
    prefix = prefix + (psz - tb->x_);
    *sz = tb->x_;
    return prefix;
}

char* tb_suffix(text_buffer* tb, int* sz) {
    if (tb->walker_) {
        *sz = 0;        // see tb_prefix

        return NULL;
    }
    char* suffix = cb_suffix(&tb->cb_, sz);
    if (suffix == NULL) {
        return NULL;
    }

    *sz = lb_csize(&tb->lb_) - tb->x_;
    if (!lb_last(&tb->lb_)) {
        *sz -= eol_len(tb);
    }
    return suffix;
}

split_line tb_curr_line(text_buffer* tb) {
    split_line ln;

    if (tb->walker_) {
        // The whole line, in the one or two runs the buffer is holding it in.
        // For the cursor the two happen to be "before" and "after" it, because
        // the gap is where the cursor is; for a walker they are wherever the
        // owner's gap falls, which is a different split of the same bytes and
        // the same thing to paint.
        tbi_walk_bytes(tb, tb->woff_, tbi_walk_line_len(tb),
                   &ln.prefix_, &ln.psz_, &ln.suffix_, &ln.ssz_);

        return ln;
    }
    ln.prefix_ = tb_prefix(tb, &ln.psz_);
    ln.suffix_ = tb_suffix(tb, &ln.ssz_);
    return ln;
}
