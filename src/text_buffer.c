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

#include <agon/vdp_vdu.h>
#include <mos_api.h>
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

    if (!tb_load(tb, fname)) {
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
    const uint8_t ch = cb_peek(&tb->cb_);
    return IS_EOL(ch);
}
bool tb_bol(text_buffer* tb) {
    return tb->x_ == 0;
}

// Character ops.
void tb_put(text_buffer* tb, uint8_t ch) {
    cb_put(&tb->cb_, ch);
    tb->x_++;
    lb_cinc(&tb->lb_);
}

bool tb_del(text_buffer* tb) {
    if (cb_del(&tb->cb_)) {
        lb_cdec(&tb->lb_);
        return true;
    }
    return false;
}

bool tb_bksp(text_buffer* tb) {
    const bool ok = cb_bksp(&tb->cb_);
    if (ok) {
        tb->x_--;
        lb_cdec(&tb->lb_);
    }
    return ok;
}

bool tb_newline(text_buffer* tb) {
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


    tb->x_ = 0;
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

    return true;
}


// Cursor ops.
uint8_t tb_next(text_buffer* tb) {
    tb->x_++;
    return cb_next(&tb->cb_, 1);
}

static bool isstop(uint8_t ch) {
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
            return true;
        default:
            return false;
    }
    return false;
}

uint8_t tb_w_next(text_buffer* tb) {
    uint8_t ch = 0;
    do {
        ch = cb_next(&tb->cb_, 1);
        tb->x_++;
    } while (!IS_EOL(ch) && !isstop(ch));

    return ch;
}

uint8_t tb_prev(text_buffer* tb) {
    const uint8_t ch = cb_prev(&tb->cb_, 1);
    if (ch) {
        tb->x_--;
    }
    return ch;
}

uint8_t tb_w_prev(text_buffer* tb) {
    uint8_t ch = 0;
    do {
        ch = cb_prev(&tb->cb_, 1);
        tb->x_--;
    } while (tb->x_ > 0 && (!IS_EOL(ch) && !isstop(ch)));

    return ch;
}

