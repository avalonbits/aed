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

#ifndef _UNDO_H_
#define _UNDO_H_

#include <stdbool.h>
#include <stdint.h>

#include "text_buffer.h"

// A log of what was done to the document, so it can be done backwards.
//
// Records are written from the five primitives in text_buffer.c that actually
// change the buffers -- tb_put, tb_del, tb_bksp, and the two merges. Every other
// mutation is built out of those, deliberately: tb_range_del deletes one
// character at a time "through the same primitives the DELETE key uses", and
// tb_del_line and tb_insert_span are the same shape. So recording there catches
// paste, cut, select-all-delete and anything added later, with no per-command
// bookkeeping to forget.
//
// The text_buffer holds a pointer to one of these, or NULL. NULL is the off
// switch, and two things rely on it: tb_load runs before the editor attaches a
// log, so the CRLF normalisation it does is not recorded; and replay suspends
// recording so undo does not push its own inverse.

// Room for the deleted text, and how many edits are remembered. 16 KiB plus
// 256 records is about 19 KiB against the roughly 126 KiB nominally free --
// 448 KiB of RAM, less 272 KiB of document buffers, the 8 KiB clipboard and a
// ~42 KiB binary. Worth exposing in the settings file once the feature is
// finished; a guess until someone measures the real headroom.
#define UNDO_TEXT_BYTES (16 * 1024)
#define UNDO_MAX_RECS   256

#define UNDO_INSERT 0
#define UNDO_DELETE 1
// A backspace run grows leftward, so its bytes arrive in reverse document order
// and are stored that way. Replay puts each one back at the same position, which
// rebuilds the forward order: 'c', then 'b' before it, then 'a' before that.
#define UNDO_DELETE_BACK 2

// How much one record will absorb before a new one starts. Without a cap a long
// typing run would undo in a single step and take a paragraph with it.
#define UNDO_RUN_MAX 128

typedef struct _undo_rec {
    // Where the affected bytes start, as the document stood with this edit in
    // place. Only meaningful when applied in order -- see undo_apply.
    int line;
    int x;
    int len;
    // Offset of the saved bytes in the text ring. UNDO_DELETE only: undoing an
    // insert just deletes again, and needs no copy of anything.
    int at;
    uint8_t op;
} undo_rec;

typedef struct _undo {
    // Bytes removed from the document, kept so they can be put back. A ring:
    // when a new record does not fit, the oldest are dropped.
    char* text_;
    int text_size_;
    int text_head_;
    int text_used_;

    undo_rec* recs_;
    int rec_size_;
    int head_;
    int top_;
    // How many of the records held have been applied. Undo walks it down, redo
    // walks it up, and a new edit truncates the log to it -- which is the whole
    // of redo, with no second stack to fall out of step.
    int cur_;

    bool off_;
    // Set by undo_break: the next edit starts a record rather than joining the
    // last one, however adjacent it looks.
    bool broken_;
} undo;

// text_bytes is the room for deleted text, max_recs the number of edits
// remembered. An edit larger than text_bytes cannot be undone at all, and
// clears the log rather than leaving a history with a hole in it.
undo* undo_init(undo* u, int text_bytes, int max_recs);
void undo_destroy(undo* u);

// Called by the model. A NULL log records nothing, which is what makes the
// pointer an off switch rather than something every caller has to check.
void undo_insert(undo* u, tb_pos at, const char* text, int len);
void undo_delete(undo* u, tb_pos at, const char* text, int len);
// As undo_delete, for a backspace: the run grows leftward, so the bytes of a
// coalesced one are held in reverse document order.
void undo_delete_back(undo* u, tb_pos at, const char* text, int len);

// Ends the current run, so the next edit starts a new record. Adjacency already
// breaks a run when the cursor moves away, so this is for the cases position
// cannot see -- a save, for one.
void undo_break(undo* u);

// Redoes the most recently undone record. False when there is nothing to redo.
// Any new edit discards the redo tail, so this only ever follows undo.
bool redo_apply(undo* u, text_buffer* tb);

// Suspends recording and reports whether it was on, for a caller that needs a
// group of primitive edits to land as one record. A line break is the reason
// this exists: tb_newline is two tb_put calls, and undoing them separately would
// remove the LF on its own and leave a CR behind, which the line index -- which
// assumes a two-byte CRLF -- cannot represent.
bool undo_hold(undo* u);
void undo_release(undo* u, bool was_on);

// Undoes the most recent record that has not been undone. False when there is
// nothing left. Recording is suspended for the duration, so replay does not
// push its own inverse.
//
// Records are only valid applied in order: a record's position describes the
// document as it stood immediately after that edit, so undoing out of order
// aims at bytes that have since moved.
bool undo_apply(undo* u, text_buffer* tb);

// Around replay, so undoing does not record its own inverse.
void undo_suspend(undo* u);
void undo_resume(undo* u);
bool undo_recording(undo* u);

// A different document; the positions mean nothing against it.
void undo_clear(undo* u);

// --- for tests ---

// Whether there is anything to undo, or to redo.
bool undo_can_undo(undo* u);
bool undo_can_redo(undo* u);

// Records held, oldest first.
int undo_count(undo* u);
const undo_rec* undo_at(undo* u, int i);
// Bytes of deleted text held.
int undo_text_used(undo* u);
// Copies a record's saved bytes out, unwrapping the ring. Returns false if the
// record saved none.
bool undo_text_of(undo* u, int i, char* out, int max);

#endif  // _UNDO_H_
