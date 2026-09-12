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

#include "doc_store.h"

#include <agon/mos.h>
#include <stddef.h>
#include <string.h>

// A file is opened for the length of one push or pop and closed again.
//
// Holding both open for the session would save two MOS calls per slide, and
// cost a pair of handles for as long as a document is open -- MOS has few, the
// clipboard wants one, and a second document would want two more. A slide is
// already a read and a write of a couple of kilobytes; two opens on top of that
// are not what makes it slow.
static char open_rw(const char* path) {
    return mos_fopen(path, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
}

static bool name_with(char* out, const char* base, const char* suffix) {
    const int blen = (base == NULL) ? 0 : (int) strlen(base);
    const int slen = (int) strlen(suffix);

    // Nothing to sit beside, so a plain name in the current directory rather
    // than one starting with a dot -- the same choice the clipboard makes.
    if (blen == 0) {
        if (3 + slen >= STORE_PATH_MAX) {
            return false;
        }
        memcpy(out, "aed", 3);
        memcpy(out + 3, suffix, (size_t) slen + 1);

        return true;
    }
    if (blen + slen >= STORE_PATH_MAX) {
        return false;
    }
    memcpy(out, base, (size_t) blen);
    memcpy(out + blen, suffix, (size_t) slen + 1);

    return true;
}

bool store_init(doc_store* st, const char* base) {
    if (st == NULL) {
        return false;
    }
    st->open_ = false;
    st->tail_fh_ = 0;
    st->head_len_ = 0;
    st->tail_start_ = STORE_HEADROOM;
    st->tail_end_ = STORE_HEADROOM;

    if (!name_with(st->head_, base, STORE_HEAD_SUFFIX)
            || !name_with(st->tail_, base, STORE_TAIL_SUFFIX)) {
        return false;
    }

    // Created empty, both of them, so that a store either has its files or has
    // failed -- rather than finding out halfway through the first slide.
    const char h = mos_fopen(st->head_, FA_WRITE | FA_CREATE_ALWAYS);
    if (h == 0) {
        return false;
    }
    mos_fclose(h);

    const char t = mos_fopen(st->tail_, FA_WRITE | FA_CREATE_ALWAYS);
    if (t == 0) {
        mos_del(st->head_);

        return false;
    }
    // The headroom, actually written. See the note on STORE_HEADROOM: a seek
    // past the end does not make a file longer, it just lands the write at the
    // end -- which puts the document a headroom too early and loses that much
    // off its far end.
    static char zeros[512];
    memset(zeros, 0, sizeof(zeros));
    bool ok = true;
    for (int at = 0; ok && at < STORE_HEADROOM; at += (int) sizeof(zeros)) {
        ok = mos_fwrite(t, zeros, (unsigned) sizeof(zeros))
             == (unsigned) sizeof(zeros);
    }
    mos_fclose(t);
    if (!ok) {
        mos_del(st->head_);
        mos_del(st->tail_);

        return false;
    }

    st->open_ = true;

    return true;
}

void store_destroy(doc_store* st) {
    if (st == NULL || !st->open_) {
        return;
    }
    store_tail_release(st);
    mos_del(st->head_);
    mos_del(st->tail_);
    st->open_ = false;
    st->head_len_ = 0;
    st->tail_start_ = STORE_HEADROOM;
    st->tail_end_ = STORE_HEADROOM;
}

int store_head_bytes(const doc_store* st) {
    return (st == NULL || !st->open_) ? 0 : st->head_len_;
}

int store_tail_bytes(const doc_store* st) {
    return (st == NULL || !st->open_) ? 0 : st->tail_end_ - st->tail_start_;
}

bool store_tail_has_room(const doc_store* st, int n) {
    return st != NULL && st->open_ && n >= 0 && st->tail_start_ >= n;
}

bool store_tail_hold(doc_store* st) {
    if (st == NULL || !st->open_ || st->tail_fh_ != 0) {
        return false;
    }
    st->tail_fh_ = open_rw(st->tail_);

    return st->tail_fh_ != 0;
}

void store_tail_release(doc_store* st) {
    if (st == NULL || st->tail_fh_ == 0) {
        return;
    }
    mos_fclose(st->tail_fh_);
    st->tail_fh_ = 0;
}

bool store_tail_append(doc_store* st, const char* buf, int n) {
    if (st == NULL || !st->open_ || buf == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    // The held handle when a load is running, its own otherwise.
    const bool held = st->tail_fh_ != 0;
    const char fh = held ? st->tail_fh_ : open_rw(st->tail_);
    if (fh == 0) {
        return false;
    }
    // Seeking past the end rather than writing the headroom is what makes the
    // dead space free: nothing is transferred to create it.
    const bool ok = mos_flseek(fh, (uint32_t) st->tail_end_) == 0
                    && mos_fwrite(fh, (char*) buf, (unsigned) n) == (unsigned) n;
    if (!held) {
        mos_fclose(fh);
    }
    if (ok) {
        st->tail_end_ += n;
    }

    return ok;
}

bool store_head_push(doc_store* st, const char* buf, int n) {
    if (st == NULL || !st->open_ || buf == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    const char fh = open_rw(st->head_);
    if (fh == 0) {
        return false;
    }
    // At head_len_, not at the end of the file. A pop leaves the bytes where
    // they are and stops counting them, so the file is often longer than the
    // document's head -- and the next push has to write over them.
    const bool ok = mos_flseek(fh, (uint32_t) st->head_len_) == 0
                    && mos_fwrite(fh, (char*) buf, (unsigned) n) == (unsigned) n;
    mos_fclose(fh);
    if (ok) {
        st->head_len_ += n;
    }

    return ok;
}

int store_head_pop(doc_store* st, char* buf, int n) {
    if (st == NULL || !st->open_ || buf == NULL || n <= 0) {
        return 0;
    }
    if (n > st->head_len_) {
        n = st->head_len_;
    }
    if (n == 0) {
        return 0;
    }
    const char fh = mos_fopen(st->head_, FA_READ);
    if (fh == 0) {
        return 0;
    }
    const int at = st->head_len_ - n;
    int got = 0;
    if (mos_flseek(fh, (uint32_t) at) == 0) {
        got = (int) mos_fread(fh, buf, (unsigned) n);
    }
    mos_fclose(fh);

    // Only what actually came back stops being the head's. A short read leaves
    // the rest where it is rather than losing it.
    st->head_len_ -= got;

    return got;
}

int store_tail_pop(doc_store* st, char* buf, int n) {
    if (st == NULL || !st->open_ || buf == NULL || n <= 0) {
        return 0;
    }
    const int have = st->tail_end_ - st->tail_start_;
    if (n > have) {
        n = have;
    }
    if (n <= 0) {
        return 0;
    }
    const char fh = mos_fopen(st->tail_, FA_READ);
    if (fh == 0) {
        return 0;
    }
    int got = 0;
    if (mos_flseek(fh, (uint32_t) st->tail_start_) == 0) {
        got = (int) mos_fread(fh, buf, (unsigned) n);
    }
    mos_fclose(fh);
    st->tail_start_ += got;

    return got;
}

static int read_at(const char* path, int at, char* buf, int n) {
    if (path == NULL || buf == NULL || n <= 0 || at < 0) {
        return 0;
    }
    const char fh = mos_fopen(path, FA_READ);
    if (fh == 0) {
        return 0;
    }
    int got = 0;
    if (mos_flseek(fh, (uint32_t) at) == 0) {
        got = (int) mos_fread(fh, buf, (unsigned) n);
    }
    mos_fclose(fh);

    return got;
}

int store_head_read(doc_store* st, int at, char* buf, int n) {
    if (st == NULL || !st->open_) {
        return 0;
    }
    if (at + n > st->head_len_) {
        n = st->head_len_ - at;     // never past what the document owns
    }

    return read_at(st->head_, at, buf, n);
}

int store_tail_read(doc_store* st, int at, char* buf, int n) {
    if (st == NULL || !st->open_) {
        return 0;
    }
    if (at + n > st->tail_end_) {
        n = st->tail_end_ - at;
    }

    return read_at(st->tail_, at, buf, n);
}

int store_tail_from(const doc_store* st) {
    return (st == NULL || !st->open_) ? 0 : st->tail_start_;
}

void store_head_rewind(doc_store* st, int n) {
    if (st == NULL || !st->open_ || n <= 0) {
        return;
    }
    st->head_len_ += n;
}

void store_tail_rewind(doc_store* st, int n) {
    if (st == NULL || !st->open_ || n <= 0) {
        return;
    }
    if (n > st->tail_start_) {
        n = st->tail_start_;
    }
    st->tail_start_ -= n;
}

bool store_tail_push(doc_store* st, const char* buf, int n) {
    if (st == NULL || !st->open_ || buf == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (!store_tail_has_room(st, n)) {
        return false;   // the headroom is spent; TAIL has to be rebuilt
    }
    const char fh = open_rw(st->tail_);
    if (fh == 0) {
        return false;
    }
    const int at = st->tail_start_ - n;
    const bool ok = mos_flseek(fh, (uint32_t) at) == 0
                    && mos_fwrite(fh, (char*) buf, (unsigned) n) == (unsigned) n;
    mos_fclose(fh);
    if (ok) {
        st->tail_start_ = at;
    }

    return ok;
}
