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
 * Files: reading a document in and writing it back out.
 *
 * Two ways in -- straight into memory when it fits, a chunk at a time into the
 * store when it does not -- and the same two out. Both decide the document's
 * break length on the way past, which is what break_len_agrees is for.
 */
#include "text_buffer.h"
#include "text_buffer_int.h"
#include "undo.h"

#include <agon/mos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tb_content(text_buffer* tb, char** prefix, int* psz, char** suffix, int* ssz) {
    *prefix = cb_prefix(&tb->cb_, psz);
    *suffix = cb_suffix(&tb->cb_, ssz);
}

// How long the break just written is. The byte in front of the line feed has
// already been copied, so a break is two bytes when that byte is a carriage
// return and one when it is not. An empty first line has no byte in front of
// it at all, which makes it a bare feed, correctly.
static int break_len_at(const char* text, int n) {
    return (n > 0 && text[n - 1] == '\r') ? 2 : 1;
}

// One document, one break length. The first break decides it and every break
// after has to agree; a file holding both kinds is reported through `mixed` so
// the caller can read it again and normalise the lot.
//
// Worth having in one place. Five separate pieces of code once assumed a break
// was two bytes -- deleting one, backspacing over one, merging the lines either
// side of one, counting a range in them, and undoing a delete that spanned one
// -- and every one of them took a byte too many from a document of bare feeds.
// The number they all read is the one set here.
static bool break_len_agrees(text_buffer* tb, int* seen, int len, bool* mixed) {
    if (*seen == 0) {
        *seen = len;
        tb->elen_ = len;

        return true;
    }
    if (*seen != len) {
        *mixed = true;

        return false;
    }

    return true;
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

    if (!lb_new(lb, lb_csize(lb))) {
        // The index is full. Every line from here on would be merged into the
        // one the cursor is on -- silently, because the bytes are all still
        // there and only the structure describing them is missing. A 200 KB
        // file of 10,000 short lines opened saying it had 8,192 and put the
        // last 1,809 into a single 36,180 byte line.
        return -1;
    }

    return added;
}

