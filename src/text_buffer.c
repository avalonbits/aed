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

/*
 * The document: what it holds, and the six edits that change it.
 *
 * Opening, closing, the questions anything may ask of a buffer, and put,
 * delete, backspace, newline, delete-line and the two merges. Everything else
 * about a text_buffer lives in a text_buffer_*.c beside this one -- moving
 * through it, searching it, paging it, ranges, and reading and writing files.
 */
#include "text_buffer.h"
#include "text_buffer_int.h"

#include "undo.h"

#include <agon/mos.h>
#include <stdlib.h>
#include <string.h>

int tb_break_len(text_buffer* tb) {
    return eol_len(tb);
}

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
    tb->fname_ = (char*) malloc(TB_FNAME_MAX);
    if (tb->fname_ == NULL) {
        lb_destroy(&tb->lb_);
        cb_destroy(&tb->cb_);
        return NULL;
    }
    tb->x_ = 0;
    tb->fname_[0] = 0;
    tb->dirty_ = false;
    tb->elen_ = 2;
    tb->undo_ = NULL;
    tb->load_dirty_ = false;
    tb->head_lines_ = 0;
    tb->tail_lines_ = 0;
    tb->walker_ = false;
    tb->paged_ = false;
    tb->store_ = NULL;
    tb->wline_ = 0;
    tb->woff_ = 0;

    if (fname != NULL && tb_load(tb, fname) != TB_OK) {
        free(tb->fname_);
        tb->fname_ = NULL;
        lb_destroy(&tb->lb_);
        cb_destroy(&tb->cb_);
        return NULL;
    };
    return tb;
}

void tb_destroy(text_buffer* tb) {
    if (tb->walker_) {
        // A walker owns nothing. Its buffers, and its store, are the ones it
        // was copied from -- see tb_copy -- so freeing them here frees them
        // out from under the cursor that does own them, and the next thing to
        // read the document reads memory that has been handed back.
        //
        // Nothing in the editor destroys a walker today: every one of them is
        // a local that goes out of scope. This is the contract holding rather
        // than a bug being fixed, and it is the one that has teeth, because it
        // fails as a use-after-free instead of as a wrong answer.
        return;
    }
    tbi_drop_store(tb);
    cb_destroy(&tb->cb_);
    lb_destroy(&tb->lb_);
    free(tb->fname_);
    tb->fname_ = NULL;
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

bool tb_eol(text_buffer* tb) {
    const char ch = cb_peek(&tb->cb_);
    return IS_EOL(ch);
}

bool tb_bol(text_buffer* tb) {
    return tb->x_ == 0;
}

char* tb_fname(text_buffer* tb) {
    // NULL on a copy, which has no name of its own -- and on a buffer that has
    // never been given one. Both mean the same thing to a caller.
    if (tb->fname_ == NULL || tb->fname_[0] == 0) {
        return NULL;
    }
    return tb->fname_;
}

bool tb_changed(text_buffer* tb) {
    // With a log attached the question is not "has anything happened" but "is
    // the document what the file holds" -- so undoing back to the last save
    // clears the marker, and redoing forward again brings it back.
    if (tb->undo_ != NULL) {
        return tb->load_dirty_ || !undo_at_save_point(tb->undo_);
    }

    return tb->dirty_;
}

void tbi_saved(text_buffer* tb) {
    tb->dirty_ = false;
    // Whatever the load changed is now on disk too.
    tb->load_dirty_ = false;
    undo_mark_saved(tb->undo_);
}

void tb_set_fname(text_buffer* tb, const char* fname, int sz) {
    if (sz >= TB_FNAME_MAX) {
        sz = TB_FNAME_MAX - 1;
    }
    strncpy(tb->fname_, fname, sz);
    tb->fname_[sz] = 0;
}

// Character ops.
bool tb_put(text_buffer* tb, char ch) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    // Read before the write: the record says where the byte landed, and after
    // the put the cursor has already moved past it.
    const tb_pos at = tb_tell(tb);
    if (!cb_put(&tb->cb_, ch)) {
        return false;
    }
    tb->x_++;
    lb_cinc(&tb->lb_);
    tb->dirty_ = true;
    undo_insert(tb->undo_, at, &ch, 1);

    return true;
}

bool tb_del(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    // Peeked before the delete: cb_del does not hand back what it removed, and
    // undoing a delete means putting the byte back.
    const char gone = cb_peek(&tb->cb_);
    const tb_pos at = tb_tell(tb);
    if (cb_del(&tb->cb_)) {
        lb_cdec(&tb->lb_);
        tb->dirty_ = true;
        undo_delete(tb->undo_, at, &gone, 1);
        return true;
    }
    return false;
}

bool tb_bksp(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    const char gone = cb_prev(&tb->cb_, 1);
    cb_next(&tb->cb_, 1);
    const bool ok = cb_bksp(&tb->cb_);
    if (ok) {
        tb->x_--;
        lb_cdec(&tb->lb_);
        tb->dirty_ = true;
        // Recorded at where the cursor ended up, which is where the byte was.
        // As a backward delete, so a run of them coalesces in the right order:
        // each byte is to the left of the one before it.
        undo_delete_back(tb->undo_, tb_tell(tb), &gone, 1);
    }
    return ok;
}

