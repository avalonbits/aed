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
 * The window: which part of a large document is in memory, and moving it.
 *
 * A document too big for the buffer keeps its middle in memory and the rest in
 * a store either side -- head above, tail below. Sliding moves the window;
 * settling decides whether it needs to.
 */
#include "text_buffer.h"
#include "text_buffer_int.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

bool tb_page_open(text_buffer* tb, const char* base) {
    if (tb == NULL || tb->walker_ || tb->paged_) {
        return false;
    }
    tb->store_ = (doc_store*) malloc(sizeof(doc_store));
    if (tb->store_ == NULL) {
        return false;
    }
    if (!store_init(tb->store_, base)) {
        free(tb->store_);
        tb->store_ = NULL;

        return false;
    }
    tb->paged_ = true;

    return true;
}

bool tb_page_fill(text_buffer* tb, const char* buf, int n) {
    if (tb == NULL || !tb->paged_ || buf == NULL || n < 0) {
        return false;
    }
    if (!store_tail_append(tb->store_, buf, n)) {
        return false;
    }
    // Complete lines, each ending in the break that closes it, which is what
    // both counters hold -- see head_lines_ in text_buffer.h.
    tb->tail_lines_ += tbi_count_lines(buf, n);

    return true;
}

/*
 * Sliding the window the document is seen through.
 *
 * Whole lines only, both ways. Memory then always holds complete lines, and
 * neither the index nor anything reading it needs a case for a line that
 * straddles the edge -- at the cost of a documented limit, that a line longer
 * than a chunk cannot be moved at all.
 *
 * The buffers used here are file-scope rather than frames. A chunk is 2 KiB and
 * the line entries for one are a few hundred bytes more, which is twenty times
 * what an ix displacement reaches; on the stack they would make every local in
 * this file cost an address computation. See test/frames.sh.
 */
// One byte over a chunk: sliding up reads a byte of lookbehind in front of the
// run it wants, to tell a chunk that landed on a line boundary from one that
// did not.
static char slide_bytes[TB_CHUNK + 1];
static int  slide_lens[TB_CHUNK / 2 + 1];   // the shortest possible line is "\r\n"

// Splits a run of bytes into the lengths of the whole lines in it, each ending
// in the line feed that closes it. Returns how many, and how many bytes they
// account for -- which is less than `n` when the run ends mid-line.
// memchr rather than a loop over the bytes: it reaches CPIR, which the eZ80 does
// in a handful of cycles a byte against the twenty-odd a C comparison costs, and
// every byte of a document passes through here on the way in and again on every
// slide. See .internal/docs/PAGING.md for what that was worth.
static int run_lines(const char* buf, int n, int* lens, int max, int* bytes) {
    int lines = 0;
    int used = 0;
    while (lines < max && used < n) {
        const char* nl = (const char*) memchr(buf + used, '\n',
                                              (size_t) (n - used));
        if (nl == NULL) {
            break;
        }
        const int at = (int) (nl - buf);
        lens[lines++] = at - used + 1;
        used = at + 1;
    }
    *bytes = used;

    return lines;
}

// Whether this buffer may move its own window. A walker shares the cursor's
// buffers, so sliding one would take the window out from under the cursor that
// owns it -- the other half of pitfall 1, and what the walker flag was put
// there for.
static bool may_slide(text_buffer* tb) {
    return tb != NULL && tb->paged_ && !tb->walker_;
}

