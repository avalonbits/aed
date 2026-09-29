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

#include "app.h"

#include <agon/mos.h>
#include <stddef.h>
#include <string.h>

/*
 * Both scratch files are opened once, at store_init, and stay open until
 * store_destroy. Every operation seeks on the handle it wants and reads or
 * writes there.
 *
 * They used to be opened and closed around each push and pop, on the reasoning
 * that "a slide is already a read and a write of a couple of kilobytes; two
 * opens on top of that are not what makes it slow". Measured on MOS 3.0.2, a
 * slide's two opens are 22 ms and its read and write of 2 KiB are under one --
 * mos_fopen costs 1.12 cs there against 0.01 cs on the console8 firmware, which
 * is the entire difference between the two in every paging measurement taken.
 * The opens were not most of a slide's cost, they were essentially all of it.
 *
 * The objection was handles: MOS has few, the clipboard wants one, and a second
 * document would want two more. Measured, MOS gives out seven at once on both
 * firmwares, so two for the open document leaves four spare.
 *
 * FA_READ | FA_WRITE and nothing else. FA_OPEN_ALWAYS on MOS 3.0.2 puts writes
 * at the end of the file whatever the position says: a seek to 100 in a 1,000
 * byte file reports success, moves fptr to 100, and then writes at 1,000.
 * Sliding up seeks backwards to write, so under that flag it wrote nothing
 * where it meant to and appended instead -- see the history of this file.
 *
 * Reading and writing the same held handle at scattered offsets is safe: FatFS
 * flushes its one sector window when the position moves out of it. Verified on
 * both firmwares before this was written, because it is exactly what a slide
 * does and a wrong answer would be silent.
 */
static int read_fh(char fh, int at, char* buf, int n) {
    if (fh == 0 || buf == NULL || n <= 0 || at < 0) {
        return 0;
    }
    if (mos_flseek(fh, (uint32_t) at) != 0) {
        return 0;
    }

    return (int) mos_fread(fh, buf, (unsigned) n);
}

static bool write_fh(char fh, int at, const char* buf, int n) {
    if (fh == 0 || buf == NULL || n < 0 || at < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }

    return mos_flseek(fh, (uint32_t) at) == 0
           && mos_fwrite(fh, (char*) buf, (unsigned) n) == (unsigned) n;
}


static bool name_with(char* out, const char* base, const char* suffix) {
    const int blen = (base == NULL) ? 0 : (int) strlen(base);
    const int slen = (int) strlen(suffix);

    // Nothing to sit beside, so a plain name in the current directory rather
    // than one starting with a dot -- the same choice the clipboard makes. The
    // name is the program's: see app.h.
    if (blen == 0) {
        const app_context* app = app_get();
        if (app == NULL || app->name == NULL) {
            return false;
        }
        const int alen = (int) strlen(app->name);
        if (alen + slen >= STORE_PATH_MAX) {
            return false;
        }
        memcpy(out, app->name, (size_t) alen);
        memcpy(out + alen, suffix, (size_t) slen + 1);

        return true;
    }
    if (blen + slen >= STORE_PATH_MAX) {
        return false;
    }
    memcpy(out, base, (size_t) blen);
    memcpy(out + blen, suffix, (size_t) slen + 1);

    return true;
}

/*
 * The stores with their files open right now.
 *
 * A store's files are named after its document, so two stores on one file
 * would share them -- and store_init creates them afresh, so the second would
 * empty the first's, and closing either would delete both. The part of a
 * document that is not in memory lives only there. So a name already in use
 * is refused, and the files are the card's, which is the program's: this list
 * is too. Two stores hold four of the seven handles MOS gives a program, so
 * no more than three can be open at once; this is room for all of them.
 */
#define STORE_LIVE_MAX 4

static const doc_store* live[STORE_LIVE_MAX];

// Case-blind, as FAT names are: A.TXT and a.txt are one file.
static bool same_name(const char* a, const char* b) {
    for (;; a++, b++) {
        char x = *a;
        char y = *b;
        if (x >= 'a' && x <= 'z') {
            x = (char) (x - 'a' + 'A');
        }
        if (y >= 'a' && y <= 'z') {
            y = (char) (y - 'a' + 'A');
        }
        if (x != y) {
            return false;
        }
        if (x == 0) {
            return true;
        }
    }
}

/*
 * A path as one spelling per file, so that two names for the same file compare
 * equal: MOS resolves a relative name against the current directory, so from
 * the root "big.c", "/big.c", "./big.c" and "sub/../big.c" are one file. The
 * current directory is asked for (MOS 2.2.0+), a drive prefix such as "0:" is
 * dropped, and "." and ".." are folded away. False when it does not fit or the
 * directory cannot be had -- which callers treat as a clash, the safe answer.
 */
static bool resolve(char* out, int max, const char* path) {
    static char joined[2 * STORE_PATH_MAX];
    int j = 0;
    if (path[0] != '/') {
        if (ffs_getcwd(joined, (unsigned) STORE_PATH_MAX) != 0) {
            return false;
        }
        const char* colon = strchr(joined, ':');
        if (colon != NULL) {
            memmove(joined, colon + 1, strlen(colon + 1) + 1);
        }
        j = (int) strlen(joined);
        joined[j++] = '/';
    }
    const int plen = (int) strlen(path);
    if (j + plen >= (int) sizeof(joined)) {
        return false;
    }
    memcpy(joined + j, path, (size_t) plen + 1);

    int n = 0;
    const char* p = joined;
    while (*p != 0) {
        while (*p == '/') {
            p++;
        }
        const char* e = p;
        while (*e != 0 && *e != '/') {
            e++;
        }
        const int len = (int) (e - p);
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            while (n > 0 && out[n - 1] != '/') {
                n--;
            }
            if (n > 0) {
                n--;
            }
        } else if (len > 0 && !(len == 1 && p[0] == '.')) {
            if (n + 1 + len >= max) {
                return false;
            }
            out[n++] = '/';
            memcpy(out + n, p, (size_t) len);
            n += len;
        }
        p = e;
    }
    if (n == 0) {
        out[n++] = '/';
    }
    out[n] = 0;

    return true;
}

