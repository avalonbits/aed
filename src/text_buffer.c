#include "text_buffer.h"

#include <agon/vdp_vdu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

text_buffer* tb_init(text_buffer* tb, int mem_kb, const char* filename) {
    char* fname = (char*) malloc(256 * sizeof(char));
    if (fname == NULL) {
        return NULL;
    }

    int line_count = mem_kb << 5;
    int char_count = (mem_kb << 10) - line_count;
    if (!cb_init(&tb->cb_, char_count)) {
        free(fname);
        return NULL;
    }
    if (!lb_init(&tb->lb_, line_count)) {
        free(fname);
        cb_destroy(&tb->cb_);
        return NULL;
    }
    tb->x_ = 0;
    if (filename != NULL) {
        size_t len = strlen(filename);
        if (len >= 255) {
            len = 255;
        }
        strncpy(fname, filename, len);
        fname[len] = 0;
    } else {
        strcpy(fname, "aed.txt");
    }
    tb->fname_ = fname;
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

static void itr_state(text_buffer* buf) {
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

line_itr tb_nline(text_buffer* buf) {
    itr_state(buf);
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
    itr_state(buf);
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