bool tb_settle(text_buffer* tb) {
    if (!may_slide(tb)) {
        return false;
    }
    bool moved = false;

    // Bounded because each slide moves a chunk and a margin is several of them,
    // so a cursor dropped into the middle of a document needs a handful. The
    // bound is what stops a slide that keeps failing from spinning here.
    int guard = TB_MARGIN / TB_CHUNK + 2;

    // Which way to go is decided once and held for the whole settle. Each
    // branch was asked on its own before, and re-asked after every slide, so
    // when both sides were short the answer alternated: slide up, which takes
    // from below the cursor to give above it, then slide down, which puts it
    // straight back. Ten slides a keystroke and the window exactly where it
    // started.
    //
    // Both sides are short whenever the window is narrower than two margins,
    // and the index makes that ordinary -- one slot per 32 bytes means a
    // document of ten-byte lines fills the index at 15,350 bytes, less than a
    // single margin, whatever the buffer's size. A climb up such a document
    // stopped at the top of the window with the rest of it in the head.
    //
    // Holding one direction leaves the old behaviour alone wherever only one
    // of them was ever possible, which is the common case: a cursor with the
    // document above it can only slide up.
    int dir = 0;                    // -1 up, +1 down, 0 undecided
    while (guard-- > 0) {
        const int psz = cb_prefix_size(&tb->cb_);
        const int ssz = cb_suffix_size(&tb->cb_);

        const bool can_down = ssz < TB_MARGIN && store_tail_bytes(tb->store_) > 0;
        const bool can_up = psz < TB_MARGIN && store_head_bytes(tb->store_) > 0;

        // A window too narrow to hold both margins cannot satisfy them however
        // the text is arranged, and then asking each side about its own margin
        // is the wrong question: both answers are yes for ever, and a slide
        // that fixes one breaks the other. Settling alternates, a slide each
        // way per keystroke, and the document is read off the card twice over
        // for nothing.
        //
        // That window is ordinary rather than exotic. The index holds one slot
        // per 32 bytes, so a document of short lines fills it with the buffer
        // part empty: ten-byte lines in a 48 KiB buffer give a 15,350 byte
        // window, and climbing 42,000 of them took 725,746 reads where a
        // healthy window needs under 200.
        //
        // So the goal changes with the size. When both margins fit, keep them.
        // When they cannot, centre the cursor instead -- the most screenfuls
        // either side that the window can give -- and only move when a slide
        // would improve the balance by more than it costs. That is what stops
        // the alternation: a slide shifts the difference by two chunks, so
        // requiring one chunk of improvement leaves nothing to undo.
        //
        // Only where centring can mean anything. A buffer smaller than a few
        // chunks cannot be balanced a chunk at a time -- at the 1 KiB the
        // tests use, one chunk is twice the whole buffer -- so those keep the
        // margin rule, which is what they were written against.
        if (psz + ssz < 2 * TB_MARGIN && psz + ssz >= 4 * TB_CHUNK) {
            const int diff = psz - ssz;
            if (diff > TB_CHUNK && store_tail_bytes(tb->store_) > 0) {
                dir = 1;
            } else if (-diff > TB_CHUNK && store_head_bytes(tb->store_) > 0) {
                dir = -1;
            } else {
                break;
            }
            if (!(dir > 0 ? tb_slide_down(tb) : tb_slide_up(tb))) {
                break;
            }
            moved = true;
            continue;
        }

        if (dir == 0) {
            if (can_down && can_up) {
                dir = ssz <= psz ? 1 : -1;      // the emptier side first
            } else if (can_down) {
                dir = 1;
            } else if (can_up) {
                dir = -1;
            } else {
                break;
            }
        }

        if (dir > 0 ? (!can_down || !tb_slide_down(tb))
                    : (!can_up || !tb_slide_up(tb))) {
            break;
        }
        moved = true;
    }
    return moved;
}

// Keeps at most `room` of the lines a run was split into, and says how many
// bytes those are. The index can run out before the buffer does -- it has one
// slot per 32 bytes of buffer, so a document of short lines fills it first --
// and a slide that took in more lines than there are slots for would put bytes
// in memory with nothing describing them.
static int fit_lines(const int* lens, int lines, int room, int* bytes) {
    if (lines > room) {
        lines = room > 0 ? room : 0;
    }
    int n = 0;
    for (int i = 0; i < lines; i++) {
        n += lens[i];
    }
    *bytes = n;

    return lines;
}

// Room kept back when memory is filled at open, so the first slide has
// somewhere to put what it brings in, and so the margins have something to be
// margins of. A share of the buffer rather than a fixed amount, so it scales
// with whatever the editor was given: a fixed reserve is most of a small
// buffer and a rounding error in a large one.
int tbi_prime_spare(text_buffer* tb) {
    // A quarter of the buffer, and no cap. It used to be capped at two chunks,
    // which filled memory to within 4 KB of full -- and a buffer with no free
    // space in it has nowhere to put the slack the four ends need, so every
    // slide closed a quarter of a megabyte up against the wall. That was 71%
    // of what walking a large document cost.
    //
    // A quarter was for a while a floor rather than a choice: a walker read by
    // moving the gap, so the gap had to outlast a repaint -- 12 KiB on the
    // widest mode -- and cb_rebalance gave it a third of whatever this left.
    // Anything under a seventh of the buffer put the gap below that and a
    // repaint would corrupt the document it was painting. Walkers move by
    // number now, and that floor is gone; see .internal/docs/WALKER.md.
    //
    // It stays a quarter because the measurements still say so, for reasons
    // that have nothing to do with the old one. On slow.asm, 419 KB, MOS
    // 3.0.2:
    //
    //              open    seek    3000 down
    //     1/4      0.94s   2.32s   0.38s
    //     1/8      1.04s   2.50s   0.34s
    //     1/16     1.10s   2.84s   0.32s
    //
    // Opening and seeking both want the reserve; only scrolling wants it back,
    // and it gains 0.06s where a seek loses 0.52. See docs/SIZING.md.
    //
    // The trade is fewer lines in memory, so a long scroll crosses more
    // chunks. Each one is cheap enough that it is worth it.
    return cb_size(&tb->cb_) / 4;
}

