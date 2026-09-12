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

#include "text_buffer.h"

#include "undo.h"

#include <agon/vdp.h>
#include <agon/mos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

text_buffer* tb_init(text_buffer* tb, int mem_kb, const char* fname) {
    int line_count = mem_kb << 5;
    int char_count = (mem_kb << 10) - line_count;
    if (!cb_init(&tb->cb_, char_count)) {
        return NULL;
    }
    if (!lb_init(&tb->lb_, line_count)) {
        cb_destroy(&tb->cb_);
        return NULL;
    }
    tb->fname_ = (char*) malloc(TB_FNAME_MAX);
    if (tb->fname_ == NULL) {
        lb_destroy(&tb->lb_);
        cb_destroy(&tb->cb_);
        return NULL;
    }
    tb->x_ = 0;
    tb->fname_[0] = 0;
    tb->dirty_ = false;
    tb->eol_ = TB_EOL_CRLF;
    tb->undo_ = NULL;
    tb->load_dirty_ = false;
    tb->head_lines_ = 0;
    tb->tail_lines_ = 0;
    tb->walker_ = false;
    tb->paged_ = false;
    tb->store_ = NULL;

    if (fname != NULL && tb_load(tb, fname) != TB_OK) {
        free(tb->fname_);
        tb->fname_ = NULL;
        lb_destroy(&tb->lb_);
        cb_destroy(&tb->cb_);
        return NULL;
    };
    return tb;
}

void tb_destroy(text_buffer* tb) {
    if (tb != NULL && tb->paged_ && !tb->walker_) {
        store_destroy(tb->store_);
        free(tb->store_);
        tb->store_ = NULL;
        tb->paged_ = false;
    }
    cb_destroy(&tb->cb_);
    lb_destroy(&tb->lb_);
    free(tb->fname_);
    tb->fname_ = NULL;
}

// Info ops.
int tb_size(text_buffer* tb) {
    return cb_size(&tb->cb_);
}
int tb_available(text_buffer* tb) {
    return cb_available(&tb->cb_);
}
int tb_used(text_buffer* tb) {
    return cb_used(&tb->cb_);
}

#define IS_EOL(x) (x == 0 || (x >= 10 && x <= 13))

bool tb_eol(text_buffer* tb) {
    const char ch = cb_peek(&tb->cb_);
    return IS_EOL(ch);
}
bool tb_bol(text_buffer* tb) {
    return tb->x_ == 0;
}

char* tb_fname(text_buffer* tb) {
    // NULL on a copy, which has no name of its own -- and on a buffer that has
    // never been given one. Both mean the same thing to a caller.
    if (tb->fname_ == NULL || tb->fname_[0] == 0) {
        return NULL;
    }
    return tb->fname_;
}

bool tb_changed(text_buffer* tb) {
    // With a log attached the question is not "has anything happened" but "is
    // the document what the file holds" -- so undoing back to the last save
    // clears the marker, and redoing forward again brings it back.
    if (tb->undo_ != NULL) {
        return tb->load_dirty_ || !undo_at_save_point(tb->undo_);
    }

    return tb->dirty_;
}

static void tb_saved(text_buffer* tb) {
    tb->dirty_ = false;
    // Whatever the load changed is now on disk too.
    tb->load_dirty_ = false;
    undo_mark_saved(tb->undo_);
}

void tb_set_fname(text_buffer* tb, const char* fname, int sz) {
    if (sz >= TB_FNAME_MAX) {
        sz = TB_FNAME_MAX - 1;
    }
    strncpy(tb->fname_, fname, sz);
    tb->fname_[sz] = 0;
}

// Character ops.
bool tb_put(text_buffer* tb, char ch) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    // Read before the write: the record says where the byte landed, and after
    // the put the cursor has already moved past it.
    const tb_pos at = tb_tell(tb);
    if (!cb_put(&tb->cb_, ch)) {
        return false;
    }
    tb->x_++;
    lb_cinc(&tb->lb_);
    tb->dirty_ = true;
    undo_insert(tb->undo_, at, &ch, 1);

    return true;
}

bool tb_del(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    // Peeked before the delete: cb_del does not hand back what it removed, and
    // undoing a delete means putting the byte back.
    const char gone = cb_peek(&tb->cb_);
    const tb_pos at = tb_tell(tb);
    if (cb_del(&tb->cb_)) {
        lb_cdec(&tb->lb_);
        tb->dirty_ = true;
        undo_delete(tb->undo_, at, &gone, 1);
        return true;
    }
    return false;
}

bool tb_bksp(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    const char gone = cb_prev(&tb->cb_, 1);
    cb_next(&tb->cb_, 1);
    const bool ok = cb_bksp(&tb->cb_);
    if (ok) {
        tb->x_--;
        lb_cdec(&tb->lb_);
        tb->dirty_ = true;
        // Recorded at where the cursor ended up, which is where the byte was.
        // As a backward delete, so a run of them coalesces in the right order:
        // each byte is to the left of the one before it.
        undo_delete_back(tb->undo_, tb_tell(tb), &gone, 1);
    }
    return ok;
}

