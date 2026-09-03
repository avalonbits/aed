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
    tb->x_ = 0;
    tb->fname_[0] = 0;
    tb->dirty_ = false;

    if (fname != NULL && !tb_load(tb, fname)) {
        lb_destroy(&tb->lb_);
        cb_destroy(&tb->cb_);
        return NULL;
    };
    return tb;
}

void tb_destroy(text_buffer* tb) {
    cb_destroy(&tb->cb_);
    lb_destroy(&tb->lb_);
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
    if (tb->fname_[0] == 0) {
        return NULL;
    }
    return tb->fname_;
}

bool tb_changed(text_buffer* tb) {
   return tb->dirty_;
}

static void tb_saved(text_buffer* tb) {
    tb->dirty_ = false;
}

void tb_set_fname(text_buffer* tb, const char* fname, int sz) {
    if (sz >= (int) sizeof(tb->fname_)) {
        sz = sizeof(tb->fname_) - 1;
    }
    strncpy(tb->fname_, fname, sz);
    tb->fname_[sz] = 0;
}

// Character ops.
bool tb_put(text_buffer* tb, char ch) {
    if (!cb_put(&tb->cb_, ch)) {
        return false;
    }
    tb->x_++;
    lb_cinc(&tb->lb_);
    tb->dirty_ = true;

    return true;
}

bool tb_del(text_buffer* tb) {
    if (cb_del(&tb->cb_)) {
        lb_cdec(&tb->lb_);
        tb->dirty_ = true;
        return true;
    }
    return false;
}

bool tb_bksp(text_buffer* tb) {
    const bool ok = cb_bksp(&tb->cb_);
    if (ok) {
        tb->x_--;
        lb_cdec(&tb->lb_);
        tb->dirty_ = true;
    }
    return ok;
}

bool tb_newline(text_buffer* tb) {
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
    tb_put(tb, '\r');
    tb_put(tb, '\n');
    const bool ok = lb_new(&tb->lb_, tb->x_);
    if (ok) {
        tb->x_ = 0;
    }

    return ok;
}

bool tb_del_line(text_buffer* tb) {
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
    if (lb_last(&tb->lb_) || !tb_eol(tb)) {
       return false;
    }

    // This function is only called when we are the end of the line.
    // If we are not the last, then we have a \r\n sequence.
    tb_del(tb);
    tb_del(tb);
    lb_merge_next(&tb->lb_);
    tb->dirty_ = true;

    return true;
}

bool tb_bksp_merge(text_buffer* tb) {
    if (!tb_bol(tb) || tb_ypos(tb) == 1) {
        return false;
    }
    cb_bksp(&tb->cb_);
    cb_bksp(&tb->cb_);

    tb->x_ = lb_merge_prev(&tb->lb_);
    tb->dirty_ = true;
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

int tb_ypos(text_buffer* tb) {
    return lb_curr(&tb->lb_)+1;
}

int tb_ymax(text_buffer* tb) {
    return lb_max(&tb->lb_)  - lb_avai(&tb->lb_) +1;
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
    int prev = -1;
    while (tb_ypos(tb) < p.line && tb_ypos(tb) != prev) {
        prev = tb_ypos(tb);
        tb_down(tb);
    }
    prev = -1;
    while (tb_ypos(tb) > p.line && tb_ypos(tb) != prev) {
        prev = tb_ypos(tb);
        tb_up(tb);
    }

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
    char_buffer* out = (char_buffer*) ctx;
    for (int i = 0; i < sz; i++) {
        if (!cb_put(out, buf[i])) {
            return false;
        }
    }

    return true;
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
    dst->fname_[0] = 0;
    dst->dirty_ = false;
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
    const char pch = cb_prev(cb, 1);
    cb_next(cb, 1);

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
    int added = 0;
    for (int i = 0; i < sz; i++) {
        lb_cinc(&tb->lb_);
        if (cb_peek(cb) == '\n') {
            added += ensure_newline(&tb->cb_, &tb->lb_);
        }
        cb_next(cb, 1);
    }

    if (cb_peek(cb) == '\n') {
        added += ensure_newline(&tb->cb_, &tb->lb_);
    }

    cb_prev(cb, sz+added);
    tb->dirty_ = added != 0;

    // Now move the line buffer back to the first line.
    while (lb_up(&tb->lb_)) ;
    return true;
}


bool tb_load(text_buffer* tb, const char* fname) {
    if (fname == NULL) {
        return false;
    }

    int fsz = strlen(fname);
    strncpy(tb->fname_, fname, fsz);
    tb->fname_[fsz] = 0;

    char fh = mos_fopen(tb->fname_, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
    if (fh == 0) {
        // Try to create the file.
        fh = mos_fopen(tb->fname_, FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
        if (fh == 0) {
            char* msg = "invalid file";
            mos_puts(msg, strlen(msg), 0);
            tb->fname_[0] = 0;
            return false;
        }
    }
    FIL* fil = mos_getfil(fh);
    if (fil == NULL) {
        mos_fclose(fh);
        return false;
    }

    bool ok = true;

    // Compared before narrowing: objsize is 32 bits and the eZ80's int is 24,
    // so a file over 8MB would arrive here as a small or negative number and
    // walk straight past a signed check.
    if (fil->obj.objsize > (uint32_t) cb_available(&tb->cb_)) {
        char* msg = "file too large";
        mos_puts(msg, strlen(msg), 0);
        mos_fclose(fh);
        tb->fname_[0] = 0;

        return false;
    }
    const int sz = (int) fil->obj.objsize;
    if (sz > 0) {
       ok = tb_read(fh, tb, sz);
    }
    mos_fclose(fh);

    return ok;
}

void tb_clear(text_buffer* tb) {
    cb_clear(&tb->cb_);
    lb_clear(&tb->lb_);
    tb->x_ = 0;
    tb->dirty_ = false;
}

tb_result tb_open(text_buffer* tb, const char* fname, int sz) {
    if (fname == NULL || sz <= 0) {
        return TB_NO_FILE;
    }

    char name[sizeof(tb->fname_)];
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

    bool ok = true;
    if (fsz > 0) {
        ok = tb_read(fh, tb, fsz);
    }
    mos_fclose(fh);

    return ok ? TB_OK : TB_NO_FILE;
}

bool tb_save(text_buffer* tb) {
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

    if (prefix != NULL && psz > 0) {
        mos_fwrite(fh, prefix, psz);
    }
    if (suffix != NULL && ssz > 0) {
        mos_fwrite(fh, suffix, ssz);
    }

    mos_fclose(fh);
    tb_saved(tb);

    return true;
}

bool tb_valid_file(text_buffer* tb) {
    return tb->fname_[0] != 0;
}
