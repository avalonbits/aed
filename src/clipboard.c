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

#include "clipboard.h"

#include <agon/mos.h>
#include <stddef.h>
#include <string.h>

static const char SCRATCH_SUFFIX[] = ".scratch";

// Any name the model can hold, plus the suffix and its terminator, fits. Stated
// here rather than checked at run time: a branch that cannot be taken cannot be
// tested either, and this catches the mistake when someone changes a size
// rather than when someone opens a file with a very long name.
_Static_assert(sizeof(((clipboard*) 0)->path_)
                   >= sizeof(((text_buffer*) 0)->fname_) + sizeof(SCRATCH_SUFFIX),
               "clipboard path must hold any document name plus .scratch");

clipboard* clip_init(clipboard* c, int size) {
    if (cb_init(&c->buf_, size) == NULL) {
        return NULL;
    }
    c->lines_ = 0;
    c->size_ = 0;
    c->on_file_ = false;
    c->path_[0] = 0;

    return c;
}

// Forgets what is held, taking the scratch file with it. Called before every
// copy and on the way out, so a file never outlives the copy it holds.
static void clip_forget(clipboard* c) {
    if (c->on_file_ && c->path_[0] != 0) {
        mos_del(c->path_);
    }
    cb_clear(&c->buf_);
    c->lines_ = 0;
    c->size_ = 0;
    c->on_file_ = false;
    c->path_[0] = 0;
}

void clip_destroy(clipboard* c) {
    clip_forget(c);
    cb_destroy(&c->buf_);
}

bool clip_has(clipboard* c) {
    return c->size_ > 0;
}

int clip_size(clipboard* c) {
    return c->size_;
}

int clip_lines(clipboard* c) {
    return c->lines_;
}

int clip_capacity(clipboard* c) {
    return cb_size(&c->buf_);
}

const char* clip_path(clipboard* c) {
    return c->path_;
}

// "<document>.scratch". Beside the file being edited, so it is obvious where it
// came from. An unnamed buffer has nothing to sit beside, so it gets a plain
// name in the current directory.
static bool scratch_path(clipboard* c, text_buffer* tb) {
    const char* name = tb_fname(tb);
    const int nlen = (name == NULL) ? 0 : (int) strlen(name);
    const int slen = (int) sizeof(SCRATCH_SUFFIX) - 1;

    // An unnamed buffer has nothing to sit beside, so it gets a plain name in
    // the current directory rather than one starting with a dot.
    if (nlen == 0) {
        memcpy(c->path_, "aed", 3);
        memcpy(c->path_ + 3, SCRATCH_SUFFIX, (size_t) slen + 1);

        return true;
    }
    memcpy(c->path_, name, (size_t) nlen);
    memcpy(c->path_ + nlen, SCRATCH_SUFFIX, (size_t) slen + 1);

    return true;
}

// Buffers the walk's output and writes it out a chunk at a time, counting the
// line breaks on the way past so the paste does not have to read the file to
// find out how many there are.
typedef struct _file_sink {
    char fh;
    char buf[CLIP_CHUNK];
    int n;
    int lines;
    bool ok;
} file_sink;

static bool flush(file_sink* f) {
    if (f->n == 0) {
        return true;
    }
    const unsigned wrote = mos_fwrite(f->fh, f->buf, (unsigned) f->n);
    if (wrote != (unsigned) f->n) {
        f->ok = false;

        return false;
    }
    f->n = 0;

    return true;
}

static bool file_write(void* ctx, const char* buf, int sz) {
    file_sink* f = (file_sink*) ctx;
    for (int i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            f->lines++;
        }
        f->buf[f->n++] = buf[i];
        if (f->n == (int) sizeof(f->buf) && !flush(f)) {
            return false;
        }
    }

    return true;
}

static bool spill(clipboard* c, text_buffer* tb, tb_pos a, tb_pos b, int size) {
    if (!scratch_path(c, tb)) {
        return false;
    }

    file_sink f;
    f.fh = mos_fopen(c->path_, FA_WRITE | FA_CREATE_ALWAYS);
    if (f.fh == 0) {
        c->path_[0] = 0;

        return false;
    }
    f.n = 0;
    f.lines = 0;
    f.ok = true;

    const bool walked = tb_range_walk(tb, a, b, file_write, &f);
    if (walked) {
        flush(&f);
    }
    mos_fclose(f.fh);

    if (!walked || !f.ok) {
        // Half a copy is worse than none: a cut would go on to delete text this
        // could not keep. Take the remnant with it.
        mos_del(c->path_);
        c->path_[0] = 0;

        return false;
    }

    c->on_file_ = true;
    c->size_ = size;
    c->lines_ = f.lines;

    return true;
}

bool clip_copy(clipboard* c, text_buffer* tb, tb_pos a, tb_pos b) {
    const int size = tb_range_size(tb, a, b);

    // Whatever was held goes now, file and all, so a copy that fails cannot
    // leave the last one looking like the new one.
    clip_forget(c);
    if (size <= 0) {
        return false;
    }

    if (size > cb_size(&c->buf_)) {
        return spill(c, tb, a, b, size);
    }

    const int n = tb_range_copy(tb, a, b, &c->buf_);
    if (n < 0) {
        return false;
    }

    int sz = 0;
    const char* text = cb_prefix(&c->buf_, &sz);
    for (int i = 0; i < sz; i++) {
        if (text[i] == '\n') {
            c->lines_++;
        }
    }
    c->size_ = n;

    return n > 0;
}

// Reads the scratch file back a chunk at a time. A line break split across two
// chunks -- the CR at the end of one, the LF at the start of the next -- is
// what pending_cr is for.
static bool paste_file(clipboard* c, text_buffer* tb) {
    const char fh = mos_fopen(c->path_, FA_READ);
    if (fh == 0) {
        return false;
    }

    char buf[CLIP_CHUNK];
    bool pending_cr = false;
    bool ok = true;
    int left = c->size_;

    while (left > 0) {
        unsigned want = (left < (int) sizeof(buf)) ? (unsigned) left
                                                   : (unsigned) sizeof(buf);
        const unsigned got = mos_fread(fh, buf, want);
        if (got == 0 || got > want) {
            ok = false;
            break;
        }
        if (!tb_insert_span(tb, buf, (int) got, &pending_cr)) {
            ok = false;
            break;
        }
        left -= (int) got;
    }
    mos_fclose(fh);

    if (ok && pending_cr) {
        ok = tb_newline(tb);
    }

    return ok;
}

bool clip_paste(clipboard* c, text_buffer* tb) {
    if (c->size_ <= 0) {
        return false;
    }
    if (c->on_file_) {
        return paste_file(c, tb);
    }

    int sz = 0;
    char* text = cb_prefix(&c->buf_, &sz);
    if (text == NULL || sz <= 0) {
        return false;
    }

    return tb_insert(tb, text, sz);
}