// Puts the front of a run of bytes into memory, whole lines only, and says how
// many bytes it took. Zero means there is no room -- in the buffer or in the
// index -- or the run does not hold a whole line; either way the rest of it is
// the caller's to put somewhere else.
int tbi_mem_give_back(text_buffer* tb, const char* buf, int n, int spare,
                         int* took_lines) {
    *took_lines = 0;
    const int room = cb_available(&tb->cb_) - spare;
    if (n <= 0 || room <= 0) {
        return 0;
    }
    if (n > room) {
        n = room;
    }
    int bytes = 0;
    // One slot held back for the trailing entry the fill puts on at the end.
    int lines = run_lines(buf, n, slide_lens,
                          (int) (sizeof(slide_lens) / sizeof(slide_lens[0])),
                          &bytes);
    lines = fit_lines(slide_lens, lines, lb_room(&tb->lb_) - 1, &bytes);
    if (lines == 0) {
        return 0;
    }
    // Checked before either is touched. Doing one and finding the other will
    // not go leaves bytes in memory with no entry describing them, which is a
    // document that reads as gibberish from there on.
    if (!cb_give_back(&tb->cb_, buf, bytes)) {
        return 0;
    }
    lb_give_back(&tb->lb_, slide_lens, lines);
    *took_lines = lines;

    return bytes;
}

// An empty buffer is not nothing: it is one line, of no length, and that line
// belongs at the *end* of what gets filled in -- it is the line the document's
// last break opens, or the one that carries on into the tail. Entries given at
// the back arrive after it, which leaves it in front of the first real line and
// every line number one out.
//
// So it is moved to the back once the filling is done: one empty entry is
// appended and lb_del pulls the first real line into the slot the cursor is on.
// The count comes out the same, which is the point -- dropping the empty line
// instead loses a line from the document.
void tbi_mem_close_empty(text_buffer* tb) {
    static const int trailing = 0;
    if (lb_give_back(&tb->lb_, &trailing, 1)) {
        lb_del(&tb->lb_);   // the first real line becomes the cursor's
    }
}

bool tb_page_prime(text_buffer* tb) {
    if (tb == NULL || !tb->paged_ || tb->walker_) {
        return false;
    }
    const int spare = tbi_prime_spare(tb);

    const bool was_empty = cb_used(&tb->cb_) == 0;
    bool gave = false;

    while (store_tail_bytes(tb->store_) > 0 && cb_available(&tb->cb_) > spare) {
        // Never more than will fit: a buffer smaller than a chunk would take
        // nothing at all otherwise, because the give would be refused and the
        // filling would stop before it started.
        int want = cb_available(&tb->cb_) - spare;
        if (want > TB_CHUNK) {
            want = TB_CHUNK;
        }
        const int got = store_tail_pop(tb->store_, slide_bytes, want);
        if (got <= 0) {
            break;
        }
        int lines = 0;
        const int bytes = tbi_mem_give_back(tb, slide_bytes, got, spare, &lines);
        if (bytes == 0) {
            // Either a line longer than a chunk, which nothing can hold a
            // chunk at a time, or the index is full. Both stop the filling
            // rather than fail it: what is in memory is sound either way.
            store_tail_rewind(tb->store_, got);
            break;
        }
        if (got > bytes) {
            store_tail_rewind(tb->store_, got - bytes);
        }
        tb->tail_lines_ -= lines;
        gave = true;
    }
    if (was_empty && gave) {
        tbi_mem_close_empty(tb);
    }

    return true;
}