bool tb_newline(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    // Both halves of the CRLF must fit, or the line index and the text would
    // disagree about where the line ends.
    if (cb_available(&tb->cb_) < 2) {
        return false;
    }
    // And the line index must have room for the line the break starts. It is
    // bounded separately from the characters and runs out first on a document
    // of short lines, and lb_new failing after the CRLF was already written
    // left exactly the disagreement the check above exists to prevent. Asked of
    // the line buffer rather than worked out here, so there is one place that
    // knows how many slots a split costs.
    if (!lb_can_new(&tb->lb_)) {
        return false;
    }

    tb->dirty_ = true;

    // One record, not two. The puts below would each record a byte, and undoing
    // those separately would take the LF out on its own and leave the CR --
    // which the line index, built on a two-byte CRLF, cannot represent.
    const tb_pos at = tb_tell(tb);
    const bool was = undo_hold(tb->undo_);
    tb_put(tb, '\r');
    tb_put(tb, '\n');
    const bool ok = lb_new(&tb->lb_, tb->x_);
    if (ok) {
        tb->x_ = 0;
    }
    undo_release(tb->undo_, was);
    if (ok) {
        undo_insert(tb->undo_, at, "\r\n", 2);
    }

    return ok;
}

bool tb_del_line(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    if (lb_last(&tb->lb_) && lb_csize(&tb->lb_) == 0) {
        return false;
    }

    tb_home(tb);
    while (lb_csize(&tb->lb_) > 0) {
        tb_del(tb);
    }
    lb_del(&tb->lb_);
    tb->dirty_ = true;

    return true;
}

bool tb_del_merge(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    if (lb_last(&tb->lb_) || !tb_eol(tb)) {
       return false;
    }

    // This function is only called when we are the end of the line.
    // If we are not the last, then we have a \r\n sequence.
    //
    // One record for the pair, for the same reason tb_newline groups its puts:
    // half a line break is not a thing the line index can hold.
    const tb_pos at = tb_tell(tb);
    const bool was = undo_hold(tb->undo_);
    tb_del(tb);
    tb_del(tb);
    lb_merge_next(&tb->lb_);
    tb->dirty_ = true;
    undo_release(tb->undo_, was);
    undo_delete(tb->undo_, at, "\r\n", 2);

    return true;
}

bool tb_bksp_merge(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    if (!tb_bol(tb) || tb_ypos(tb) == 1) {
        return false;
    }
    // Straight to cb_bksp rather than tb_bksp, because the line bookkeeping
    // below is not what tb_bksp does -- so this is the one mutation that has to
    // record for itself. The two bytes are the CRLF ending the previous line.
    cb_bksp(&tb->cb_);
    cb_bksp(&tb->cb_);

    tb->x_ = lb_merge_prev(&tb->lb_);
    tb->dirty_ = true;
    undo_delete(tb->undo_, tb_tell(tb), "\r\n", 2);
    return true;
}


// Cursor ops.
char tb_next(text_buffer* tb) {
    tb->x_++;
    return cb_next(&tb->cb_, 1);
}

static bool isstop(char ch) {
    switch (ch) {
        case '[':
        case ']':
        case '(':
        case ')':
        case '<':
        case '>':
        case ' ':
        case '\t':
        case ';':
        case ':':
        case '.':
        case ',':
        case '@':
        case '!':
        case '#':
        case '\\':
        case '/':
            return true;
        default:
            return false;
    }
    return false;
}

// ASCII case folding. The Agon's character set beyond 127 is not a case-mapped
// alphabet, so anything else is left alone rather than guessed at.
static char fold(char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (char) (ch + ('a' - 'A')) : ch;
}

static bool match_at(const char* hay, const char* needle, int nsz) {
    for (int i = 0; i < nsz; i++) {
        if (fold(hay[i]) != fold(needle[i])) {
            return false;
        }
    }

    return true;
}

// Where `needle` sits in one line, or -1. `from` is the first index tried going
// forward, or the last one going backward; negative means the whole line.
static int scan_line(const char* hay, int hsz, const char* needle, int nsz,
                     int from, bool forward) {
    if (hay == NULL || nsz > hsz) {
        return -1;
    }
    const int last = hsz - nsz;

    // The needle's first character, folded once. Almost every position in a
    // document fails on it, and match_at is a real call -- a call, a frame and
    // a return -- so asking it was costing one of those per byte of the
    // document. Testing the cheapest term first and only then paying for the
    // rest is what a search should do; here it is the whole of the work.
    //
    // Both loop bounds are unsigned. i and last are both non-negative -- the
    // nsz > hsz check above is what makes last so -- and a signed comparison on
    // this target carries a `call pe, __setflag` to repair the flags, which an
    // unsigned one does not.
    const char n0 = fold(needle[0]);
    if (forward) {
        const unsigned stop = (unsigned) last;
        for (unsigned i = (unsigned)(from < 0 ? 0 : from); i <= stop; i++) {
            if (fold(hay[i]) == n0 && match_at(hay + i, needle, nsz)) {
                return (int) i;
            }
        }
    } else {
        for (unsigned i = (unsigned)((from < 0 || from > last) ? last : from) + 1;
             i-- != 0; ) {
            if (fold(hay[i]) == n0 && match_at(hay + i, needle, nsz)) {
                return (int) i;
            }
        }
    }

    return -1;
}

