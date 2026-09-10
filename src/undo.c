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

#include "undo.h"

#include <stdlib.h>
#include <string.h>

undo* undo_init(undo* u, int text_bytes, int max_recs) {
    if (text_bytes <= 0 || max_recs <= 0) {
        return NULL;
    }
    u->text_ = (char*) malloc((size_t) text_bytes);
    if (u->text_ == NULL) {
        return NULL;
    }
    u->recs_ = (undo_rec*) malloc((size_t) max_recs * sizeof(undo_rec));
    if (u->recs_ == NULL) {
        free(u->text_);
        u->text_ = NULL;
        return NULL;
    }
    u->text_size_ = text_bytes;
    u->rec_size_ = max_recs;
    u->off_ = false;
    undo_clear(u);

    return u;
}

void undo_destroy(undo* u) {
    if (u == NULL) {
        return;
    }
    free(u->text_);
    free(u->recs_);
    u->text_ = NULL;
    u->recs_ = NULL;
    u->text_size_ = 0;
    u->rec_size_ = 0;
    undo_clear(u);
}

void undo_clear(undo* u) {
    if (u == NULL) {
        return;
    }
    u->text_head_ = 0;
    u->text_used_ = 0;
    u->head_ = 0;
    u->top_ = 0;
    u->cur_ = 0;
}

void undo_suspend(undo* u) { if (u != NULL) u->off_ = true; }
void undo_resume(undo* u)  { if (u != NULL) u->off_ = false; }
bool undo_recording(undo* u) { return u != NULL && !u->off_; }

static undo_rec* rec_at(undo* u, int i) {
    return &u->recs_[(u->head_ + i) % u->rec_size_];
}

// Forgets the oldest record, giving back whatever text it was holding.
static void drop_oldest(undo* u) {
    if (u->top_ == 0) {
        return;
    }
    const undo_rec* r = rec_at(u, 0);
    if (r->op == UNDO_DELETE) {
        u->text_head_ = (u->text_head_ + r->len) % u->text_size_;
        u->text_used_ -= r->len;
    }
    u->head_ = (u->head_ + 1) % u->rec_size_;
    u->top_--;
    if (u->cur_ > u->top_) {
        u->cur_ = u->top_;
    }
}

// A new edit invalidates anything that was undone: there is no longer a
// document state for those records to describe.
static void discard_redo(undo* u) {
    u->top_ = u->cur_;
}

static bool make_room(undo* u, int text_len) {
    if (text_len > u->text_size_) {
        return false;   // will never fit, whatever is dropped
    }
    while (u->text_size_ - u->text_used_ < text_len) {
        drop_oldest(u);
    }
    while (u->top_ >= u->rec_size_) {
        drop_oldest(u);
    }

    return true;
}

static void push(undo* u, uint8_t op, tb_pos at, int len, int text_at) {
    undo_rec* r = rec_at(u, u->top_);
    r->op = op;
    r->line = at.line;
    r->x = at.x;
    r->len = len;
    r->at = text_at;
    u->top_++;
    u->cur_ = u->top_;
}

bool undo_hold(undo* u) {
    const bool was = undo_recording(u);
    undo_suspend(u);

    return was;
}

void undo_release(undo* u, bool was_on) {
    if (was_on) {
        undo_resume(u);
    }
}

// Both kinds keep their bytes. A delete needs them to put the text back; an
// insert needs them so the edit can be done again after being undone.
static void record(undo* u, uint8_t op, tb_pos at, const char* text, int len) {
    if (!undo_recording(u) || len <= 0 || text == NULL) {
        return;
    }
    discard_redo(u);
    if (!make_room(u, len)) {
        // Bigger than the whole ring. Keeping the records either side would
        // leave a history that silently skips this edit, so the log goes.
        undo_clear(u);
        return;
    }
    const int at_off = (u->text_head_ + u->text_used_) % u->text_size_;
    const int first = u->text_size_ - at_off < len ? u->text_size_ - at_off : len;
    memcpy(u->text_ + at_off, text, (size_t) first);
    if (first < len) {
        memcpy(u->text_, text + first, (size_t) (len - first));
    }
    u->text_used_ += len;
    push(u, op, at, len, at_off);
}

void undo_insert(undo* u, tb_pos at, const char* text, int len) {
    record(u, UNDO_INSERT, at, text, len);
}

void undo_delete(undo* u, tb_pos at, const char* text, int len) {
    record(u, UNDO_DELETE, at, text, len);
}

// The saved bytes, as up to two runs -- the ring may have wrapped in the middle
// of a record.
static int text_runs(undo* u, const undo_rec* r, const char** a, const char** b) {
    const int first = u->text_size_ - r->at < r->len ? u->text_size_ - r->at : r->len;
    *a = u->text_ + r->at;
    *b = u->text_;

    return first;
}

bool undo_apply(undo* u, text_buffer* tb) {
    if (u == NULL || u->cur_ == 0) {
        return false;
    }
    const undo_rec* r = rec_at(u, u->cur_ - 1);
    tb_pos at;
    at.line = r->line;
    at.x = r->x;

    const bool was = undo_hold(u);
    tb_seek(tb, at);

    if (r->op == UNDO_INSERT) {
        // Put it back by taking it out again. Through the same primitives an
        // ordinary delete uses, so the line index is maintained by code that
        // already gets it right -- a line break is two bytes and needs the
        // merge, not two plain deletes.
        int left = r->len;
        while (left > 0) {
            if (tb_eol(tb)) {
                if (!tb_del_merge(tb)) {
                    break;
                }
                left -= 2;
            } else {
                if (!tb_del(tb)) {
                    break;
                }
                left -= 1;
            }
        }
    } else {
        // tb_insert_span turns a CRLF into a real line break, and carries a
        // trailing CR across calls -- which is exactly what a record split
        // across the end of the ring needs.
        const char* a;
        const char* b;
        const int first = text_runs(u, r, &a, &b);
        bool pending_cr = false;
        tb_insert_span(tb, a, first, &pending_cr);
        if (first < r->len) {
            tb_insert_span(tb, b, r->len - first, &pending_cr);
        }
        if (pending_cr) {
            tb_newline(tb);
        }
        tb_seek(tb, at);
    }

    undo_release(u, was);
    u->cur_--;

    return true;
}

int undo_count(undo* u) {
    return u == NULL ? 0 : u->top_;
}

const undo_rec* undo_at(undo* u, int i) {
    if (u == NULL || i < 0 || i >= u->top_) {
        return NULL;
    }
    return rec_at(u, i);
}

int undo_text_used(undo* u) {
    return u == NULL ? 0 : u->text_used_;
}

bool undo_text_of(undo* u, int i, char* out, int max) {
    const undo_rec* r = undo_at(u, i);
    if (r == NULL || r->op != UNDO_DELETE || r->len > max) {
        return false;
    }
    const int first = u->text_size_ - r->at < r->len ? u->text_size_ - r->at : r->len;
    memcpy(out, u->text_ + r->at, (size_t) first);
    if (first < r->len) {
        memcpy(out + first, u->text_, (size_t) (r->len - first));
    }

    return true;
}