// `full` is set when the load stopped because the line index ran out rather
// than because anything went wrong with the file. The caller starts again with
// the paged loader, which has somewhere to put the lines that will not fit.
// See tb_load_paged_pass: `normalise` false takes the file's breaks as they
// are and lets the first one say how long a break is in this document; a file
// with both kinds says so in `mixed` and is read again with it set.
static bool tb_read_pass(char fh, text_buffer* tb, int sz, bool* full,
                         bool normalise, bool* mixed) {
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
    //  the document and the view decides how wide they render. Line endings are
    //  kept as they are too, unless this is the second pass over a file that
    //  had both kinds.
    // `added` counts the CRs put in front of a bare LF, which is how a
    // normalising pass knows whether it changed the file.
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
    int seen = 0;           // the break length this document turned out to have
    char* curr = cb->curr_;

    *mixed = false;
    tb->elen_ = 2;
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
        int n = 0;
        if (normalise) {
            n = ensure_newline(&tb->cb_, &tb->lb_);
            added += n;
        } else {
            const int len = break_len_at(cb->buf_, (int) (curr - cb->buf_));
            if (!break_len_agrees(tb, &seen, len, mixed)) {
                return false;
            }
            n = lb_new(&tb->lb_, lb_csize(&tb->lb_)) ? 0 : -1;
        }
        if (n < 0) {
            *full = true;

            return false;
        }
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
        if (n < 0) {
            *full = true;

            return false;
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
    if (normalise) {
        tb->dirty_ = added != 0;
    } else {
        tb->dirty_ = false;
    }
    // Remembered separately from dirty_: this one cannot be undone away, since
    // the rewrite happened before there was a log to record it.
    tb->load_dirty_ = tb->dirty_;

    // Now move the line buffer back to the first line.
    while (lb_up(&tb->lb_)) ;
    return true;
}

static bool tb_read(char fh, text_buffer* tb, int sz, bool* full) {
    bool mixed = false;
    if (tb_read_pass(fh, tb, sz, full, false, &mixed)) {
        return true;
    }
    if (*full || !mixed) {
        return false;
    }

    // Both kinds of break. Start again from the top of the file, converting.
    tb_clear(tb);
    if (mos_flseek(fh, 0) != 0) {
        return false;
    }

    return tb_read_pass(fh, tb, sz, full, true, &mixed);
}

/*
 * One read of a document too big for memory into the window and the store.
 *
 * The same walk tb_read does, streaming: a chunk at a time, with the one piece
 * of state that cannot live inside a chunk -- whether the last byte of the
 * previous one was a carriage return, which decides whether the line feed
 * opening this one already has its pair.
 *
 * `normalise` false is the ordinary way: the bytes go in exactly as they are,
 * and the first line break says whether this document's breaks are one byte or
 * two. That is what lets a file of bare line feeds be opened, edited and saved
 * without a byte of it being converted either way.
 *
 * A file with both kinds of break cannot be held that way, because the line
 * index keeps one break length for the whole document. The pass gives up when
 * it meets the second kind and says so in `mixed`, and the caller reads it
 * again with `normalise` set, which turns every break into a CRLF -- the one
 * case that still costs a conversion, and the one that has always opened dirty
 * because saving really does rewrite it.
 */
static bool tb_load_paged_pass(text_buffer* tb, char fh, int size,
                               bool normalise, bool* mixed) {
    static char in[TB_CHUNK];
    // A chunk, doubled because every bare line feed gains a carriage return,
    // and a chunk again for the half line held over from the round before.
    static char out[TB_CHUNK * 3];

    if (!tb_page_open(tb, tb->fname_)) {
        return false;
    }

    // Memory is filled from the read itself, not from the tail afterwards.
    // Writing the whole document out and reading the window straight back in
    // is a buffer's worth of each -- half a megabyte of pointless card traffic
    // on a 419 KiB file, and the largest single piece of what was left of the
    // open cost after the handle was held. Once memory is full the rest goes
    // to the tail, and it stays that way for the remainder of the read: going
    // back would put later text in front of earlier.
    const int spare = tbi_prime_spare(tb);
    const bool was_empty = cb_used(&tb->cb_) == 0;
    bool filling = true;
    bool gave = false;
    int carry = 0;

    int left = size;
    bool pending_cr = false;
    int added = 0;
    int seen = 0;           // the break length this document turned out to have

    *mixed = false;
    tb->elen_ = 2;

    while (left > 0) {
        const int want = left < TB_CHUNK ? left : TB_CHUNK;
        const int got = (int) mos_fread(fh, in, (unsigned) want);
        if (got <= 0) {
            tbi_drop_store(tb);

            return false;
        }
        // The run up to the next line feed, found and moved whole, the same way
        // tb_read does it: memchr is a CPIR on this machine and memcpy an LDIR,
        // block instructions that manage a byte in a cycle or two, where
        // testing and copying a byte at a time in C is a dozen instructions
        // each. Only the line feed itself is handled singly, and there is one
        // of those per line rather than per byte.
        //
        // This was half of what opening a 419 KB file cost.
        int n = carry;      // the converted bytes land after what was held over
        carry = 0;
        const char* p = in;
        int left_in = got;
        while (left_in > 0) {
            const char* nl = (const char*) memchr(p, '\n', (size_t) left_in);
            const int run = nl != NULL ? (int) (nl - p) : left_in;
            if (run > 0) {
                memcpy(out + n, p, (size_t) run);
                n += run;
                p += run;
                left_in -= run;
                // Only the last byte of the run can leave one pending, and a
                // run of none leaves whatever the chunk before did.
                pending_cr = out[n - 1] == '\r';
            }
            if (nl == NULL) {
                break;
            }
            if (normalise) {
                if (!pending_cr) {
                    out[n++] = '\r';
                    added++;
                }
            } else {
                // Taken as it is. Reading the byte already copied works
                // across a chunk boundary too, because a break split by one is
                // held over in `out` with its return.
                if (!break_len_agrees(tb, &seen, break_len_at(out, n), mixed)) {
                    return false;
                }
            }
            out[n++] = '\n';
            pending_cr = false;
            p++;
            left_in--;
        }
        int at = 0;
        if (filling) {
            int lines = 0;
            at = tbi_mem_give_back(tb, out, n, spare, &lines);
            gave = gave || at > 0;
            const int rest = n - at;
            if (at > 0 && rest <= (int) sizeof(out) - TB_CHUNK * 2) {
                // Memory takes whole lines, and a chunk ends in the middle of
                // one as often as not. The half line is held over for the next
                // round rather than given to the tail: giving it away would
                // put later text in front of earlier, so the filling would
                // have to stop, and it would stop after the very first chunk.
                //
                // What is held over has no break in it, by construction, so
                // the last break in `out` is always inside the chunk just
                // converted and the half line is at most a chunk less one --
                // which is what `out` is sized for and why this test cannot
                // actually fail. It is written against the buffer rather than
                // against a constant so that the size and the bound cannot
                // drift apart.
                memmove(out, out + at, (size_t) rest);
                carry = rest;
                at = n;         // nothing for the tail this time round
            } else {
                // No room, or a single line longer than two chunks. Either way
                // memory is done and everything from here goes to the tail,
                // starting with what is held over -- which is still at the
                // front of `out`, in front of the chunk just converted.
                filling = false;
            }
        }
        if (at < n && !tb_page_fill(tb, out + at, n - at)) {
            tbi_drop_store(tb);

            return false;
        }
        left -= got;
    }
    // The last half line, if the read ended while memory was still filling.
    if (carry > 0 && !tb_page_fill(tb, out, carry)) {
        tbi_drop_store(tb);

        return false;
    }

    // Whatever memory did not take off the read, in case it has room left --
    // a buffer bigger than the document's first chunks, or an index that ran
    // out and freed slots. Nothing to do in the ordinary case.
    tb_page_prime(tb);
    if (was_empty && gave) {
        tbi_mem_close_empty(tb);
    }

    // Nothing in memory and a document in the store is not an open document,
    // it is an unreachable one: a single line longer than memory can hold
    // cannot be brought in, because a slide moves whole lines and there is no
    // whole line to move. It looked like success -- a 200 KB file of one line
    // opened as an empty buffer, said it had one line of no length, and saved
    // all 204,800 bytes back. So the file is there, invisible, and one
    // keystroke away from being edited at the wrong end.
    //
    // Refused instead, which is what it said before large files were openable
    // at all. The limit is a line longer than the window, not a file.
    if (cb_used(&tb->cb_) == 0 && store_tail_bytes(tb->store_) > 0) {
        tbi_drop_store(tb);

        return false;
    }

    // Nothing was changed on the way in unless this was the second pass, so the
    // document is clean and saving it gives the file back byte for byte. The
    // normalised one is dirty, because a save really will rewrite its breaks.
    if (normalise) {
        tb->dirty_ = added != 0;
    } else {
        tb->dirty_ = false;
    }
    tb->load_dirty_ = tb->dirty_;

    return true;
}

static bool tb_load_paged(text_buffer* tb, char fh, int size) {
    bool mixed = false;
    if (tb_load_paged_pass(tb, fh, size, false, &mixed)) {
        return true;
    }
    if (!mixed) {
        return false;
    }

    // Both kinds of break. Start again from the top of the file, converting.
    tb_clear(tb);
    if (mos_flseek(fh, 0) != 0) {
        return false;
    }

    return tb_load_paged_pass(tb, fh, size, true, &mixed);
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
    bool full = false;
    if (sz > 0) {
       ok = tb_read(fh, tb, sz, &full);
    }
    if (full) {
        // It fits in memory and its lines do not fit in the index. The index
        // has one slot per 32 bytes of buffer, so 8,192 of them at the size the
        // editor runs with, and a 200 KB file of short lines has more lines
        // than that while being nothing out of the ordinary.
        //
        // Paging is what has somewhere to put them: it holds a window on the
        // document and the rest in the store, and the index only ever has to
        // describe the window. So the load starts again, from the top of the
        // file, down the path a file too big for memory takes.
        tb_clear(tb);
        if (mos_flseek(fh, 0) != 0) {
            mos_fclose(fh);
            tb->fname_[0] = 0;

            return TB_NO_FILE;
        }
        const bool paged = tb_load_paged(tb, fh, sz);
        mos_fclose(fh);
        if (!paged) {
            tb->fname_[0] = 0;

            return TB_TOO_LARGE;
        }

        return TB_OK;
    }
    mos_fclose(fh);

    return ok ? TB_OK : TB_NO_FILE;
}

// Lets go of the store, if there is one. A document that is being replaced or
// emptied has no use for it, and its two scratch files should not outlive it --
// nor should paged_ stay set, because tb_page_open refuses on a buffer that is
// already paged and the next document would silently fail to page.
void tbi_drop_store(text_buffer* tb) {
    if (tb == NULL || !tb->paged_ || tb->walker_) {
        return;
    }
    store_destroy(tb->store_);
    free(tb->store_);
    tb->store_ = NULL;
    tb->paged_ = false;
}

void tb_clear(text_buffer* tb) {
    tbi_drop_store(tb);
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
    tb->elen_ = 2;
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
    if (fil->obj.objsize > (uint32_t) 0x7FFFFF) {
        mos_fclose(fh);

        return TB_TOO_LARGE;
    }
    // Bigger than the buffer is no longer a refusal, here as in tb_load: it
    // pages. CTRL+O used to be the one way into the editor that could not open
    // a large file, so `aed big.asm` worked and opening the same file from
    // inside did not.
    //
    // Measured against the whole buffer rather than what is free in it: the
    // document on screen is about to be discarded, so its bytes are not in the
    // way of the one replacing them.
    const bool big = fil->obj.objsize > (uint32_t) cb_size(&tb->cb_);
    const int fsz = (int) fil->obj.objsize;

    // A line longer than the window cannot be paged: a slide moves whole lines
    // and there is no whole line to move. Checked here, on the front of the
    // file, because everything below this discards the document on screen and
    // a file that cannot be opened has to leave the editor as it was.
    //
    // One chunk of lookahead. A first line longer than that but still shorter
    // than memory gets past this and is caught after the load instead, by
    // which time the old document is gone -- but that is a line of thousands
    // of characters, where this catches the file that is one line from end to
    // end, which is what a minified anything looks like.
    if (big) {
        static char probe[TB_CHUNK];
        const int want = fsz < TB_CHUNK ? fsz : TB_CHUNK;
        const int got = (int) mos_fread(fh, probe, (unsigned) want);
        if (got <= 0 || memchr(probe, '\n', (size_t) got) == NULL) {
            mos_fclose(fh);

            return TB_TOO_LARGE;
        }
        if (mos_flseek(fh, 0) != 0) {
            mos_fclose(fh);

            return TB_NO_FILE;
        }
    }

    tb_clear(tb);
    memcpy(tb->fname_, name, (size_t) sz + 1);

    // tb_read normalises line endings through the same primitives an edit uses.
    // tb_load gets away with not caring because it runs before the editor
    // attaches a log; this does not, and without the hold the first undo would
    // unpick the file's own CRLFs.
    const bool was = undo_hold(tb->undo_);
    bool ok = true;
    bool full = false;
    if (big) {
        // Nothing to roll back to if this fails: the document it would have
        // been rolled back to has already been cleared, because the store's
        // scratch files are named after the one being opened and there is no
        // way to find out whether they can be made without trying. A failure
        // here leaves an empty buffer, which is what the caller reports.
        ok = tb_load_paged(tb, fh, fsz);
    } else if (fsz > 0) {
        ok = tb_read(fh, tb, fsz, &full);
    }
    undo_release(tb->undo_, was);
    if (full) {
        // It fits by size and its lines do not fit the index, as in tb_load.
        // Start again down the paged path, which only has to describe the
        // window.
        tb_clear(tb);
        if (mos_flseek(fh, 0) != 0) {
            mos_fclose(fh);

            return TB_NO_FILE;
        }
        ok = tb_load_paged(tb, fh, fsz);
        mos_fclose(fh);

        return ok ? TB_OK : TB_TOO_LARGE;
    }
    mos_fclose(fh);
    if (!ok) {
        return big ? TB_TOO_LARGE : TB_NO_FILE;
    }

    return TB_OK;
}

/*
 * The whole document, in order, to a sink: HEAD, then what memory holds, then
 * what is left of TAIL.
 *
 * This is the only way to read a paged document end to end. A walker cannot do
 * it -- walkers must not slide, or painting would move the window out from
 * under the cursor that owns it -- so anything that has to see text outside the
 * window comes through here instead. Saving was the first, and find and the
 * range operations are the rest.
 *
 * The text arrives exactly as it is held, with the gap in the middle of memory
 * skipped rather than sent. The breaks are the file's own -- a document loaded
 * from bare line feeds streams bare line feeds -- so a sink can write what it
 * is given straight out.
 *
 * Reads only. The window does not move and the cursor does not either, so a
 * caller can stream the document and carry on from where it was.
 */
bool tbi_doc_stream(text_buffer* tb, tb_sink sink, void* ctx) {
    static char buf[TB_CHUNK];

    // The head, front to back.
    for (int at = 0; at < store_head_bytes(tb->store_); ) {
        const int got = store_head_read(tb->store_, at, buf, TB_CHUNK);
        if (got <= 0) {
            return false;
        }
        if (!sink(ctx, buf, got)) {
            return false;
        }
        at += got;
    }

    // Memory: the prefix and the suffix, in order. The gap between them holds
    // no live text.
    {
        char* prefix = NULL;
        char* suffix = NULL;
        int psz = 0;
        int ssz = 0;
        tb_content(tb, &prefix, &psz, &suffix, &ssz);
        if (prefix != NULL && psz > 0 && !sink(ctx, prefix, psz)) {
            return false;
        }
        if (suffix != NULL && ssz > 0 && !sink(ctx, suffix, ssz)) {
            return false;
        }
    }

    // And whatever the tail still holds, from where its live text starts.
    {
        int at = store_tail_from(tb->store_);
        const int end = at + store_tail_bytes(tb->store_);
        while (at < end) {
            const int got = store_tail_read(tb->store_, at, buf, TB_CHUNK);
            if (got <= 0) {
                return false;
            }
            if (!sink(ctx, buf, got)) {
                return false;
            }
            at += got;
        }
    }

    return true;
}

// tbi_doc_stream's sink for saving: the file, and whether it is still going.
//
// There is no conversion here any more. A document is held with the breaks its
// file had -- see tb_load_paged_pass -- so what the buffer holds is what the
// file wants, and the 135 lines that used to turn CRLF back into bare line
// feeds on the way out went with that. Saving slow.asm, 419 KB of bare line
// feeds, went from 2.06 seconds to 0.18.
typedef struct _save_out {
    char fh;
    bool ok;
} save_out;

static bool save_sink(void* ctx, const char* buf, int sz) {
    save_out* o = (save_out*) ctx;

    if (o->ok && sz > 0
            && mos_fwrite(o->fh, (char*) buf, (unsigned) sz) != (unsigned) sz) {
        o->ok = false;
    }

    return o->ok;
}

static bool tb_save_paged(text_buffer* tb) {
    static const char TMP_SUFFIX[] = ".aeds";
    static char tmp[TB_FNAME_MAX + sizeof(TMP_SUFFIX)];

    const int nlen = (int) strlen(tb->fname_);
    if (nlen + (int) sizeof(TMP_SUFFIX) > (int) sizeof(tmp)) {
        return false;
    }
    memcpy(tmp, tb->fname_, (size_t) nlen);
    memcpy(tmp + nlen, TMP_SUFFIX, sizeof(TMP_SUFFIX));

    const char fh = mos_fopen(tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return false;
    }

    // Line endings go back out the way they came in. The document is held in
    // memory and in the store as CRLF whatever the file had, so one that came
    // in with bare line feeds has to be converted on the way out -- otherwise
    // opening and saving it adds a byte to every line, which is what a 419 KiB
    // file growing by exactly its line count looks like.
    save_out o;
    o.fh = fh;
    o.ok = true;

    if (!tbi_doc_stream(tb, save_sink, &o)) {
        o.ok = false;
    }
    const bool ok = o.ok;
    mos_fclose(fh);

    if (!ok) {
        mos_del(tmp);

        return false;
    }
    if (mos_ren(tmp, tb->fname_) != 0) {
        mos_del(tmp);

        return false;
    }

    return true;
}

bool tb_save(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    if (!tb_valid_file(tb)) {
        return false;
    }
    if (tb->paged_) {
        if (!tb_save_paged(tb)) {
            return false;
        }
        tbi_saved(tb);

        return true;
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

    bool ok = true;
    if (prefix != NULL && psz > 0) {
        ok = mos_fwrite(fh, prefix, (unsigned) psz) == (unsigned) psz;
    }
    if (ok && suffix != NULL && ssz > 0) {
        ok = mos_fwrite(fh, suffix, (unsigned) ssz) == (unsigned) ssz;
    }

    mos_fclose(fh);
    // A card that filled up partway leaves a short file behind, and the buffer
    // stays dirty so the user is told the save did not happen.
    if (!ok) {
        return false;
    }
    tbi_saved(tb);

    return true;
}

bool tb_valid_file(text_buffer* tb) {
    return tb->fname_ != NULL && tb->fname_[0] != 0;
}