bool store_live(const char* base, const doc_store* except) {
    static char head[STORE_PATH_MAX];
    static char key[STORE_PATH_MAX];
    if (!name_with(head, base, STORE_HEAD_SUFFIX)) {
        return false;
    }
    if (!resolve(key, STORE_PATH_MAX, head)) {
        return true;        // cannot tell: say it clashes rather than risk it
    }
    for (int i = 0; i < STORE_LIVE_MAX; i++) {
        if (live[i] != NULL && live[i] != except && same_name(live[i]->key_, key)) {
            return true;
        }
    }

    return false;
}

static bool live_add(const doc_store* st) {
    for (int i = 0; i < STORE_LIVE_MAX; i++) {
        if (live[i] == NULL) {
            live[i] = st;

            return true;
        }
    }

    return false;
}

static void live_remove(const doc_store* st) {
    for (int i = 0; i < STORE_LIVE_MAX; i++) {
        if (live[i] == st) {
            live[i] = NULL;
        }
    }
}

bool store_init(doc_store* st, const char* base) {
    if (st == NULL) {
        return false;
    }
    st->open_ = false;
    st->head_fh_ = 0;
    st->tail_fh_ = 0;
    st->head_len_ = 0;
    st->tail_start_ = STORE_HEADROOM;
    st->tail_end_ = STORE_HEADROOM;

    if (!name_with(st->head_, base, STORE_HEAD_SUFFIX)
            || !name_with(st->tail_, base, STORE_TAIL_SUFFIX)) {
        return false;
    }
    // Another store's files: see live above. And the resolved name kept, for
    // later stores to be compared with.
    if (store_live(base, NULL) || !resolve(st->key_, STORE_PATH_MAX, st->head_)) {
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

    // And now the handles the store keeps. Created and filled above with their
    // own, because creating is the one thing FA_CREATE_ALWAYS is for and these
    // want FA_READ | FA_WRITE and nothing else -- see the note on the helpers.
    st->head_fh_ = mos_fopen(st->head_, FA_READ | FA_WRITE);
    st->tail_fh_ = mos_fopen(st->tail_, FA_READ | FA_WRITE);
    if (st->head_fh_ == 0 || st->tail_fh_ == 0) {
        if (st->head_fh_ != 0) {
            mos_fclose(st->head_fh_);
        }
        if (st->tail_fh_ != 0) {
            mos_fclose(st->tail_fh_);
        }
        st->head_fh_ = 0;
        st->tail_fh_ = 0;
        mos_del(st->head_);
        mos_del(st->tail_);

        return false;
    }

    st->open_ = true;
    live_add(st);

    return true;
}

void store_destroy(doc_store* st) {
    if (st == NULL || !st->open_) {
        return;
    }
    // Closed before deleting: a file with an open handle is not a file MOS
    // will remove, and a scratch file left on the card outlives the session
    // that made it.
    if (st->head_fh_ != 0) {
        mos_fclose(st->head_fh_);
        st->head_fh_ = 0;
    }
    if (st->tail_fh_ != 0) {
        mos_fclose(st->tail_fh_);
        st->tail_fh_ = 0;
    }
    mos_del(st->head_);
    mos_del(st->tail_);
    live_remove(st);
    st->open_ = false;
    st->head_len_ = 0;
    st->tail_start_ = STORE_HEADROOM;
    st->tail_end_ = STORE_HEADROOM;
}

bool store_tail_has_room(const doc_store* st, int n) {
    return st != NULL && st->open_ && n >= 0 && st->tail_start_ >= n;
}

bool store_tail_append(doc_store* st, const char* buf, int n) {
    if (st == NULL || !st->open_ || buf == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (!write_fh(st->tail_fh_, st->tail_end_, buf, n)) {
        return false;
    }
    st->tail_end_ += n;

    return true;
}

bool store_head_push(doc_store* st, const char* buf, int n) {
    if (st == NULL || !st->open_ || buf == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    // At head_len_, not at the end of the file. A pop leaves the bytes where
    // they are and stops counting them, so the file is often longer than the
    // document's head -- and the next push has to write over them.
    if (!write_fh(st->head_fh_, st->head_len_, buf, n)) {
        return false;
    }
    st->head_len_ += n;

    return true;
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
    const int got = read_fh(st->head_fh_, st->head_len_ - n, buf, n);

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
    const int got = read_fh(st->tail_fh_, st->tail_start_, buf, n);
    st->tail_start_ += got;

    return got;
}

int store_head_read(doc_store* st, int at, char* buf, int n) {
    if (st == NULL || !st->open_) {
        return 0;
    }
    if (at + n > st->head_len_) {
        n = st->head_len_ - at;     // never past what the document owns
    }

    return read_fh(st->head_fh_, at, buf, n);
}

int store_tail_read(doc_store* st, int at, char* buf, int n) {
    if (st == NULL || !st->open_) {
        return 0;
    }
    if (at + n > st->tail_end_) {
        n = st->tail_end_ - at;
    }

    return read_fh(st->tail_fh_, at, buf, n);
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
    const int at = st->tail_start_ - n;
    if (!write_fh(st->tail_fh_, at, buf, n)) {
        return false;
    }
    st->tail_start_ = at;

    return true;
}