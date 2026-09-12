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

#ifndef _DOC_STORE_H_
#define _DOC_STORE_H_

#include <stdbool.h>

/*
 * The document either side of what is in memory, on disk.
 *
 *   HEAD                MEM (the gap buffer)            TAIL
 *   [....text....]      [prefix][gap][suffix]    [dead][....text....]
 *                ^                                     ^tail_start
 *
 * Text is never edited on disk. It is only pushed and popped at the end facing
 * memory, which is the property the whole paging design rests on -- see
 * .internal/docs/PAGING.md.
 *
 * This knows nothing about the gap buffer, the line index or the document's
 * name. It is two files and four pushes and pops, so that the awkward part --
 * which end, which offset, how long is each -- is somewhere it can be tested on
 * its own.
 */

// Scratch paths are the document's, with these on the end.
#define STORE_HEAD_SUFFIX ".aedh"
#define STORE_TAIL_SUFFIX ".aedt"
#define STORE_PATH_MAX    280

/*
 * Dead space at the front of TAIL, so that text pushed back has somewhere to go.
 *
 * tail_start moves down when text is pushed back and up when it is consumed. If
 * only what was taken is put back, it cannot go below where it started. Insert
 * text and then scroll up over it and more goes back than came out, so it walks
 * towards zero.
 *
 * It is written, not seeked past. Seeking past the end of a file and writing
 * there looked free -- no headroom transferred -- and the host stubs modelled
 * the gap as zeroes, so it passed everything. On real FatFS `f_lseek` does not
 * extend a file: the seek clips to the end and the write lands at offset zero
 * instead. The document then sits a headroom too early in the file, and the
 * last 64 KiB of it is read off the end as nothing. Found on hardware, where
 * the last thousand lines of a 419 KiB file were simply unreachable.
 *
 * So it costs a headroom-sized write at open: 0.35 s of the card at 182 KiB/s.
 * Smaller would be cheaper and would run out sooner; this is the number the
 * design picked and there is no measurement yet to move it.
 */
#define STORE_HEADROOM (64 * 1024)

typedef struct _doc_store {
    char head_[STORE_PATH_MAX];
    char tail_[STORE_PATH_MAX];

    // HEAD's length is kept here rather than read back off the file. There is
    // no ffs_ftruncate below MOS 2.3.0, so popping cannot shorten the file --
    // it only stops counting the bytes, and the next push writes over them.
    int head_len_;

    // Where the live text starts in TAIL, and where it ends. Everything below
    // tail_start_ is dead: either headroom never used, or text already taken
    // into memory.
    int tail_start_;
    int tail_end_;

    bool open_;
} doc_store;

// Names the scratch files after `base` and creates them both empty. `base` is
// the document's path; an empty or NULL one gets a plain name in the current
// directory, the way the clipboard's scratch file does.
bool store_init(doc_store* st, const char* base);

// Closes and removes both files. Safe on a store that never opened.
void store_destroy(doc_store* st);

// Builds TAIL, front to back, as the document is read in at open. Appends after
// the headroom and after whatever has already been appended.
bool store_tail_append(doc_store* st, const char* buf, int n);

// How much document is on each side. Both are bytes, not lines.
int store_head_bytes(const doc_store* st);
int store_tail_bytes(const doc_store* st);

// Sliding down: memory's front goes to HEAD, TAIL's front comes into memory.
bool store_head_push(doc_store* st, const char* buf, int n);
int  store_tail_pop(doc_store* st, char* buf, int n);

// Sliding up: HEAD's end comes back into memory, memory's back goes to TAIL.
// Both are the exact reverse of the pair above.
int  store_head_pop(doc_store* st, char* buf, int n);
bool store_tail_push(doc_store* st, const char* buf, int n);

// Puts bytes back without writing them: they are still in the file, and only
// the count of what belongs to the document has to move. A pop that came back
// with half a line on the end of it gives that half back this way, which costs
// nothing.
void store_head_rewind(doc_store* st, int n);
void store_tail_rewind(doc_store* st, int n);

// Reads `n` bytes of a side at `at`, without changing what belongs to the
// document. For saving, which walks both files end to end and must leave them
// exactly as it found them -- the document is still open afterwards.
int store_head_read(doc_store* st, int at, char* buf, int n);
int store_tail_read(doc_store* st, int at, char* buf, int n);

// Where the tail's live text begins, for a save to read from.
int store_tail_from(const doc_store* st);

// Whether TAIL still has room in front of it for `n` more bytes to be pushed
// back. False means the headroom is spent and TAIL has to be rebuilt before
// another push -- a slow path, and the caller's problem rather than this one's.
bool store_tail_has_room(const doc_store* st, int n);

#endif  // _DOC_STORE_H_