bool tb_find(text_buffer* tb, const char* needle, int nsz, tb_pos from,
             bool forward, tb_pos* at) {
    if (needle == NULL || nsz <= 0 || at == NULL) {
        return false;
    }

    // On a copy, which is safe because moving a gap duplicates rather than
    // destroys: going forward copies high bytes down, going back copies low
    // bytes up, and in both cases the bytes outside the moved region keep their
    // values. The real cursor reads the same before and after.
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_seek(&cp, (tb_pos){from.line, 0});

    const int total = tb_ymax(tb);
    int line = from.line;
    int start = from.x;
    bool first = true;

    // One pass per line, plus one more for the part of the starting line the
    // first pass skipped over.
    for (int n = 0; n <= total; n++) {
        int sz = 0;
        const char* text = tb_suffix(&cp, &sz);
        // A caller searching backwards hands us x - 1, which is negative when
        // the cursor sits in column 0 -- and that means nothing on this line is
        // behind it, not that the whole line is fair game.
        const int hit = (first && !forward && start < 0)
            ? -1
            : scan_line(text, sz, needle, nsz, first ? start : -1, forward);
        first = false;
        if (hit >= 0) {
            at->line = line;
            at->x = hit;

            return true;
        }

        int next = forward ? line + 1 : line - 1;
        const bool wrapped = next > total || next < 1;
        if (wrapped) {
            next = forward ? 1 : total;
            // Only the wrap seeks. Stepping a line at a time keeps the whole
            // sweep linear; a seek per line would move the gap from wherever it
            // is on every one of them.
            tb_seek(&cp, (tb_pos){next, 0});
        } else if (forward) {
            tb_down(&cp);
            tb_home(&cp);
        } else {
            tb_up(&cp);
            tb_home(&cp);
        }
        line = next;
    }

    return false;
}

bool tb_is_word_stop(char ch) {
    return isstop(ch);
}

#define KEEP_SKIPPING(from_stopch, ch) \
    (!IS_EOL(ch) && ( \
        (from_stopch && isstop(ch)) || \
        (!from_stopch && !isstop(ch)) \
    ))

char tb_w_next(text_buffer* tb, char from_ch) {
    const bool stopch = isstop(from_ch);
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
    const bool stopch = isstop(from_ch);
    char ch = 0;
    do {
        ch = cb_prev(&tb->cb_, 1);
        tb->x_--;
    } while (tb->x_ > 0 && KEEP_SKIPPING(stopch, ch));

    return ch;
}

char tb_up(text_buffer* tb) {
    if (!lb_up(&tb->lb_)) {
        return 0;
    }

    const int  sz = lb_csize(&tb->lb_);
    const int maxX = sz - 2;
    int back = sz + tb->x_;
    if (maxX < tb->x_) {
        tb->x_ = maxX;
    }

    return cb_prev(&tb->cb_, back - tb->x_);
}

char tb_down(text_buffer* tb) {
    int move = lb_csize(&tb->lb_) - tb->x_;
    if (!lb_down(&tb->lb_)) {
        return 0;
    }
    int cend = lb_csize(&tb->lb_);
    if (!lb_last(&tb->lb_)) {
        cend -= 2;
    }
    if (tb->x_ > cend) {
        tb->x_ = cend;
    }
    return cb_next(&tb->cb_, move + tb->x_);
}

char tb_home(text_buffer* tb) {
    const int back = tb->x_;
    tb->x_ = 0;
    return cb_prev(&tb->cb_, back);
}