/*
 * Whether a slide can bring a chunk in without sending one out.
 *
 * A slide normally makes room by pushing the far end of the window into the
 * store, so memory holds what it held and cannot grow until it bursts. That
 * reasoning needs something to push. Deleting empties the window -- a range
 * larger than memory empties it outright -- and then there is nothing to send
 * and nothing that needs sending, because the room is already there.
 *
 * Asked as a question about room rather than about emptiness. Sliding down
 * used to make the exception only for a buffer holding nothing at all, which
 * left a window down to its last byte unable to move in either direction: the
 * head kept the rest of the document and no slide would bring it back.
 */
static bool slide_room(text_buffer* tb) {
    return cb_available(&tb->cb_) >= TB_CHUNK && lb_room(&tb->lb_) > 0;
}

bool tb_slide_down(text_buffer* tb) {
    if (!may_slide(tb)) {
        return false;
    }
    if (store_tail_bytes(tb->store_) == 0) {
        return false;       // nothing below to bring in
    }

    // Out of the front, into the head. Whole lines, and never the line the
    // cursor is on -- lb_front_fit only counts the ones before it.
    //
    // Never more than the tail can put back. The cap the other way -- no more
    // comes in than went out -- was only half of it: a tail holding ten bytes
    // still had a whole 2 KiB chunk evicted against it, and the difference
    // stayed in the head. Settling near the bottom of a document made a pump
    // out of that. Sliding up would send the one line behind the cursor to the
    // tail; sliding down saw a tail with something in it, evicted a chunk from
    // the front, and got ten bytes back. Two thousand and thirty bytes left the
    // window per turn, and inside three cursor movements a 15,350 byte window
    // held thirty bytes with the document's other 199,960 in the head.
    //
    // Never more than the store can put back, which is what stops a slide
    // being a leak. The cap the other way -- no more comes in than went out --
    // was only half of it: a tail holding eighty bytes still had a whole 2 KiB
    // chunk evicted against it, and the difference stayed in the head.
    //
    // The exception is the index. Sending text out is also how slots are
    // freed, so when there are none left the eviction has to happen whatever
    // the tail can return, or a document of short lines cannot slide at all.
    int out_lines = 0;
    int out_room = store_tail_bytes(tb->store_);
    if (lb_room(&tb->lb_) == 0 && out_room < TB_CHUNK) {
        out_room = TB_CHUNK;        // slots, not bytes, are what is short
    }
    if (out_room > TB_CHUNK) {
        out_room = TB_CHUNK;
    }
    const int out_bytes = lb_front_fit(&tb->lb_, out_room, &out_lines);
    if (out_lines == 0 && !slide_room(tb)) {
        return false;       // the first line is longer than a chunk
    }
    // Nothing in front of the cursor to send, but room to bring text into all
    // the same -- see slide_room. Deleting a range larger than memory empties
    // the window outright, and that used to leave the rest of the document in
    // the store with no way back: a select-all cut on a 160,000 byte document
    // copied all of it and left 2,516 lines behind.

    // Sent before anything is brought in, so that the room it frees -- in the
    // index as much as in the buffer -- is there to bring into.
    static int out_lens[TB_CHUNK / 2 + 1];
    static char out_buf[TB_CHUNK];
    lb_take_front(&tb->lb_, out_lens, out_lines);
    cb_take_front(&tb->cb_, out_buf, out_bytes);

    if (!store_head_push(tb->store_, out_buf, out_bytes)) {
        cb_give_front(&tb->cb_, out_buf, out_bytes);
        lb_give_front(&tb->lb_, out_lens, out_lines);

        return false;
    }
    tb->head_lines_ += out_lines;

    // No more comes in than went out, so memory holds what it held. Without
    // that a slide near the top of a document -- where there is barely anything
    // in front of the cursor to send -- would take in a whole chunk against a
    // line or two going out, and memory would grow until it burst.
    // What went out -- and more when the window has room going spare, for the
    // reason sliding up refills: the run that comes back ends part way through
    // a line and that tail is rewound, so matching the outgo loses half a line
    // every slide. See the note in tb_slide_up.
    int want = out_bytes > 0 ? out_bytes : TB_CHUNK;
    {
        const int slack = cb_available(&tb->cb_) - tbi_prime_spare(tb);
        if (slack > want) {
            want = slack;
        }
        if (want > TB_CHUNK) {
            want = TB_CHUNK;
        }
    }
    const int left = store_tail_bytes(tb->store_);
    if (left <= TB_CHUNK && left > want && cb_available(&tb->cb_) >= left) {
        // The tail's last scrap, taken whole. Bounded intake is what stops
        // memory growing without limit, and a scrap under one chunk cannot do
        // that -- but taking it in pieces can leave a piece with no break in
        // it, which run_lines finds no line in and every slide puts back.
        want = left;
    }
    const int got = store_tail_pop(tb->store_, slide_bytes, want);
    if (got <= 0) {
        return true;        // the front went out; there was nothing to replace it
    }
    int in_bytes = 0;
    int in_lines = run_lines(slide_bytes, got, slide_lens,
                             (int) (sizeof(slide_lens) / sizeof(slide_lens[0])),
                             &in_bytes);
    // The index's last entry is the line that carries on past memory, and it
    // stays last -- so it comes off here and goes back on at the end. Lines
    // appended after it instead would leave an empty line in the middle of the
    // document, and every line number past it one further out with every slide.
    int trailing = 0;
    const bool had_trailing = lb_take_back(&tb->lb_, &trailing, 1) == 1;

    // One slot short of the room: an entry for whatever carries on past
    // memory goes back after these -- either the one just taken off, or a
    // fresh one when the line being completed is the cursor's own.
    // Nothing that ends in a break, and the tail is now empty: what came back
    // is the document's last line, which has no break by definition. It is the
    // rest of the line the index's last entry is already counting, so the entry
    // grows by it and the tail is done.
    //
    // Without this those bytes could never enter memory -- run_lines finds no
    // line in them, so every slide put them back -- and because the tail then
    // never empties, settling kept choosing to slide down for ever. Each of
    // those slides sent a little of the front to the head and got nothing in
    // return, so a file with no final newline drained its own window: 190,408
    // bytes down to 25,595, and the cursor could not be scrolled back to the
    // top afterwards.
    if (in_lines == 0 && store_tail_bytes(tb->store_) == 0 && got > 0
            && had_trailing && cb_give_back(&tb->cb_, slide_bytes, got)) {
        trailing += got;
        lb_give_back(&tb->lb_, &trailing, 1);

        return true;
    }
    in_lines = fit_lines(slide_lens, in_lines, lb_room(&tb->lb_) - 1, &in_bytes);
    if (in_lines == 0 || !cb_give_back(&tb->cb_, slide_bytes, in_bytes)) {
        store_tail_rewind(tb->store_, got);
        if (had_trailing) {
            lb_give_back(&tb->lb_, &trailing, 1);
        }

        return true;        // still a slide: the front is in the head
    }
    if (got > in_bytes) {
        store_tail_rewind(tb->store_, got - in_bytes);
    }

    // The first line to arrive completes the line that was carrying on past
    // memory -- that is what it *is*, the rest of it. The others follow, and a
    // fresh empty entry goes last for whatever carries on now.
    //
    // When the cursor is on that line there was nothing after it to take off,
    // so it is still the cursor's own entry and has to be grown in place.
    // Appending after it instead leaves it stranded in the middle of the
    // document, with no bytes, and a later slide ships it to the head as a line
    // that is not there -- which is a head counting one more line than it holds
    // and every line number past it wrong.
    //
    // Both branches are the same shape: grow the line that was carrying on,
    // append the rest, and end with a fresh empty entry for whatever carries
    // on now. They differ only in where that first line lives -- the cursor's
    // own entry, or the one taken off above.
    //
    // The taken-off branch used to append the arrivals and then put the
    // trailing entry back after them. That put the rest of a line in front of
    // its own beginning and left the boundary between them in the middle of
    // the text, so the index said a line ended where there was no break. The
    // next slide then shipped that miscounted line to the head, which is how a
    // head came to end part way through a line and to count one more line than
    // it held.
    static const int fresh = 0;
    if (had_trailing) {
        trailing += slide_lens[0];
        lb_give_back(&tb->lb_, &trailing, 1);
        if (in_lines > 1) {
            lb_give_back(&tb->lb_, slide_lens + 1, in_lines - 1);
        }
        lb_give_back(&tb->lb_, &fresh, 1);
    } else {
        lb_cadd(&tb->lb_, slide_lens[0]);
        if (in_lines > 1) {
            lb_give_back(&tb->lb_, slide_lens + 1, in_lines - 1);
        }
        lb_give_back(&tb->lb_, &fresh, 1);
    }
    tb->tail_lines_ -= in_lines;

    return true;
}