uint8_t tb_up(text_buffer* tb) {
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

uint8_t tb_down(text_buffer* tb) {
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

uint8_t tb_home(text_buffer* tb) {
    const int back = tb->x_;
    tb->x_ = 0;
    return cb_prev(&tb->cb_, back);
}

uint8_t tb_end(text_buffer* tb) {
    uint8_t ch = cb_peek(&tb->cb_);
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

// State for the line iterator
static text_buffer tb;
static int ypos;
static int lused;

static void itr_state(text_buffer* buf, int from_l) {
    tb.lb_.buf_ = buf->lb_.buf_;
    tb.lb_.curr_ = buf->lb_.curr_;
    tb.lb_.cend_ = buf->lb_.cend_;
    tb.lb_.size_ = buf->lb_.size_;

    tb.cb_.buf_ = buf->cb_.buf_;
    tb.cb_.curr_ = buf->cb_.curr_;
    tb.cb_.cend_ = buf->cb_.cend_;
    tb.cb_.size_ = buf->cb_.size_;

    tb.x_ = buf->x_;
	ypos = lb_curr(&tb.lb_);
    lused = lb_max(&tb.lb_) - lb_avai(&tb.lb_);
    tb_home(&tb);
    if (ypos != from_l) {
        // do someting
    }
}

static line lnext() {
	if (ypos > lused) {
        line l = {NULL, 0, 0};
        return l;
    }

    int sz = 0;
    uint8_t* suffix = tb_suffix(&tb, &sz);
    ++ypos;

    tb_down(&tb);
    int osz = 0;
    tb_suffix(&tb, &osz);
    line l = {suffix, sz, osz};

    return l;
}

line_itr tb_nline(text_buffer* buf, int from_l) {
    itr_state(buf, from_l-1);
    return lnext;
}

static line lprev() {
    if (ypos == 0) {
        line l = {NULL, 0, 0};
        return  l;
    }

    int sz = 0;
    uint8_t* suffix = tb_suffix(&tb, &sz);
    --ypos;

    tb_up(&tb);
    int osz = 0;
    tb_suffix(&tb, &osz);
    line l = {suffix, sz, osz};

    return l;
}

line_itr tb_pline(text_buffer* buf) {
    itr_state(buf, tb_ypos(buf));
    return lprev;
}

// Char read.
uint8_t tb_peek(text_buffer* tb) {
    return cb_peek(&tb->cb_);
}
uint8_t tb_peek_at(text_buffer* tb, int idx) {
    return cb_peek_at(&tb->cb_, idx);
}

uint8_t* tb_prefix(text_buffer* tb, int* sz) {
    int psz = 0;
    uint8_t* prefix = cb_prefix(&tb->cb_, &psz);
    if (prefix == NULL) {
        return NULL;
    }
    prefix = prefix + (psz - tb->x_);
    *sz = tb->x_;
    return prefix;
}

uint8_t* tb_suffix(text_buffer* tb, int* sz) {
    uint8_t* suffix = cb_suffix(&tb->cb_, sz);
    if (suffix == NULL) {
        return NULL;
    }

    *sz = lb_csize(&tb->lb_) - tb->x_;
    if (!lb_last(&tb->lb_)) {
        *sz -= 2;
    }
    return suffix;
}

void tb_content(text_buffer* tb, uint8_t** prefix, int* psz, uint8_t** suffix, int* ssz) {
    *prefix = cb_prefix(&tb->cb_, psz);
    *suffix = cb_suffix(&tb->cb_, ssz);
}

static bool tb_read(uint8_t fh, text_buffer* tb, int sz) {
    // In order to read the file to the text buffer, we move cend_ sz postions and then
    // pass it + sz as the buffer to read.
    char_buffer* cb = &tb->cb_;
    cb->cend_ -= sz;
    mos_fread(fh, (char*)cb->cend_, sz);

    //  Now I need to update lb_ line buffer with the correct values.
    //  There might be cases where the line endings are not \r\n so we correct for them.
    bool saw_r = false;
    bool saw_n = false;
    int added = 0;
    for (int i = 0; i < (int)sz; i++) {
        lb_cinc(&tb->lb_);
        const uint8_t ch = cb_peek(cb);

        if (saw_r) {
            if (ch != '\n') {
                cb_put(cb, '\n');
                added++;
            }
            lb_new(&tb->lb_, lb_csize(&tb->lb_));
            if (ch != '\n') {
                lb_cinc(&tb->lb_);
            }
            saw_r = false;
            saw_n = false;
        } else if  (saw_n) {
            if (!saw_r) {
                cb_prev(cb, 1);
                cb_put(cb, '\r');
                cb_next(cb, 1);
                added++;
            }
            lb_new(&tb->lb_, lb_csize(&tb->lb_));
            if (!saw_r) {
                lb_cinc(&tb->lb_);
            }
            saw_r = false;
            saw_n = false;
        } else if (ch == '\r') {
            saw_r = true;
        } else if  (ch == '\n') {
            saw_n = true;
        }
        cb_next(cb, 1);
    }
    const uint8_t ch = cb_peek(cb);
    if (saw_r) {
        if (ch != '\n') {
            lb_cinc(&tb->lb_);
            cb_put(cb, '\n');
            added++;
        }
        lb_new(&tb->lb_, lb_csize(&tb->lb_));
    } else if  (saw_n) {
        if (!saw_r) {
            lb_cinc(&tb->lb_);
            cb_prev(cb, 1);
            cb_put(cb, '\r');
            cb_next(cb, 1);
            added++;
        }
        lb_new(&tb->lb_, lb_csize(&tb->lb_));
    }

    cb_prev(cb, sz+added);

    // Now move the line buffer back to the first line.
    while (lb_up(&tb->lb_)) ;
    return true;
}


bool tb_load(text_buffer* tb, const char* fname) {
    if (fname == NULL) {
        fname = "aed.txt";
    }

    int fsz = strlen(fname);
    strncpy(tb->fname_, fname, fsz);
    tb->fname_[fsz] = 0;

    uint8_t fh = mos_fopen(tb->fname_, FA_READ | FA_WRITE | FA_CREATE_NEW);
    if (fh == 0) {
        tb->fname_[0] = 0;
        return false;
    }
    FIL* fil = mos_getfil(fh);
    if (fil == NULL) {
        mos_fclose(fh);
        return false;
    }

    bool ok = true;
    int sz = (int) fil->obj.objsize;
    if (sz > 0 && !tb_read(fh, tb, sz)) {
        ok = false;
    }
    mos_fclose(fh);
    return ok;
}

bool tb_valid_file(text_buffer* tb) {
    return tb->fname_[0] != 0;
}