char tb_goto_offset(text_buffer* tb, int off) {
    if (off < 0) {
        off = 0;
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
    return tb->head_lines_ + lb_curr(&tb->lb_) + 1;
}

int tb_ymax(text_buffer* tb) {
    return tb->head_lines_ + mem_lines(tb) + tb->tail_lines_;
}

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
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            tb->tail_lines_++;
        }
    }

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
static int run_lines(const char* buf, int n, int* lens, int max, int* bytes) {
    int lines = 0;
    int at = 0;
    int used = 0;
    for (; at < n && lines < max; at++) {
        if (buf[at] == '\n') {
            lens[lines++] = at - used + 1;
            used = at + 1;
        }
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
    while (guard-- > 0) {
        int psz = 0;
        int ssz = 0;
        cb_prefix(&tb->cb_, &psz);
        cb_suffix(&tb->cb_, &ssz);

        if (ssz < TB_MARGIN && store_tail_bytes(tb->store_) > 0) {
            if (!tb_slide_down(tb)) {
                break;
            }
            moved = true;
            continue;
        }
        if (psz < TB_MARGIN && store_head_bytes(tb->store_) > 0) {
            if (!tb_slide_up(tb)) {
                break;
            }
            moved = true;
            continue;
        }
        break;
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

bool tb_page_prime(text_buffer* tb) {
    if (tb == NULL || !tb->paged_ || tb->walker_) {
        return false;
    }
    // Room kept back so the first slide has somewhere to put what it brings in,
    // and so the margins have something to be margins of. A share of the buffer
    // rather than a fixed amount: two chunks is right for the 248 KiB the
    // editor runs with and larger than the whole of a small one, and a reserve
    // bigger than the buffer fills nothing at all.
    int spare = cb_size(&tb->cb_) / 4;
    if (spare > TB_CHUNK * 2) {
        spare = TB_CHUNK * 2;
    }

    // An empty buffer is not nothing: it is one line, of no length, and that
    // line belongs at the *end* of what gets filled in -- it is the line the
    // document's last break opens, or the one that carries on into the tail.
    // Entries given at the back arrive after it, which leaves it in front of
    // the first real line and every line number one out.
    //
    // So it is moved to the back afterwards: one empty entry is appended and
    // lb_del pulls the first real line into the slot the cursor is on. The
    // count comes out the same, which is the point -- dropping the empty line
    // instead loses a line from the document.
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
        int bytes = 0;
        int lines = run_lines(slide_bytes, got, slide_lens,
                              (int) (sizeof(slide_lens) / sizeof(slide_lens[0])),
                              &bytes);
        // One slot held back for the trailing entry this puts on at the end.
        lines = fit_lines(slide_lens, lines, lb_room(&tb->lb_) - 1, &bytes);
        if (lines == 0) {
            // Either a line longer than a chunk, which nothing can hold a
            // chunk at a time, or the index is full. Both stop the filling
            // rather than fail it: what is in memory is sound either way.
            store_tail_rewind(tb->store_, got);
            break;
        }
        if (got > bytes) {
            store_tail_rewind(tb->store_, got - bytes);
        }
        // Checked before either is touched. Doing one and finding the other
        // will not go leaves bytes in memory with no entry describing them,
        // which is a document that reads as gibberish from there on.
        if (!cb_give_back(&tb->cb_, slide_bytes, bytes)) {
            store_tail_rewind(tb->store_, bytes);
            break;
        }
        lb_give_back(&tb->lb_, slide_lens, lines);
        tb->tail_lines_ -= lines;
        gave = true;
    }
    if (was_empty && gave) {
        static const int trailing = 0;
        if (lb_give_back(&tb->lb_, &trailing, 1)) {
            lb_del(&tb->lb_);   // the first real line becomes the cursor's
        }
    }

    return true;
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
    int out_lines = 0;
    const int out_bytes = lb_front_fit(&tb->lb_, TB_CHUNK, &out_lines);
    if (out_lines == 0) {
        return false;       // the first line is longer than a chunk
    }

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
    const int got = store_tail_pop(tb->store_, slide_bytes, out_bytes);
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
    static const int fresh = 0;
    if (had_trailing) {
        lb_give_back(&tb->lb_, slide_lens, in_lines);
        lb_give_back(&tb->lb_, &trailing, 1);
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

    int out_lines = 0;
    const int out_bytes = lb_back_fit(&tb->lb_, TB_CHUNK, &out_lines);
    if (out_lines == 0 || !store_tail_has_room(tb->store_, out_bytes)) {
        if (had_trailing) {
            lb_give_back(&tb->lb_, &trailing, 1);
        }

        return false;       // nothing to send, or the headroom is spent
    }

    static int out_lens[TB_CHUNK / 2 + 1];
    static char out_buf[TB_CHUNK];
    lb_take_back(&tb->lb_, out_lens, out_lines);
    cb_take_back(&tb->cb_, out_buf, out_bytes);

    if (!store_tail_push(tb->store_, out_buf, out_bytes)) {
        cb_give_back(&tb->cb_, out_buf, out_bytes);
        lb_give_back(&tb->lb_, out_lens, out_lines);
        if (had_trailing) {
            lb_give_back(&tb->lb_, &trailing, 1);
        }

        return false;
    }
    if (had_trailing) {
        lb_give_back(&tb->lb_, &trailing, 1);
    }
    tb->tail_lines_ += out_lines;

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
    const int got = store_head_pop(tb->store_, slide_bytes, out_bytes + 1);
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
    in_lines = fit_lines(slide_lens, in_lines, lb_room(&tb->lb_), &in_bytes);
    if (in_lines == 0 || in_bytes != got - keep_at
            || !cb_give_front(&tb->cb_, slide_bytes + keep_at, in_bytes)) {
        store_head_rewind(tb->store_, got - keep_at);

        return true;        // still a slide: the back is in the tail
    }
    lb_give_front(&tb->lb_, slide_lens, in_lines);
    tb->head_lines_ -= in_lines;

    return true;
}

void tb_set_offscreen(text_buffer* tb, int head_lines, int tail_lines) {
    tb->head_lines_ = head_lines;
    tb->tail_lines_ = tail_lines;
}

// --- positions and ranges ---

// The line's text, not counting the CRLF that ends it. The last line has none.
static int line_len(text_buffer* tb) {
    const int sz = lb_csize(&tb->lb_);

    return lb_last(&tb->lb_) ? sz : sz - 2;
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

int tb_range_size(text_buffer* tb, tb_pos a, tb_pos b) {
    order(&a, &b);
    if (a.line == b.line) {
        const int n = b.x - a.x;

        return n > 0 ? n : 0;
    }

    // Walked on a copy: tb_copy aliases the same buffers, so this reads the
    // document without disturbing where the real cursor is.
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_seek(&cp, a);

    int total = line_len(&cp) - a.x + 2;   // the rest of the line, and its CRLF
    int prev = -1;
    while (tb_ypos(&cp) < b.line && tb_ypos(&cp) != prev) {
        prev = tb_ypos(&cp);
        tb_down(&cp);
        if (tb_ypos(&cp) >= b.line) {
            break;
        }
        total += line_len(&cp) + 2;
    }
    total += b.x;

    return total > 0 ? total : 0;
}

bool tb_range_walk(text_buffer* tb, tb_pos a, tb_pos b, tb_sink sink, void* ctx) {
    static const char crlf[2] = { '\r', '\n' };

    order(&a, &b);
    int left = tb_range_size(tb, a, b);
    if (left <= 0) {
        return true;    // nothing to send is not a failure
    }

    // Walked on a copy: tb_copy aliases the same buffers, so the real cursor
    // does not move.
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_seek(&cp, a);

    while (left > 0) {
        int sz = 0;
        const char* line = tb_suffix(&cp, &sz);
        const int take = sz < left ? sz : left;
        if (take > 0 && !sink(ctx, line, take)) {
            return false;
        }
        left -= take;

        if (left <= 0) {
            break;
        }
        // What is left of the range runs past this line, so the break goes in.
        if (!sink(ctx, crlf, 2)) {
            return false;
        }
        left -= 2;

        const int prev = tb_ypos(&cp);
        tb_down(&cp);
        if (tb_ypos(&cp) == prev) {
            break;      // ran out of document
        }
        tb_home(&cp);
    }

    return true;
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
    bool any = false;
    while (left > 0) {
        if (tb_eol(tb)) {
            if (!tb_del_merge(tb)) {
                break;   // last line: nothing left to join to
            }
            left -= 2;
        } else {
            if (!tb_del(tb)) {
                break;
            }
            left -= 1;
        }
        any = true;
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
            needed += 2;
            breaks++;
            i++;
        } else if (buf[i] == '\n' || buf[i] == '\r') {
            needed += 2;
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
    dst->lb_.buf_ = src->lb_.buf_;
    dst->lb_.curr_ = src->lb_.curr_;
    dst->lb_.cend_ = src->lb_.cend_;
    dst->lb_.size_ = src->lb_.size_;

    dst->cb_.buf_ = src->cb_.buf_;
    dst->cb_.curr_ = src->cb_.curr_;
    dst->cb_.cend_ = src->cb_.cend_;
    dst->cb_.size_ = src->cb_.size_;

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
    dst->eol_ = src->eol_;
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
    return cb_peek(&tb->cb_);
}

char* tb_prefix(text_buffer* tb, int* sz) {
    int psz = 0;
    char* prefix = cb_prefix(&tb->cb_, &psz);
    if (prefix == NULL) {
        return NULL;
    }
    prefix = prefix + (psz - tb->x_);
    *sz = tb->x_;
    return prefix;
}

char* tb_suffix(text_buffer* tb, int* sz) {
    char* suffix = cb_suffix(&tb->cb_, sz);
    if (suffix == NULL) {
        return NULL;
    }

    *sz = lb_csize(&tb->lb_) - tb->x_;
    if (!lb_last(&tb->lb_)) {
        *sz -= 2;
    }
    return suffix;
}

split_line tb_curr_line(text_buffer* tb) {
    split_line ln;

    ln.prefix_ = tb_prefix(tb, &ln.psz_);
    ln.suffix_ = tb_suffix(tb, &ln.ssz_);
    return ln;
}

static void tb_content(text_buffer* tb, char** prefix, int* psz, char** suffix, int* ssz) {
    *prefix = cb_prefix(&tb->cb_, psz);
    *suffix = cb_suffix(&tb->cb_, ssz);
}

static int ensure_newline(char_buffer* cb, line_buffer* lb) {
    int added = 0;

    // Look at the character behind the cursor and put the cursor back where it
    // was. Only put back what was actually taken: at the very start of the
    // buffer there is nothing behind the cursor, cb_prev moves nothing, and an
    // unconditional cb_next would walk the cursor *forward* over a byte nobody
    // asked it to pass. A file whose first character is a line feed does that
    // on its first byte, and every byte after it is then one out.
    char* const was = cb->curr_;
    const char pch = cb_prev(cb, 1);
    if (cb->curr_ != was) {
        cb_next(cb, 1);
    }

    if (pch != '\r') {
        if (!cb_put(cb, '\r')) {
            // Without the CR this ending is a bare LF, but every consumer of
            // the line index assumes a two-byte CRLF -- tb_suffix and tb_up
            // both subtract 2. Recording a boundary here would put navigation
            // permanently off by one, so leave the index alone.
            return 0;
        }
        lb_cinc(lb);
        added++;
    }

    lb_new(lb, lb_csize(lb));

    return added;
}

static bool tb_read(char fh, text_buffer* tb, int sz) {
    // In order to read the file to the text buffer, we move cend_ sz postions and then
    // pass it + sz as the buffer to read.
    char_buffer* cb = &tb->cb_;
    cb->cend_ -= sz;
    const unsigned got = mos_fread(fh, (char*)cb->cend_, (unsigned) sz);
    if (got != (unsigned) sz) {
        // Whatever was not read is still whatever happened to be in that
        // memory. Carrying on would index it as document text and hand it back
        // as the file's contents, which is worse than admitting the read
        // failed. Give the space back and let the caller say so.
        cb->cend_ += sz;

        return false;
    }

    //  Now update the line buffer. Tabs are kept as-is -- they are one byte in
    //  the document and the view decides how wide they render. Only line
    //  endings are normalised, since the line index assumes a two-byte CRLF.
    // `added` counts the CRs put in front of a bare LF, `crlf` the breaks that
    // already had one. Together they say what the file's endings were, which is
    // what decides how it goes back out.
    //
    // The walk is written out here rather than made of calls to cb_peek,
    // cb_next and lb_cinc. Those are three external calls for every byte of the
    // file -- a call, a frame and a return each, none of which the compiler can
    // inline across translation units -- and on a 64 KB document that was the
    // whole of a three-second load. The steps are the same ones those functions
    // take; only the call is gone.
    //
    // The cursor is put back into the buffer around ensure_newline, which works
    // on the structure and is reached once a line rather than once a byte.
    int added = 0;
    int crlf = 0;
    char* curr = cb->curr_;
    char* cend = cb->cend_;
    int* lcur = tb->lb_.curr_;
    int llen = *lcur;

    int left = sz;

    while (left != 0) {
        // The run up to the next line feed, found and moved whole. memchr is a
        // CPIR on this machine and memmove an LDIR -- block instructions that
        // do a byte a cycle or two -- where testing and copying a byte at a
        // time in C is a dozen instructions each. Only the line feed itself is
        // handled one at a time, and there is one of those per line.
        const char* nl = (const char*) memchr(cend, '\n', (size_t) left);
        const int run = nl != NULL ? (int) (nl - cend) : left;
        if (run != 0) {
            memmove(curr, cend, (size_t) run);
            curr += run;
            cend += run;
            llen += run;
            left -= run;
        }
        if (nl == NULL) {
            break;
        }

        llen++;                 // the line feed, counted before it is passed
        cb->curr_ = curr;
        cb->cend_ = cend;
        *lcur = llen;
        const int n = ensure_newline(&tb->cb_, &tb->lb_);
        if (n == 0) {
            crlf++;
        }
        added += n;
        curr = cb->curr_;
        cend = cb->cend_;
        lcur = tb->lb_.curr_;
        llen = *lcur;

        *curr++ = *cend++;
        left--;
    }
    cb->curr_ = curr;
    cb->cend_ = cend;
    *lcur = llen;

    if (cb_peek(cb) == '\n') {
        const int n = ensure_newline(&tb->cb_, &tb->lb_);
        if (n == 0) {
            crlf++;
        }
        added += n;
    }

    cb_prev(cb, sz+added);

    // A file whose breaks were all bare LFs is written back as bare LFs, so
    // opening it and saving it leaves it byte for byte as it was. That is what
    // lets it be clean on open: nothing the user did, and nothing a save would
    // do, has changed it.
    //
    // A file with both kinds cannot have that. It goes back out as CRLF, which
    // rewrites its LF-only lines -- a real change to the file, so it opens
    // dirty and the exit prompt is telling the truth.
    if (added > 0 && crlf == 0) {
        tb->eol_ = TB_EOL_LF;
        tb->dirty_ = false;
    } else {
        tb->eol_ = TB_EOL_CRLF;
        tb->dirty_ = added != 0;
    }
    // Remembered separately from dirty_: this one cannot be undone away, since
    // the rewrite happened before there was a log to record it.
    tb->load_dirty_ = tb->dirty_;

    // Now move the line buffer back to the first line.
    while (lb_up(&tb->lb_)) ;
    return true;
}


// Reads a document too big for memory into the store, normalising its line
// endings on the way.
//
// The same normalisation tb_read does, but streaming: a chunk at a time, with
// the one piece of state that cannot live inside a chunk -- whether the last
// byte of the previous one was a carriage return, which decides whether the
// line feed opening this one already has its pair.
//
// The output can be twice the input, in a file of nothing but bare line feeds,
// which is why there are two buffers rather than one.
static bool tb_load_paged(text_buffer* tb, char fh, int size) {
    static char in[TB_CHUNK];
    static char out[TB_CHUNK * 2];

    if (!tb_page_open(tb, tb->fname_)) {
        return false;
    }

    int left = size;
    bool pending_cr = false;
    int added = 0;
    int crlf = 0;

    while (left > 0) {
        const int want = left < TB_CHUNK ? left : TB_CHUNK;
        const int got = (int) mos_fread(fh, in, (unsigned) want);
        if (got <= 0) {
            return false;
        }
        int n = 0;
        for (int i = 0; i < got; i++) {
            const char c = in[i];
            if (c == '\n') {
                if (pending_cr) {
                    crlf++;         // it already had its carriage return
                } else {
                    out[n++] = '\r';
                    added++;
                }
                pending_cr = false;
            } else {
                pending_cr = (c == '\r');
            }
            out[n++] = c;
        }
        if (!tb_page_fill(tb, out, n)) {
            return false;
        }
        left -= got;
    }

    tb_page_prime(tb);

    // A file whose breaks were all bare line feeds goes back out the same way,
    // so opening and saving it leaves it byte for byte as it was -- which is
    // what lets it be clean on open. One with both kinds cannot have that, and
    // opens dirty because a save really will rewrite it.
    if (added > 0 && crlf == 0) {
        tb->eol_ = TB_EOL_LF;
        tb->dirty_ = false;
    } else {
        tb->eol_ = TB_EOL_CRLF;
        tb->dirty_ = added != 0;
    }
    tb->load_dirty_ = tb->dirty_;

    return true;
}

tb_result tb_load(text_buffer* tb, const char* fname) {
    if (fname == NULL) {
        return TB_NO_FILE;
    }

    // Clamped, as tb_set_fname does. This name comes from argv, and copying
    // strlen(fname) bytes into a fixed buffer was an overflow waiting for
    // someone to type a long enough path.
    int fsz = strlen(fname);
    if (fsz >= TB_FNAME_MAX) {
        fsz = TB_FNAME_MAX - 1;
    }
    strncpy(tb->fname_, fname, fsz);
    tb->fname_[fsz] = 0;

    char fh = mos_fopen(tb->fname_, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
    if (fh == 0) {
        // Try to create the file.
        fh = mos_fopen(tb->fname_, FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
        if (fh == 0) {
            tb->fname_[0] = 0;

            return TB_NO_FILE;
        }
    }
    FIL* fil = mos_getfil(fh);
    if (fil == NULL) {
        mos_fclose(fh);

        return TB_NO_FILE;
    }

    bool ok = true;

    // Compared before narrowing: objsize is 32 bits and the eZ80's int is 24,
    // so a file over 8MB would arrive here as a small or negative number and
    // walk straight past a signed check.
    //
    // Too big for memory is no longer too big to open. It goes to the store
    // instead and memory holds a window on it -- which is the whole of what
    // .internal/docs/PAGING.md is for. What is still refused is a file too big
    // for the arithmetic: 8 MB is where a size stops fitting in this machine's
    // int, and nothing below that line can be trusted about it.
    if (fil->obj.objsize > (uint32_t) 0x7FFFFF) {
        mos_fclose(fh);
        tb->fname_[0] = 0;

        return TB_TOO_LARGE;
    }
    if (fil->obj.objsize > (uint32_t) cb_available(&tb->cb_)) {
        const bool paged = tb_load_paged(tb, fh, (int) fil->obj.objsize);
        mos_fclose(fh);
        if (!paged) {
            tb->fname_[0] = 0;

            return TB_TOO_LARGE;
        }

        return TB_OK;
    }
    const int sz = (int) fil->obj.objsize;
    if (sz > 0) {
       ok = tb_read(fh, tb, sz);
    }
    mos_fclose(fh);

    return ok ? TB_OK : TB_NO_FILE;
}

void tb_clear(text_buffer* tb) {
    cb_clear(&tb->cb_);
    lb_clear(&tb->lb_);
    tb->x_ = 0;
    tb->dirty_ = false;
    tb->load_dirty_ = false;
    // Nothing is anywhere else once there is no document.
    tb->head_lines_ = 0;
    tb->tail_lines_ = 0;
    // A different document, or none. The records describe text that is no
    // longer there and their positions point into it.
    undo_clear(tb->undo_);
    // An emptied document has no endings to preserve, so it takes the platform
    // default rather than keeping the last file's.
    tb->eol_ = TB_EOL_CRLF;
}

tb_result tb_open(text_buffer* tb, const char* fname, int sz) {
    if (fname == NULL || sz <= 0) {
        return TB_NO_FILE;
    }

    // Static: 256 bytes of name on the stack would put this frame past the
    // 128 bytes an ix displacement reaches, and charge every other local for it.
    static char name[TB_FNAME_MAX];
    if (sz >= (int) sizeof(name)) {
        sz = sizeof(name) - 1;
    }
    strncpy(name, fname, sz);
    name[sz] = 0;

    char fh = mos_fopen(name, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
    if (fh == 0) {
        fh = mos_fopen(name, FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
        if (fh == 0) {
            return TB_NO_FILE;
        }
    }
    FIL* fil = mos_getfil(fh);
    if (fil == NULL) {
        mos_fclose(fh);

        return TB_NO_FILE;
    }

    // Measured against the whole buffer, not what is free in it right now: the
    // document on screen is about to be discarded, so its bytes are not in the
    // way. Checking before discarding anything is the point -- a file that will
    // not fit must leave the editor exactly as it was.
    // Compared before narrowing, for the same reason as in tb_load: objsize is
    // 32 bits wide and the eZ80's int is 24, so a file over 8MB narrows to a
    // small or negative number and sails past a signed comparison -- taking the
    // document with it, since the clear happens next.
    if (fil->obj.objsize > (uint32_t) cb_size(&tb->cb_)) {
        mos_fclose(fh);

        return TB_TOO_LARGE;
    }
    const int fsz = (int) fil->obj.objsize;

    tb_clear(tb);
    memcpy(tb->fname_, name, (size_t) sz + 1);

    // tb_read normalises line endings through the same primitives an edit uses.
    // tb_load gets away with not caring because it runs before the editor
    // attaches a log; this does not, and without the hold the first undo would
    // unpick the file's own CRLFs.
    const bool was = undo_hold(tb->undo_);
    bool ok = true;
    if (fsz > 0) {
        ok = tb_read(fh, tb, fsz);
    }
    undo_release(tb->undo_, was);
    mos_fclose(fh);

    return ok ? TB_OK : TB_NO_FILE;
}

// Writes the document with the CR of every CRLF dropped, so a file that came in
// with bare LFs goes back out with them. Buffered because doing it a byte at a
// time would be one MOS call per character.
//
// Only a CR that is immediately followed by an LF is dropped. A lone CR is text
// as far as this editor is concerned -- an LF-only file can still contain one,
// and it survives the round trip.
typedef struct _lf_writer {
    char fh;
    int n;
    char buf[256];
} lf_writer;

static void lfw_flush(lf_writer* w) {
    if (w->n > 0) {
        mos_fwrite(w->fh, w->buf, (unsigned) w->n);
        w->n = 0;
    }
}

static void lfw_put(lf_writer* w, char c) {
    if (w->n == (int) sizeof(w->buf)) {
        lfw_flush(w);
    }
    w->buf[w->n++] = c;
}

// The document is two segments with the gap between them, at wherever the cursor
// happens to be. This walks them as one stream so that where the split fell is
// not something the CR test has to care about.
//
// Cursor movement steps over a break two bytes at a time, so the gap is not
// thought to be able to land between a CR and its LF -- a test that breaks only
// that case fails nothing. The lookahead spans the segments anyway: it costs one
// comparison, and the alternative is this staying correct only for as long as
// that remains true of every edit path.
static void tb_write_lf(char fh, const char* pre, int psz,
                        const char* suf, int ssz) {
    // Static: an lf_writer is a 256-byte buffer, and on the stack it puts this
    // frame past the 128 bytes an ix displacement reaches -- which is charged
    // to every local the function has, not just the buffer. One save at a time.
    static lf_writer w;
    w.fh = fh;
    w.n = 0;

    const int total = psz + ssz;
    for (int i = 0; i < total; i++) {
        const char c = i < psz ? pre[i] : suf[i - psz];
        if (c == '\r') {
            const int j = i + 1;
            const char nx = j < psz ? pre[j] : (j < total ? suf[j - psz] : 0);
            if (nx == '\n') {
                continue;
            }
        }
        lfw_put(&w, c);
    }
    lfw_flush(&w);
}

bool tb_save(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    if (!tb_valid_file(tb)) {
        return false;
    }

    char fh = mos_fopen(tb->fname_, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return false;
    }

    // The prefix and the suffix are the document, in order: the gap between
    // them holds no live text, so saving is just the two segments back to back.
    char* prefix = NULL;
    char* suffix = NULL;
    int psz = 0;
    int ssz = 0;
    tb_content(tb, &prefix, &psz, &suffix, &ssz);

    if (tb->eol_ == TB_EOL_LF) {
        tb_write_lf(fh, prefix, prefix != NULL ? psz : 0,
                    suffix, suffix != NULL ? ssz : 0);
    } else {
        if (prefix != NULL && psz > 0) {
            mos_fwrite(fh, prefix, psz);
        }
        if (suffix != NULL && ssz > 0) {
            mos_fwrite(fh, suffix, ssz);
        }
    }

    mos_fclose(fh);
    tb_saved(tb);

    return true;
}

bool tb_valid_file(text_buffer* tb) {
    return tb->fname_ != NULL && tb->fname_[0] != 0;
}