bool tb_newline(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    // Both halves of the CRLF must fit, or the line index and the text would
    // disagree about where the line ends.
    if (cb_available(&tb->cb_) < eol_len(tb)) {
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

    // One record, not two. The puts below would each record a byte, and undoing
    // those separately would take the LF out on its own and leave the CR --
    // which the line index, built on a two-byte CRLF, cannot represent.
    const tb_pos at = tb_tell(tb);
    const bool was = undo_hold(tb->undo_);
    if (eol_len(tb) == 2) {
        tb_put(tb, '\r');
    }
    tb_put(tb, '\n');
    const bool ok = lb_new(&tb->lb_, tb->x_);
    if (ok) {
        tb->x_ = 0;
    }
    undo_release(tb->undo_, was);
    if (ok) {
        undo_insert(tb->undo_, at, eol_len(tb) == 2 ? "\r\n" : "\n",
                    eol_len(tb));
    }

    return ok;
}

bool tb_del_line(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    if (lb_last(&tb->lb_) && lb_csize(&tb->lb_) == 0) {
        return false;
    }

    tb_home(tb);
    // Stopped by the delete refusing as well as by the count reaching zero.
    // Those are the same question asked of the two buffers, and trusting only
    // the index means that if it ever says a line is longer than the text
    // really is, this spins for ever. That is what a hang on the last line of
    // a document turned out to be -- see tb_del_merge and tb_bksp_merge, which
    // both used to leave it a byte out on a document of bare line feeds.
    while (lb_csize(&tb->lb_) > 0 && tb_del(tb)) {
    }
    if (lb_csize(&tb->lb_) > 0) {
        // The text ran out before the index said it should. Keeping the entry
        // leaves the two agreeing about what is still there, which the rest of
        // the editor can carry on from; dropping it would lose the bytes that
        // are out of the count but still in the buffer.
        tb->dirty_ = true;

        return true;
    }
    lb_del(&tb->lb_);
    tb->dirty_ = true;

    return true;
}

bool tb_del_merge(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    // No settling here, deliberately, though tb_bksp_merge does it at the
    // other end. tb_range_del leans on this failing when the window runs out:
    // it settles for itself and then starts its loop over, because after a
    // slide the cursor that was at the end of the last line in memory is in
    // the middle of one and wants tb_del rather than this.
    if (lb_last(&tb->lb_) || !tb_eol(tb)) {
       return false;
    }

    // Only called at the end of a line, so what is under the cursor is the
    // break -- and a break is as long as this document's breaks are. It used
    // to delete two characters flat, on the reasoning that anything but the
    // last line ends in a CRLF. That stopped being true when a document began
    // keeping the breaks its file had: on one written with bare line feeds the
    // second delete ate the first character of the line being joined on, and
    // left the index a byte heavier than the text. tb_del_line then never
    // finished, because it deletes until the index says the line is empty.
    //
    // One record for however many characters, for the same reason tb_newline
    // groups its puts: half a line break is not a thing the line index can
    // hold.
    const tb_pos at = tb_tell(tb);
    const bool was = undo_hold(tb->undo_);
    for (int i = 0; i < eol_len(tb); i++) {
        tb_del(tb);
    }
    lb_merge_next(&tb->lb_);
    tb->dirty_ = true;
    undo_release(tb->undo_, was);
    undo_delete(tb->undo_, at, eol_len(tb) == 2 ? "\r\n" : "\n", eol_len(tb));

    return true;
}

bool tb_bksp_merge(text_buffer* tb) {
    if (tb->walker_) {
        return false;       // a copy shares the original's buffers
    }
    if (!tb_bol(tb) || tb_ypos(tb) == 1) {
        return false;
    }
    // And there has to be a line above it *in memory* to join onto. The test
    // above is about the document: on a paged one the cursor can sit on the
    // first line the window holds with thousands more behind it in the head,
    // and then this deleted bytes the index had no line to take them off,
    // and lb_merge_prev refused and left the column at minus one.
    //
    // Settling first is what tb_up does in the same position, and brings the
    // line above in when there is one to bring. tb_del_merge guards the other
    // end with lb_last, which is the same question asked downwards.
    if (lb_curr(&tb->lb_) == 0
            && (!tb_settle(tb) || lb_curr(&tb->lb_) == 0)) {
        return false;
    }
    // Straight to cb_bksp rather than tb_bksp, because the line bookkeeping
    // below is not what tb_bksp does -- so this is the one mutation that has to
    // record for itself.
    //
    // As many bytes as this document's break is. It took two flat, on the same
    // reasoning tb_del_merge used: that what is behind the cursor is a CRLF.
    // On a document written with bare line feeds that took the last character
    // of the previous line with it, and left the index counting a byte the
    // text no longer had.
    for (int i = 0; i < eol_len(tb); i++) {
        cb_bksp(&tb->cb_);
    }

    tb->x_ = lb_merge_prev(&tb->lb_, eol_len(tb));
    tb->dirty_ = true;
    undo_delete(tb->undo_, tb_tell(tb), eol_len(tb) == 2 ? "\r\n" : "\n",
                eol_len(tb));
    return true;
}