bool tb_slide_up(text_buffer* tb) {
    if (!may_slide(tb)) {
        return false;
    }
    if (store_head_bytes(tb->store_) == 0) {
        return false;       // nothing above to bring in
    }

    // Off with the trailing entry first, for the same reason as sliding down:
    // it is the line that carries on past memory and it stays last. Measuring
    // without taking it off would count it as a line of no length and send out
    // a line that is not there.
    int trailing = 0;
    const bool had_trailing = lb_take_back(&tb->lb_, &trailing, 1) == 1;

    // Taking an index entry off does not take the bytes it describes with it,
    // and the trailing line's bytes are the last ones in memory. So whatever
    // goes out has to include them: cb_take_back takes from the end, and the
    // whole lines being sent sit *in front* of that line rather than behind
    // it. Sending only the whole lines takes the wrong bytes -- part of the
    // trailing line and part of the last whole one -- and pushes a run with no
    // break in it to the tail as though it were a line.
    //
    // Refusing to send at all when that line has text was the older answer.
    // It is safe and it is a dead end: the lines in front of the trailing one
    // can only reach the tail through it, so a document whose last line has
    // text -- which is any file without a final newline -- could fill its
    // window and then never move it up again.
    //
    // Sending it too is what works. Its bytes go to the tail with the lines in
    // front of it, in the order they already sit in memory, and the entry left
    // behind is empty: the line now carries on past memory, which is what a
    // trailing entry means.
    //
    // A chunk at a time, less whatever the trailing line takes, because the two
    // travel together and one buffer has to hold both.
    //
    // Deliberately *not* bounded by what the head can give back, which sliding
    // down is bounded by. The symmetry is tempting and it costs: going up, the
    // head empties as the window climbs, so near the top of a file such a
    // bound stops anything going out, the window fills, and the slides start
    // failing. Measured on slow.asm, it put 3,000 arrow-ups from 30 to 94
    // centiseconds and a seek to the top from 220 to 446. The leak that bound
    // was written for is in sliding down, and that is where it stayed.
    int out_lines = 0;
    int out_bytes = 0;
    int out_room = TB_CHUNK - trailing;
    if (out_room > 0) {
        out_bytes = lb_back_fit(&tb->lb_, out_room, &out_lines);
    }
    // What actually leaves: the whole lines, and the trailing line's bytes
    // behind them. A trailing line longer than a chunk cannot go at all, and
    // then this is the older behaviour again.
    int send = out_bytes;
    if (trailing > 0 && trailing <= TB_CHUNK && out_bytes + trailing <= TB_CHUNK) {
        send += trailing;
    }
    // Nothing behind the cursor to send is only a reason to stop when there is
    // also no room to bring anything into -- the same exception sliding down
    // makes, and for the same reason. Without it a window emptied by deleting
    // could not move up, so the head held the rest of the document and the
    // only way back to it was gone.
    if (send == 0 && !slide_room(tb)) {
        if (had_trailing) {
            lb_give_back(&tb->lb_, &trailing, 1);
        }

        return false;
    }
    if (send > 0 && !store_tail_has_room(tb->store_, send)) {
        if (had_trailing) {
            lb_give_back(&tb->lb_, &trailing, 1);
        }

        return false;       // the headroom is spent
    }

    static int out_lens[TB_CHUNK / 2 + 1];
    static char out_buf[TB_CHUNK];
    if (send > 0) {
        if (out_lines > 0) {
            lb_take_back(&tb->lb_, out_lens, out_lines);
        }
        cb_take_back(&tb->cb_, out_buf, send);

        if (!store_tail_push(tb->store_, out_buf, send)) {
            cb_give_back(&tb->cb_, out_buf, send);
            if (out_lines > 0) {
                lb_give_back(&tb->lb_, out_lens, out_lines);
            }
            if (had_trailing) {
                lb_give_back(&tb->lb_, &trailing, 1);
            }

            return false;
        }
        tb->tail_lines_ += out_lines;
        if (send > out_bytes) {
            trailing = 0;   // its bytes are in the tail; it carries on past memory
        }
    }
    if (had_trailing) {
        lb_give_back(&tb->lb_, &trailing, 1);
    }

    // Bounded by what goes out, as sliding down is, plus one byte of lookbehind.
    //
    // A chunk taken off the head's end starts wherever the arithmetic puts it,
    // which is usually part way through a line -- those bytes belong to a line
    // whose start is still in the head and have to go back. But it sometimes
    // lands exactly on a boundary, and then nothing needs giving back.
    //
    // The two cannot be told apart from the chunk alone: a run starting mid
    // line and a run starting at one look identical. The extra byte is what
    // distinguishes them. Without it, a chunk that landed on a boundary lost
    // its first line every time -- stranded in the head for good.
    // What went out, plus a byte of lookbehind -- and more than that when the
    // window has room going spare.
    //
    // Matching the outgo sounds conservative and is not: the chunk that comes
    // back starts part way through a line, and those bytes go back to the head
    // (see keep_at below), so a slide that pops `send + 1` keeps only
    // `send + 1 - keep_at`. That is half a line short, every time. Nothing
    // notices for a while -- and then the window has quietly shrunk below the
    // two margins it is supposed to keep, which is where the cliff was.
    //
    // Measured on slow.asm: twenty-four rounds of three thousand lines down
    // and back took a 256 KiB window from 188,178 bytes to 127,160, and a
    // 96 KiB one under two margins in seven.
    //
    // So a slide refills as well as moves. The cap is the reserve prime_spare
    // keeps for the gap: the window grows back toward as full as the buffer
    // allows and stops there, rather than draining a line at a time for ever.
    int want = (out_bytes > 0 ? out_bytes : TB_CHUNK) + 1;
    {
        const int slack = cb_available(&tb->cb_) - tbi_prime_spare(tb);
        if (slack > want) {
            want = slack;
        }
        if (want > TB_CHUNK) {
            want = TB_CHUNK;
        }
    }
    const int got = store_head_pop(tb->store_, slide_bytes, want);
    if (got <= 0) {
        return true;
    }
    int keep_at = 0;
    if (store_head_bytes(tb->store_) > 0) {
        keep_at = 1;                    // the lookbehind byte itself goes back
        if (slide_bytes[0] != '\n') {
            while (keep_at < got && slide_bytes[keep_at] != '\n') {
                keep_at++;
            }
            if (keep_at >= got) {
                store_head_rewind(tb->store_, got);

                return true;            // no break in the whole chunk
            }
            keep_at++;      // the line feed closes the line before, not after
        }
        store_head_rewind(tb->store_, keep_at);
    }
    int in_bytes = 0;
    int in_lines = run_lines(slide_bytes + keep_at, got - keep_at, slide_lens,
                             (int) (sizeof(slide_lens) / sizeof(slide_lens[0])),
                             &in_bytes);
    if (in_lines == 0 || in_bytes != got - keep_at) {
        store_head_rewind(tb->store_, got - keep_at);

        return true;        // the chunk does not end where a line does
    }

    // More lines than there are slots for, which is what a document of short
    // lines gives every time: the index holds one slot per 32 bytes, so 2 KiB
    // of ten-byte lines is two hundred lines against a hundred slots.
    //
    // The ones to keep are the ones nearest memory, and those are at the *end*
    // of the run -- a chunk off the head's end is the text directly above the
    // window. So the leading lines are the ones to drop, and their bytes go
    // back to the head.
    //
    // Trimming from the front instead is what fit_lines does, which is right
    // sliding down and wrong here; the byte count then disagreed with the
    // chunk and the whole thing was put back. The slide still reported that it
    // had moved, so settling would call it again, and again, each time
    // bringing back nothing: 185,690 bytes stayed in the head across twelve
    // slides while the cursor waited for text that never arrived.
    int drop = in_lines - lb_room(&tb->lb_);
    if (drop < 0) {
        drop = 0;
    }
    int drop_bytes = 0;
    for (int i = 0; i < drop; i++) {
        drop_bytes += slide_lens[i];
    }
    in_lines -= drop;
    in_bytes -= drop_bytes;
    if (in_lines == 0
            || !cb_give_front(&tb->cb_, slide_bytes + keep_at + drop_bytes,
                              in_bytes)) {
        store_head_rewind(tb->store_, got - keep_at);

        return true;        // still a slide: the back is in the tail
    }
    store_head_rewind(tb->store_, drop_bytes);
    lb_give_front(&tb->lb_, slide_lens + drop, in_lines);
    tb->head_lines_ -= in_lines;

    return true;
}

void tb_set_offscreen(text_buffer* tb, int head_lines, int tail_lines) {
    tb->head_lines_ = head_lines;
    tb->tail_lines_ = tail_lines;
}
