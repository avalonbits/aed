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
    u->broken_ = false;
    u->saved_ = 0;
    u->last_ch_ = 0;
    u->grouping_ = false;
}

void undo_mark_saved(undo* u) {
    if (u != NULL) {
        u->saved_ = u->cur_;
        // A save ends the run. Otherwise the next keystroke joins the record
        // that was current when the file was written, and undoing back to the
        // save point would take part of the saved text with it.
        u->broken_ = true;
    }
}

bool undo_at_save_point(undo* u) {
    return u != NULL && u->cur_ == u->saved_;
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
    // Indices shift down with the records. Dropping the record the save point
    // sits behind means the log can no longer reach the state on disk, so the
    // point is lost rather than merely moved -- and -1 says so, because 0 is a
    // real position meaning "before everything held".
    if (u->saved_ > 0) {
        u->saved_--;
    } else if (u->saved_ == 0) {
        u->saved_ = -1;
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

void undo_break(undo* u) {
    if (u != NULL) {
        u->broken_ = true;
    }
}

void undo_group_begin(undo* u) {
    if (u != NULL) {
        u->broken_ = true;
        u->grouping_ = true;
    }
}

void undo_group_end(undo* u) {
    if (u != NULL) {
        u->grouping_ = false;
        u->broken_ = true;
    }
}

// Whether this edit continues the last record rather than starting one.
//
// Position does most of the work: an edit that is not adjacent to the last one
// cannot be a continuation of it, which means moving the cursor breaks a run
// without anything having to notice that the cursor moved.
static bool joins_last(undo* u, uint8_t op, tb_pos at, const char* text,
                       int len) {
    if (u->broken_ || u->top_ == 0) {
        return false;
    }
    const undo_rec* last = rec_at(u, u->top_ - 1);
    if (last->op != op || last->line != at.line) {
        return false;
    }
    if (u->grouping_) {
        // One command, one record. Only the line has to match, because a
        // command's edits are contiguous by construction.
        return true;
    }
    if (last->len + len > UNDO_RUN_MAX) {
        return false;
    }
    // A run holds a word and the stops after it: the boundary falls where a
    // stop is followed by something that is not one, in the order the edit
    // travelled. Typing "hello world" is "hello " then "world"; backspacing
    // through it is " world" then "hello". The rule is the same either way
    // because the bytes are considered in the order they were recorded.
    if (tb_is_word_stop(u->last_ch_) && !tb_is_word_stop(text[0])) {
        return false;
    }
    switch (op) {
        case UNDO_INSERT:
            // Typing on: the new byte sits where the run ended.
            return at.x == last->x + last->len;
        case UNDO_DELETE:
            // DELETE eats forward from a cursor that does not move.
            return at.x == last->x;
        case UNDO_DELETE_BACK:
            // BACKSPACE eats leftward, so the run's start moves with it.
            return at.x == last->x - len;
        default:
            return false;
    }
}

// Appends to the ring. Only sound for the most recent record, whose bytes end
// exactly where the next allocation begins -- which is the only record that is
// ever extended.
static void append_text(undo* u, const char* text, int len) {
    const int at_off = (u->text_head_ + u->text_used_) % u->text_size_;
    const int first = u->text_size_ - at_off < len ? u->text_size_ - at_off : len;
    memcpy(u->text_ + at_off, text, (size_t) first);
    if (first < len) {
        memcpy(u->text_, text + first, (size_t) (len - first));
    }
    u->text_used_ += len;
}

// Both kinds keep their bytes. A delete needs them to put the text back; an
// insert needs them so the edit can be done again after being undone.
static void record(undo* u, uint8_t op, tb_pos at, const char* text, int len) {
    if (!undo_recording(u) || len <= 0 || text == NULL) {
        return;
    }
    discard_redo(u);

    if (joins_last(u, op, at, text, len)) {
        // Extending, so only the text has to fit; there is no new record.
        if (u->text_size_ - u->text_used_ >= len) {
            undo_rec* last = rec_at(u, u->top_ - 1);
            append_text(u, text, len);
            last->len += len;
            if (op == UNDO_DELETE_BACK) {
                // The run now starts where this byte was.
                last->x = at.x;
            }
            u->last_ch_ = text[len - 1];

            return;
        }
        // Not enough room to extend without dropping records, and dropping the
        // oldest cannot free the newest. Fall through and start a record.
    }

    if (!make_room(u, len)) {
        // Bigger than the whole ring. Keeping the records either side would
        // leave a history that silently skips this edit, so the log goes.
        undo_clear(u);
        return;
    }
    const int at_off = (u->text_head_ + u->text_used_) % u->text_size_;
    append_text(u, text, len);
    push(u, op, at, len, at_off);
    u->broken_ = false;
    u->last_ch_ = text[len - 1];
}

void undo_insert(undo* u, tb_pos at, const char* text, int len) {
    record(u, UNDO_INSERT, at, text, len);
}

void undo_delete(undo* u, tb_pos at, const char* text, int len) {
    record(u, UNDO_DELETE, at, text, len);
}

void undo_delete_back(undo* u, tb_pos at, const char* text, int len) {
    record(u, UNDO_DELETE_BACK, at, text, len);
}

static char byte_at(undo* u, const undo_rec* r, int i) {
    return u->text_[(r->at + i) % u->text_size_];
}

// Puts a reversed run back. The bytes were stored as backspace met them, right
// to left, so inserting each at the same position rebuilds the original order.
// A run is broken at a line break, so there is never a CRLF in one of these and
// tb_put is enough.
static void insert_reversed(text_buffer* tb, undo* u, const undo_rec* r,
                            tb_pos at) {
    for (int i = 0; i < r->len; i++) {
        tb_seek(tb, at);
        tb_put(tb, byte_at(u, r, i));
    }
}

// Takes len bytes out from `at`, through the same primitives an ordinary delete
// uses so the line index is maintained by code that already gets it right.
static void delete_span(text_buffer* tb, int len) {
    int left = len;
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
}

// Puts a forward run back, letting tb_insert_span turn a CRLF into a real line
// break. Its pending_cr also carries a record split across the end of the ring.
static void insert_forward(text_buffer* tb, undo* u, const undo_rec* r) {
    const int first = u->text_size_ - r->at < r->len
        ? u->text_size_ - r->at : r->len;
    bool pending_cr = false;
    tb_insert_span(tb, u->text_ + r->at, first, &pending_cr);
    if (first < r->len) {
        tb_insert_span(tb, u->text_, r->len - first, &pending_cr);
    }
    if (pending_cr) {
        tb_newline(tb);
    }
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
        delete_span(tb, r->len);
    } else if (r->op == UNDO_DELETE_BACK) {
        insert_reversed(tb, u, r, at);
        tb_seek(tb, at);
    } else {
        insert_forward(tb, u, r);
        tb_seek(tb, at);
    }

    undo_release(u, was);
    u->cur_--;
    u->broken_ = true;

    return true;
}

bool redo_apply(undo* u, text_buffer* tb) {
    if (u == NULL || u->cur_ >= u->top_) {
        return false;
    }
    const undo_rec* r = rec_at(u, u->cur_);
    tb_pos at;
    at.line = r->line;
    at.x = r->x;

    const bool was = undo_hold(u);
    tb_seek(tb, at);

    if (r->op == UNDO_INSERT) {
        // Doing it again, which is why an insert keeps its bytes at all.
        insert_forward(tb, u, r);
    } else {
        // Both kinds of delete took a run starting at the record's position,
        // whichever end the cursor was working from.
        delete_span(tb, r->len);
    }

    undo_release(u, was);
    u->cur_++;
    u->broken_ = true;

    return true;
}

bool undo_can_undo(undo* u) {
    return u != NULL && u->cur_ > 0;
}

bool undo_can_redo(undo* u) {
    return u != NULL && u->cur_ < u->top_;
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
    if (r == NULL || r->len > max) {
        return false;
    }
    for (int k = 0; k < r->len; k++) {
        out[k] = byte_at(u, r, k);
    }

    return true;
}
