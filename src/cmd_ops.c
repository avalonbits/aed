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

#include "cmd_ops.h"

#include <stddef.h>

#include "config.h"
#include "editor.h"
#include <string.h>

#include "text_buffer.h"
#include "screen.h"
#include "user_input.h"

#define SCR(ed) screen* scr = &ed->scr_
#define UI(ed) user_input* ui = &ed->ui_
#define TB(ed) text_buffer* tb = &ed->buf_

/*
 * A row, joined and lexed, so it can be coloured.
 *
 * SYN_ROW_MAX bounds how much of a long line is looked at: past that column a
 * row paints in the document's own colour. The screen is at most 128 columns,
 * so this only bites on a line scrolled a long way sideways.
 *
 * SYN_ROW_RUNS bounds the tokens in one row. add_run merges neighbours of the
 * same class and lets the last run swallow the rest when it runs out, so
 * overflow costs colour rather than correctness -- and the cost of a colour
 * change is why a cap is wanted at all (test/probes/vducost.c).
 *
 * Static rather than on the stack: 512 bytes of frame is wider than an eZ80
 * index displacement reaches. One row is painted at a time.
 *
 * The scan that works out what a row begins inside gets a buffer of its own
 * rather than borrowing this one. It used to borrow it, on the grounds that it
 * never runs while a row is being painted -- which was true of the paint paths
 * and false of cmd_sync_cursor_colour, where the row was assembled first and
 * the scan then overwrote it. The line came out as the head of whichever row
 * was assembled last, and the cursor took its colour from that.
 */
#define SYN_ROW_MAX  512
#define SYN_ROW_RUNS 64

static char synRow_[SYN_ROW_MAX];
static char synScan_[SYN_ROW_MAX];
static tok_run synRuns_[SYN_ROW_RUNS];

static int row_bytes(const split_line* ln, char* buf, int max) {
    int n = ln->psz_;
    if (n > max) {
        n = max;
    }
    if (n > 0) {
        memcpy(buf, ln->prefix_, (size_t) n);
    }
    int m = ln->ssz_;
    if (n + m > max) {
        m = max - n;
    }
    if (m > 0) {
        memcpy(buf + n, ln->suffix_, (size_t) m);
    }

    return n + m;
}

/*
 * Hands the screen the colours for the row about to be painted, and returns
 * what that row leaves open for the row below it.
 *
 * With no grammar the screen is told to paint plainly, which is also what it
 * does for any paint that does not come through here -- scr_paint_span drops
 * the runs when it is done, so no row can inherit another's colouring.
 */
/*
 * After an edit has repainted its own row, the rows below it may be inside
 * something different. Repaints them when they are, and leaves the screen
 * alone when they are not, which is almost always.
 */
// What the row under `y` begins in, or 0 when there is no such row to ask about.
static char row_below_state(editor* ed, char y) {
    SCR(ed);
    const char next = (char)(y + 1);
    if (ed->synTopLine_ == 0 || next >= scr->bottomY_ || next >= SCR_MAX_ROWS) {
        return 0;
    }

    return ed->rowSyn_[next];
}

/*
 * An edit changed what its row leaves open -- the second character of a
 * comment opener, or the character that closed one -- so the rows under it are
 * inside something else now. Painting the row recorded the new answer; this
 * notices that it moved and paints the rest of the screen.
 */
static void resync_rows_below(editor* ed, char y, char was) {
    SCR(ed);
    if (!syn_crosses_lines(&ed->syn_) || ed->synTopLine_ == 0) {
        return;
    }
    const char next = (char)(y + 1);
    if (next >= scr->bottomY_ || next >= SCR_MAX_ROWS
            || ed->rowSyn_[next] == was) {
        return;
    }
    cmd_repaint_rows(ed, next, (char)(scr->bottomY_ - 1));
}

static int state_at_row(editor* ed, text_buffer* tb, char ypos);
static int top_line(screen* scr, text_buffer* tb);

/*
 * The screen asking what colours a row it is about to paint.
 *
 * Everything the answer needs is in rowSyn_: what that row begins inside. The
 * text comes from the screen, because the screen has it in hand, and what the
 * row leaves goes into the row below -- so painting a screen top to bottom
 * chains the answers along without anybody keeping a running total.
 *
 * This is the only place a row's colouring is worked out. It used to be
 * pushed, by each of a dozen paint sites, and the ones that forgot painted
 * plainly with nothing to say they had.
 */
static int ed_colour_row(void* ctx, char ypos, const char* pre, int presz,
                         const char* suf, int sufsz, const tok_run** runs) {
    editor* ed = (editor*) ctx;
    if (!ed->syn_.loaded || ypos < 0 || ypos >= SCR_MAX_ROWS) {
        return 0;
    }
    const split_line ln = { presz, (char*) pre, sufsz, (char*) suf };
    const int len = row_bytes(&ln, synRow_, SYN_ROW_MAX);
    /*
     * Read from the model, never asked of it.
     *
     * Asking means checking whether the model still describes the screen, and
     * the answer is worked out from where the cursor is -- which, during a
     * paint, is halfway through an edit. Every return and every join decided
     * the screen was stale and lexed all of it again, twice, and the answers
     * were taken against a view that had not finished moving. The model is
     * brought up to date at the start of an operation, where the view and the
     * document agree; by the time a row is painted the answer is already here.
     */
    const int in = (ed->synTopLine_ != 0) ? ed->rowSyn_[ypos] : SYN_STATE_NONE;
    int out = SYN_STATE_NONE;
    const int n = syn_lex(&ed->syn_, synRow_, len, in, &out,
                          synRuns_, SYN_ROW_RUNS);
    if (ed->synTopLine_ != 0 && ypos + 1 < SCR_MAX_ROWS
            && ypos + 1 < ed->scr_.bottomY_) {
        ed->rowSyn_[ypos + 1] = (char) out;
    }
    *runs = synRuns_;

    return n;
}

/*
 * What a row leaves open, without painting it.
 *
 * For the handful of moments when the model has to be brought up to date
 * before anything is drawn: an edit has changed the document and the rows are
 * about to move, and the answer for the row below depends on the row above as
 * it now is.
 */
static int row_leaves(editor* ed, const char* pre, int presz,
                      const char* suf, int sufsz, int in) {
    if (!ed->syn_.loaded) {
        return SYN_STATE_NONE;
    }
    const split_line ln = { presz, (char*) pre, sufsz, (char*) suf };
    const int len = row_bytes(&ln, synScan_, SYN_ROW_MAX);
    int out = SYN_STATE_NONE;
    syn_lex(&ed->syn_, synScan_, len, in, &out, NULL, 0);

    return out;
}

/*
 * The screen asking what colour the cell under the cursor belongs in.
 *
 * Worked out here and now rather than handed over in advance, so it describes
 * where the cursor is rather than where it was when something last thought to
 * say. -1 leaves the cell in the document's own colour.
 */
static char ed_colour_cell(void* ctx) {
    editor* ed = (editor*) ctx;
    if (!ed->syn_.loaded) {
        return -1;
    }
    text_buffer* tb = &ed->buf_;
    const split_line ln = tb_curr_line(tb);
    const int len = row_bytes(&ln, synRow_, SYN_ROW_MAX);
    /*
     * Read from the model rather than asked of it. This runs in the middle of
     * commands, while the cursor has moved and the view has not caught up, and
     * a question at that moment can decide the whole screen is stale and work
     * it out again -- against a view that is halfway through changing. The
     * answer would be wrong and the model would keep it.
     *
     * Nothing else uses it, so the cost of being cold here is one cell drawn
     * in the document's colour until the next paint fills the model in.
     */
    const int in = (ed->synTopLine_ != 0 && ed->scr_.currY_ < SCR_MAX_ROWS)
                 ? ed->rowSyn_[ed->scr_.currY_] : SYN_STATE_NONE;
    int out = SYN_STATE_NONE;
    const int n = syn_lex(&ed->syn_, synRow_, len, in, &out,
                          synRuns_, SYN_ROW_RUNS);
    // tb_curr_line splits the cursor's row at the cursor, so the prefix is how
    // many bytes into the line it is.
    for (int i = 0; i < n; i++) {
        if (ln.psz_ < synRuns_[i].end) {
            return theme_colour(&ed->theme_, (tok_class) synRuns_[i].cls);
        }
    }

    return -1;
}

void ed_attach_colourer(editor* ed) {
    scr_set_colourer(&ed->scr_, ed_colour_row, ed_colour_cell, ed);
}

// Walks a copy of the document forward, a line at a time. syn_state_before
// asks for lines in order, which is what lets this be a walk rather than a
// seek per line.
typedef struct _back_scan {
    text_buffer cp;
    int at;                     // the document line cp is on, counting from 1
} back_scan;

static int back_get(void* ctx, int y, char* buf, int max) {
    back_scan* bs = (back_scan*) ctx;
    const int want = y + 1;     // syn_state_before counts lines from zero
    while (bs->at < want) {
        // tb_down answers with the character it landed on, so whether it moved
        // is a question for tb_ypos. Comparing the two reads a letter as a
        // line number, and 'A' is line 65.
        tb_down(&bs->cp);
        const int now = tb_ypos(&bs->cp);
        if (now == bs->at) {
            return -1;          // the document ended first
        }
        bs->at = now;
    }
    const split_line ln = tb_curr_line(&bs->cp);

    return row_bytes(&ln, buf, max);
}

/*
 * What the top line on screen begins inside, for a view that has just arrived
 * there.
 *
 * Costs nothing for a grammar with nothing that crosses a line, which is every
 * grammar but C -- syn_state_before answers those without reading a line.
 */
static int top_state(editor* ed, int line) {
    if (!syn_crosses_lines(&ed->syn_) || line <= 1) {
        return SYN_STATE_NONE;
    }
    // Static, as font_list's DIR is and for the same reason: a text_buffer on
    // the stack joins the frame of whatever this is inlined into and takes it
    // past the 128 bytes an eZ80 index displacement reaches. One scan at a
    // time, and it never runs while a row is being painted.
    static back_scan bs;
    tb_copy(&bs.cp, &ed->buf_);
    int from = line - SYN_LOOKBACK;
    if (from < 1) {
        from = 1;
    }
    tb_pos p;
    p.line = from;
    p.x = 0;
    tb_seek(&bs.cp, p);
    bs.at = tb_ypos(&bs.cp);

    return syn_state_before(&ed->syn_, line - 1, back_get, &bs,
                            synScan_, SYN_ROW_MAX);
}

/*
 * What the row at `ypos` begins inside.
 *
 * The rows above it are on screen and so are in memory, so this walks down
 * from the top of the screen lexing as it goes rather than reading anything.
 * Bounded by the screen: at most a screenful of lines, and none at all for a
 * grammar with nothing that crosses a line -- which is why typing in an
 * assembly or BASIC file costs exactly what it did before.
 */
static int state_at_row(editor* ed, text_buffer* tb, char ypos);

static void fill_screen(editor* ed, text_buffer* tb) {
    // No clear first. scr_write_line pads every row it paints to the full width,
    // so it covers whatever was there -- clearing the area and then painting
    // over all of it writes the whole text area twice, and the blank moment
    // between the two is the flash on every cut, paste and refresh.
    //
    // The clear was doing one thing besides that: covering the rows past the end
    // of the document, which the loop below stops before. Those are blanked
    // explicitly now. It also used to erase the footer, whose viewport it
    // overlapped; not erasing it is one fewer full-width row per refresh.
    SCR(ed);
    char ypos = scr->topY_;
    char tpos = tb_ypos(tb);

    /*
     * This is where the screen's answers start from. Only the top row has to
     * be worked out -- what it begins inside, which for a view that has landed
     * somewhere new means reading back. Every row under it begins in what the
     * row above leaves, and each paint records that for the row below as it
     * goes, so the rest chains itself.
     */
    if (scr->topY_ < SCR_MAX_ROWS) {
        ed->rowSyn_[scr->topY_] = (char) top_state(ed, tpos);
    }
    /*
     * Recorded as the reader works it out, rather than as the line this walk
     * happens to start on. The two agree for every caller here -- refresh
     * walks the cursor up to the top row first -- and writing it the reader's
     * way means a paint cannot disagree with itself and start the whole
     * screen over in the middle of being painted.
     */
    ed->synTopLine_ = top_line(scr, &ed->buf_);
    ed->synLines_ = tb_ymax(tb);

    for (; ypos < scr->bottomY_; ypos++) {
        const split_line ln = tb_curr_line(tb);
        scr_paint_row(scr, ypos, ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_);

        tb_down(tb);
        const int npos = tb_ypos(tb);
        if (npos == tpos) {
            ypos++;
            break;
        }
        tpos = npos;
    }

    for (; ypos < scr->bottomY_; ypos++) {
        if (ypos < SCR_MAX_ROWS && ypos > scr->topY_) {
            // A blank row leaves what it was given.
            ed->rowSyn_[ypos] = ed->rowSyn_[ypos - 1];
        }
        scr_write_line(scr, ypos, NULL, 0);
    }
}


static void refresh_screen(editor* ed, text_buffer* tb) {
    SCR(ed);
    char currY = scr->currY_;
    char currX = scr->currX_;

    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    while (tb_ypos(&cp) > 1 &&  scr->currY_ > scr->topY_) {
        tb_up(&cp);
        scr->currY_--;
    }
    fill_screen(ed, &cp);

    scr->currY_ = currY;
    scr->currX_ = currX;
    scr_sync_cursor(scr);
}

// The horizontal origin is screen-wide, so a scroll invalidates every visible
// row, not just the one the cursor is on. Only the controller can repaint them:
// the view has no way to walk the document.
// Paints `count` screen columns starting at `sx`, one cell per visible row.
// Used after a VDP region scroll has shifted the text area sideways: only the
// newly exposed columns are unknown, the rest moved with the hardware.
static void fill_columns(editor* ed, text_buffer* tb, char sx, int count) {
    SCR(ed);
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    int up = scr->currY_ - scr->topY_;
    while (up-- > 0 && tb_ypos(&cp) > 1) {
        tb_up(&cp);
    }

    int tpos = tb_ypos(&cp);
    for (char ypos = scr->topY_; ypos < scr->bottomY_; ypos++) {
        const split_line ln = tb_curr_line(&cp);
        for (int i = 0; i < count; i++) {
            const char g = scr_glyph_at_split(scr, ln.prefix_, ln.psz_,
                                              ln.suffix_, ln.ssz_,
                                              scr->originX_ + sx + i);
            scr_put_at(scr, (char)(sx + i), ypos, g);
        }

        tb_down(&cp);
        const int npos = tb_ypos(&cp);
        if (npos == tpos) {
            break;
        }
        tpos = npos;
    }
}

// The horizontal origin is screen-wide, so a scroll invalidates every visible
// row. For a short hop the VDP can shift the whole text area itself and only
// the exposed columns need drawing; past that a full repaint is cheaper.
// `edited` says whether the current row's text changed. The region scroll moves
// pixels, which only reproduces the document while the text is unchanged, so an
// edit that also scrolls must have its row redrawn from the buffer afterwards.
static void resync_after_scroll(editor* ed, text_buffer* tb, char to_ch,
                                int delta, bool edited) {
    SCR(ed);
    if (delta == 0) {
        return;
    }

    if (delta > SCR_MAX_HSCROLL || delta < -SCR_MAX_HSCROLL) {
        refresh_screen(ed, tb);   // reads the document, so already correct
    } else {
        scr_scroll_h(scr, delta);
        if (delta > 0) {
            fill_columns(ed, tb, (char)(scr->cols_ - delta), delta);
        } else {
            fill_columns(ed, tb, 0, -delta);
        }
        if (edited) {
            split_line ln = tb_curr_line(tb);
            scr_paint_row(scr, scr->currY_, ln.prefix_, ln.psz_,
                          ln.suffix_, ln.ssz_);
        }
    }
    scr_sync_cursor(scr);
    scr_show_cursor_ch(scr, to_ch);
}

static bool update_fname(screen* scr, user_input* ui, text_buffer* tb, char* prefill) {
    char* fname;
    int sz;
    RESPONSE res = ui_text(ui, scr, "File name: ", prefill, &fname, &sz);
    if (res == CANCEL_OPT) {
        return false;
    } else if (res == YES_OPT) {
        tb_set_fname(tb, fname, sz);
        return true;
    }
    return false;
}

bool cmd_save(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    if (!tb_changed(tb)) {
        return true;
    }

    if (!tb_valid_file(tb) && !update_fname(scr, ui, tb, NULL)) {
        return false;
    }

    return tb_save(tb);
}


void cmd_save_as(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    if (update_fname(scr, ui, tb, tb_fname(tb))) {
        tb_save(tb);
    }
}


bool cmd_quit(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    if (!tb_changed(tb)) {
        return true;
    }

    RESPONSE res = ui_dialog(ui, scr, "Save before quit?");
    if (res == NO_OPT) {
        return true;
    }
    if (res == CANCEL_OPT) {
        return false;
    }

    return cmd_save(ed);
}

// The screen row showing the document's first visible line. Not stored: the
// cursor's document line and its screen row give it away, and one derived
// number cannot drift out of step with the two it comes from.
static int top_line(screen* scr, text_buffer* tb) {
    return tb_ypos(tb) - (scr->currY_ - scr->topY_);
}

void cmd_selection_range(editor* ed, tb_pos* from, tb_pos* to) {
    tb_pos a = ed->anchor_;
    tb_pos b = tb_tell(&ed->buf_);
    if (tb_cmp(a, b) > 0) {
        const tb_pos t = a;
        a = b;
        b = t;
    }
    *from = a;
    *to = b;
}

// The columns of `ln` that the selection covers, as [from, to). Empty when
// none of it does.
static void row_selection(editor* ed, int line, const split_line* ln,
                          int* from, int* to) {
    *from = 0;
    *to = 0;
    if (!ed->selecting_) {
        return;
    }

    SCR(ed);
    tb_pos a;
    tb_pos b;
    cmd_selection_range(ed, &a, &b);
    if (line < a.line || line > b.line) {
        return;
    }

    const int len = ln->psz_ + ln->ssz_;
    *from = (line == a.line)
        ? scr_column_of_n(scr, ln->prefix_, ln->psz_, ln->suffix_, ln->ssz_, a.x)
        : 0;
    if (line == b.line) {
        *to = scr_column_of_n(scr, ln->prefix_, ln->psz_,
                              ln->suffix_, ln->ssz_, b.x);
    } else {
        // Past the end of the text by one, so a line break inside the selection
        // shows as a highlighted cell rather than as nothing at all.
        *to = scr_column_of_n(scr, ln->prefix_, ln->psz_,
                              ln->suffix_, ln->ssz_, len) + 1;
    }
}

/*
 * Fills rowSyn_ for every row on screen, from one walk down the document.
 *
 * The walk is the expensive part -- a line of C costs about a millisecond and
 * a half to lex -- so it is done once for a view and read back per row, rather
 * than once per row painted.
 */
static void fill_row_states(editor* ed, text_buffer* tb, int top) {
    SCR(ed);
    // Static for the same reason as the scan above: this is inlined into
    // cmd_repaint_rows, which already holds a text_buffer of its own.
    static text_buffer cp;

    /*
     * Where to start, and in what.
     *
     * Reading back to find out what the top line begins inside costs
     * SYN_LOOKBACK lines -- 200 of them, four times what a screenful is, and
     * the larger half of what working the screen out again costs at all.
     *
     * A view that has moved *down* needs none of it. What its old top line
     * began in is already known, and the lines between then and now are on
     * screen, so walking forward from there is a lex per line moved -- one,
     * for a scroll. The read-back is left for a view that arrived somewhere
     * new: a jump, a slide, or a first paint.
     */
    int from = top;
    int state;
    const int moved = top - ed->synTopLine_;
    if (ed->synTopLine_ != 0 && moved >= 0 && moved <= SCR_MAX_ROWS
            && scr->topY_ < SCR_MAX_ROWS) {
        from = ed->synTopLine_;
        state = ed->rowSyn_[scr->topY_];
    } else {
        state = top_state(ed, top);
    }

    tb_copy(&cp, tb);
    tb_pos p;
    p.line = from;
    p.x = 0;
    tb_seek(&cp, p);
    int prev = tb_ypos(&cp);
    // Forward to the line the top row actually shows.
    while (prev < top) {
        const split_line ln = tb_curr_line(&cp);
        const int len = row_bytes(&ln, synScan_, SYN_ROW_MAX);
        syn_lex(&ed->syn_, synScan_, len, state, &state, NULL, 0);
        tb_down(&cp);
        if (tb_ypos(&cp) == prev) {
            break;
        }
        prev = tb_ypos(&cp);
    }
    for (char y = scr->topY_; y < scr->bottomY_ && y < SCR_MAX_ROWS; y++) {
        ed->rowSyn_[y] = (char) state;
        const split_line ln = tb_curr_line(&cp);
        const int len = row_bytes(&ln, synScan_, SYN_ROW_MAX);
        syn_lex(&ed->syn_, synScan_, len, state, &state, NULL, 0);
        tb_down(&cp);
        if (tb_ypos(&cp) == prev) {
            // Past the end of the document. The rows below it are blank, and a
            // blank row leaves what it was given.
            for (char rest = (char)(y + 1);
                 rest < scr->bottomY_ && rest < SCR_MAX_ROWS; rest++) {
                ed->rowSyn_[rest] = (char) state;
            }
            break;
        }
        prev = tb_ypos(&cp);
    }
    ed->synTopLine_ = top;
    ed->synLines_ = tb_ymax(tb);
}

/*
 * A line was inserted, splitting the line shown on row `y`; the half that moved
 * down begins in `below`.
 *
 * The rows above y are unchanged and so is row y, whose line still begins where
 * it did. The rows under it show what the row above them showed, so what is
 * known about them is shifted rather than worked out again.
 *
 * Working a screen out again costs a lex of every row on it -- about 140
 * milliseconds of C on an Agon, which is what pressing return used to pay.
 */
static void rows_inserted(editor* ed, char y, int below) {
    SCR(ed);
    if (ed->synTopLine_ == 0 || !syn_crosses_lines(&ed->syn_)) {
        return;
    }
    int last = scr->bottomY_ - 1;
    if (last >= SCR_MAX_ROWS) {
        last = SCR_MAX_ROWS - 1;
    }
    if (y < scr->topY_ || y >= last) {
        ed->synTopLine_ = 0;    // the split is at or past the bottom row
        return;
    }
    for (int r = last; r > y + 1; r--) {
        ed->rowSyn_[r] = ed->rowSyn_[r - 1];
    }
    ed->rowSyn_[y + 1] = (char) below;
    ed->synLines_++;
}

static int state_at_row(editor* ed, text_buffer* tb, char ypos);

/*
 * What the line above `at` leaves, given what that line begins in.
 *
 * Its own function so that the buffer it needs does not join the frame of
 * whatever rows_shifted_up is inlined into -- cmd_down already holds one, and
 * the two together take it past the 128 bytes an eZ80 index displacement
 * reaches (test/frames.sh).
 */
static int state_after(editor* ed, text_buffer* at, int began) {
    static text_buffer up;
    tb_copy(&up, at);
    const int here = tb_ypos(&up);
    tb_up(&up);
    if (tb_ypos(&up) == here) {
        return SYN_STATE_NONE;      // nothing above it
    }
    tb_home(&up);
    const split_line prev = tb_curr_line(&up);
    const int len = row_bytes(&prev, synScan_, SYN_ROW_MAX);
    int out = SYN_STATE_NONE;
    syn_lex(&ed->syn_, synScan_, len, began, &out, NULL, 0);

    return out;
}

/*
 * The rows from `first` down moved up by one, and a line that was off screen
 * arrived at the bottom. `at_bottom` is a buffer sitting on that new line, and
 * `first_state` is what row `first` now begins in, or -1 to take what the
 * shift moved into it.
 *
 * Returns what the bottom row begins in, and leaves the answers describing the
 * screen as it now is. The bottom is the only row that has to be worked out:
 * what the row above it leaves is what it begins in, and that row's beginning
 * is what the shift just moved down into it.
 *
 * Falls back to working the whole screen out again when there is nothing to
 * shift, which is a lex of every row -- about a tenth of a second of C.
 *
 * Kept out of line on purpose. Inlined into cmd_down, which already holds a
 * text_buffer of its own, it takes that frame to 133 bytes -- past the 128 an
 * eZ80 index displacement reaches, which charges an address computation to
 * every local the function has (test/frames.sh).
 */
__attribute__((noinline))
static int rows_shifted_up(editor* ed, text_buffer* at_bottom, char first,
                           int first_state) {
    SCR(ed);
    if (!syn_crosses_lines(&ed->syn_)) {
        return SYN_STATE_NONE;
    }
    int last = scr->bottomY_ - 1;
    if (last >= SCR_MAX_ROWS) {
        last = SCR_MAX_ROWS - 1;
    }
    if (ed->synTopLine_ == 0 || first < scr->topY_ || first > last
            || last <= scr->topY_) {
        ed->synTopLine_ = 0;

        return state_at_row(ed, at_bottom, (char) last);
    }
    for (int r = first; r < last; r++) {
        ed->rowSyn_[r] = ed->rowSyn_[r + 1];
    }
    if (first_state >= 0) {
        // The first row that moved does not always show what the row under it
        // showed: joining two lines leaves one line where two were, and what
        // that line leaves is not what either of them left.
        ed->rowSyn_[first] = (char) first_state;
    }

    ed->rowSyn_[last] = (char) state_after(ed, at_bottom,
                                           ed->rowSyn_[last - 1]);

    return ed->rowSyn_[last];
}

static int state_at_row(editor* ed, text_buffer* tb, char ypos) {
    SCR(ed);
    if (!syn_crosses_lines(&ed->syn_)) {
        return SYN_STATE_NONE;
    }
    if (ypos < scr->topY_ || ypos >= SCR_MAX_ROWS) {
        return SYN_STATE_NONE;
    }
    const int top = top_line(scr, tb);
    if (ed->synTopLine_ != top || ed->synLines_ != tb_ymax(tb)) {
        fill_row_states(ed, tb, top);
    }

    return ed->rowSyn_[ypos];
}


void cmd_repaint_rows(editor* ed, char fromY, char toY) {
    SCR(ed);
    TB(ed);

    if (fromY < scr->topY_) {
        fromY = scr->topY_;
    }
    if (toY >= scr->bottomY_) {
        toY = scr->bottomY_ - 1;
    }

    text_buffer cp;
    tb_copy(&cp, tb);
    tb_pos start;
    start.line = top_line(scr, tb) + (fromY - scr->topY_);
    start.x = 0;
    tb_seek(&cp, start);
    if (tb_ypos(&cp) != start.line) {
        // The range begins past the end of the document. tb_seek clamps to the
        // last line, and painting from there would put that line on screen a
        // second time, in a row that should be blank. Only visible when the
        // last line has text on it: a document ending in a newline has an empty
        // line after it, and painting that looks exactly like blanking the row.
        for (char blank = fromY; blank <= toY; blank++) {
            scr_write_line(scr, blank, NULL, 0);
        }
        scr_sync_cursor(scr);

        return;
    }

    // Brings the model up to date if the view has moved since it was written.
    // Here rather than inside the paints: the view and the document agree at
    // this point, and a paint is the one moment they may not.
    (void) state_at_row(ed, tb, fromY);

    char y = fromY;
    char last = toY;
    for (; y <= last; y++) {
        const split_line ln = tb_curr_line(&cp);
        int from = 0;
        int to = 0;
        row_selection(ed, tb_ypos(&cp), &ln, &from, &to);
        /*
         * What this row leaves is what the next begins in, and painting it
         * records that. When it differs from what the next row was painted
         * with -- a comment just opened or closed here -- the rest of the
         * screen is wrong, so the range carries on to the bottom. That is what
         * makes typing the second character of a comment opener recolour
         * everything below it.
         */
        const char was = (y + 1 < SCR_MAX_ROWS && y + 1 < scr->bottomY_)
                       ? ed->rowSyn_[y + 1] : 0;
        scr_write_line_sel_split(scr, y, ln.prefix_, ln.psz_,
                                 ln.suffix_, ln.ssz_, from, to);
        if (ed->synTopLine_ != 0 && y + 1 < SCR_MAX_ROWS
                && y + 1 < scr->bottomY_ && ed->rowSyn_[y + 1] != was) {
            last = (char)(scr->bottomY_ - 1);
        }

        const int prev = tb_ypos(&cp);
        tb_down(&cp);
        if (tb_ypos(&cp) == prev) {
            y++;
            break;      // ran out of document
        }
    }

    // Rows the document no longer reaches. This used to stop at the break and
    // leave whatever was there, which was harmless only because every caller
    // either cleared first or was repainting a document that had not shrunk.
    // A cut shrinks it, so those rows would keep the lines that used to be on
    // them.
    for (; y <= toY; y++) {
        scr_write_line(scr, y, NULL, 0);
    }
    scr_sync_cursor(scr);
}

void cmd_repaint_span(editor* ed, char y, int from_col, int to_col) {
    SCR(ed);
    TB(ed);

    if (y < scr->topY_ || y >= scr->bottomY_) {
        return;
    }

    text_buffer cp;
    tb_copy(&cp, tb);
    tb_pos start;
    start.line = top_line(scr, tb) + (y - scr->topY_);
    start.x = 0;
    tb_seek(&cp, start);

    const split_line ln = tb_curr_line(&cp);
    int from = 0;
    int to = 0;
    row_selection(ed, tb_ypos(&cp), &ln, &from, &to);
    scr_write_line_span_split(scr, y, ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_,
                              from, to, from_col, to_col);
    scr_sync_cursor(scr);
}

// Which screen row the cursor belongs on, after the document has changed by
// more than a character. Walks back from the cursor towards the top of the
// document, one row per line, until it runs out of either.
static void place_cursor_row(editor* ed) {
    SCR(ed);
    TB(ed);

    scr->currY_ = scr->topY_;
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    while (tb_ypos(&cp) > 1 && scr->currY_ < scr->bottomY_ - 1) {
        tb_up(&cp);
        scr->currY_++;
    }
}

// Puts the view back together after the document changed under it by more than
// a character. The cursor is wherever the model left it; the screen row it
// should appear on is worked out by walking back up from it.
static void reshow(editor* ed) {
    SCR(ed);
    TB(ed);

    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
    refresh_screen(ed, tb);
    scr_show_cursor_ch(scr, tb_peek(tb));
}

// Where the cursor's row belongs after a selection delete: the row the top of
// the selection is already on. Everything between there and the cursor collapses
// into that row, so the row moves up by exactly the number of lines the
// selection spanned -- and for a selection inside one line, it does not move.
//
// Deriving it instead, as place_cursor_row does, walks up from the cursor as far
// as the screen allows. On any document taller than the screen that lands the
// cursor on the last text row, and because the line shown at the top is derived
// from the cursor's row, the whole view moves with it: cutting two words in the
// middle of a 300-line file scrolled the document twenty lines.
// Returns false when the row could not be kept and the view has therefore moved,
// which means every row on screen is wrong and only a full repaint will do.
static bool keep_cursor_row(editor* ed, int top_line, int cursor_line) {
    SCR(ed);

    bool kept = true;
    int y = scr->currY_ - (cursor_line - top_line);
    if (y < scr->topY_) {
        // The selection began above the window, so there is no row to keep.
        // Put the join on the top row and show the document from there.
        y = scr->topY_;
        kept = false;
    }
    if (y - scr->topY_ > top_line - 1) {
        // Never leave more rows above the cursor than the document has lines to
        // fill them with, or the view shows blank rows above line 1.
        y = scr->topY_ + top_line - 1;
        kept = false;
    }
    scr->currY_ = (char) y;

    return kept;
}

// Repaints after an edit that changed how many lines the document has, without
// redrawing the text area.
//
// Such an edit does one thing to the screen, however many lines it spanned: the
// cursor's row becomes the join of the text before it and the text after it,
// the rows below move by the number of lines that went or arrived, and that
// many rows at the far end are the only ones that have to be drawn. So one row
// is repainted, the rest are moved by the VDP's own region scroll rather than
// resent, and only the newly exposed rows are sent.
//
// `delta` is the change: positive when lines went, negative when they arrived.
// The two directions used to be separate functions and were the same function
// -- the whole difference between them is which way the region scrolls and
// which end of it is left needing paint.
//
// Returns false if it cannot, having drawn nothing, so the caller can fall back.
static bool reshow_delta(editor* ed, int delta) {
    SCR(ed);
    TB(ed);

    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    if (scr_place_cursor(scr, prefix, psz) != 0) {
        // The horizontal origin moved. It is screen-wide, so every other row is
        // now drawn against the old one and none of them can be kept.
        return false;
    }

    cmd_repaint_rows(ed, scr->currY_, scr->currY_);

    const char first = (char) (scr->currY_ + 1);
    const char last = (char) (scr->bottomY_ - 1);
    const int moved = delta < 0 ? -delta : delta;
    if (moved > 0 && first <= last) {
        const int height = last - first + 1;
        if (moved >= height) {
            // More lines moved than there are rows below: nothing down there
            // survives, so scrolling it would be wasted bytes.
            cmd_repaint_rows(ed, first, last);
        } else if (delta > 0) {
            scr_scroll_rows_up(scr, first, last, moved);
            cmd_repaint_rows(ed, (char) (last - moved + 1), last);
        } else {
            scr_scroll_rows_down(scr, first, last, moved);
            cmd_repaint_rows(ed, first, (char) (first + moved - 1));
        }
    }

    scr_sync_cursor(scr);
    scr_show_cursor_ch(scr, tb_peek(tb));

    return true;
}

// Puts the cursor on the row that leaves the view where it was, when the line is
// still on screen. The line shown on the top row is derived from the cursor's
// row, so setting that row is how the view is aimed -- see keep_cursor_row.
static void show_line_at(editor* ed, int line, int top_before) {
    SCR(ed);

    int y = scr->topY_ + (line - top_before);
    if (y < scr->topY_ || y >= scr->bottomY_) {
        // The edit was off screen. Put it half way down rather than at an edge,
        // so what surrounds it is visible.
        y = scr->topY_ + (scr->bottomY_ - scr->topY_) / 2;
    }
    if (y - scr->topY_ > line - 1) {
        // Never leave more rows above the cursor than the document has lines.
        y = scr->topY_ + line - 1;
    }
    scr->currY_ = (char) y;
}

// Repaints after an undo or a redo.
//
// A record either holds line breaks or it does not, and that decides everything:
// undoing an insert of k breaks takes k lines out, undoing a delete of k puts k
// back, and a record with none changes one line and no others. So the line count
// before and after says which of the three shapes this was, without the view
// having to know anything about what the record contained.
static void reshow_edit(editor* ed, int lines_before, int top_before) {
    TB(ed);

    ed->selecting_ = false;
    show_line_at(ed, tb_ypos(tb), top_before);

    const int delta = tb_ymax(tb) - lines_before;
    // `delta` counts lines gained, and reshow_delta counts lines lost.
    const bool done = reshow_delta(ed, -delta);
    if (!done) {
        // The horizontal origin moved, so every row is drawn against the old
        // one and none can be kept.
        reshow(ed);
    }
}

void cmd_undo(editor* ed) {
    TB(ed);
    SCR(ed);

    const int top_before = top_line(scr, tb);
    const int lines_before = tb_ymax(tb);
    if (!undo_apply(&ed->undo_, tb)) {
        return;
    }
    reshow_edit(ed, lines_before, top_before);
}

// Puts `line` halfway down the screen, or as close as the document allows.
//
// A match always lands in the same place, so the eye knows where to look
// instead of hunting the screen for a cursor that could be anywhere. Near the
// top of a document there is not enough above it to centre against, and then it
// sits as low as the lines available allow.
static void centre_line(editor* ed, int line) {
    SCR(ed);

    int y = scr->topY_ + (scr->bottomY_ - scr->topY_) / 2;
    if (y - scr->topY_ > line - 1) {
        y = scr->topY_ + line - 1;
    }
    scr->currY_ = (char) y;
}

// Jumps to a match, centres it, and leaves it selected.
//
// Selected because a bare cursor is hard to pick out: the whole of what was
// found is shown in reversed colours, the way a selection made by hand is. The
// cursor goes to the end of the match, which is where a selection made by
// moving right would have left it -- so typing replaces what was found, and the
// next search starts past it rather than finding it again.
static void jump_to_match(editor* ed, tb_pos at, int len) {
    SCR(ed);
    TB(ed);

    scr_hide_cursor_ch(scr, tb_peek(tb));

    ed->anchor_ = at;
    ed->selecting_ = true;
    tb_pos end = at;
    end.x += len;
    tb_seek(tb, end);

    centre_line(ed, at.line);

    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
    // Through cmd_repaint_rows rather than refresh_screen: only that one asks
    // row_selection which columns are covered, and without it the match would
    // be selected without looking selected.
    cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
    scr_show_cursor_ch(scr, tb_peek(tb));
}

// Searches from just past the cursor, so repeating moves on rather than finding
// the same match again.
static void find_from_cursor(editor* ed, bool forward) {
    TB(ed);
    UI(ed);
    SCR(ed);

    if (ed->findsz_ <= 0) {
        return;
    }

    tb_pos from = tb_tell(tb);
    if (forward) {
        from.x += 1;
    } else {
        // A match leaves the cursor at its end, so stepping one back from the
        // cursor is still inside it and the same match is found again -- which
        // looked exactly like CTRL+P doing nothing at all. Going backwards
        // starts from where the match began.
        //
        // Only when the selection is behind the cursor. A selection made by
        // hand can run the other way, and its anchor is then ahead of the
        // cursor -- searching back from there would skip over everything
        // between the two.
        if (ed->selecting_
            && (ed->anchor_.line < from.line
                || (ed->anchor_.line == from.line && ed->anchor_.x < from.x))) {
            from = ed->anchor_;
        }
        from.x -= 1;
    }

    tb_pos at;
    if (!tb_find(tb, ed->find_, ed->findsz_, from, forward, &at)) {
        // Whatever was selected described the last match, not this attempt.
        ed->selecting_ = false;
        // Left exactly where it was. Someone who cannot find what they wanted
        // has no use for a view that has moved somewhere else in the trying.
        ui_message(ui, scr, "Not found");
        cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
        scr_show_cursor_ch(scr, tb_peek(tb));

        return;
    }
    jump_to_match(ed, at, ed->findsz_);
}

void cmd_find(editor* ed) {
    UI(ed);
    SCR(ed);

    char* text = NULL;
    int sz = 0;
    const RESPONSE res = ui_text(ui, scr, "Find: ", ed->find_, &text, &sz);
    if (res != YES_OPT || text == NULL || sz <= 0) {
        // Cancelled, or nothing typed. The document has not moved -- a modal
        // search only jumps once there is something to jump to.
        cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
        scr_show_cursor_ch(scr, tb_peek(&ed->buf_));

        return;
    }
    if (sz > (int) sizeof(ed->find_) - 1) {
        sz = (int) sizeof(ed->find_) - 1;
    }
    memcpy(ed->find_, text, (size_t) sz);
    ed->find_[sz] = 0;
    ed->findsz_ = sz;

    cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
    find_from_cursor(ed, true);
}

void cmd_find_next(editor* ed) {
    find_from_cursor(ed, true);
}

void cmd_find_prev(editor* ed) {
    find_from_cursor(ed, false);
}

void cmd_redo(editor* ed) {
    TB(ed);
    SCR(ed);

    const int top_before = top_line(scr, tb);
    const int lines_before = tb_ymax(tb);
    if (!redo_apply(&ed->undo_, tb)) {
        return;
    }
    reshow_edit(ed, lines_before, top_before);
}

bool cmd_delete_selection(editor* ed) {
    if (!ed->selecting_) {
        return false;
    }
    TB(ed);

    tb_pos a;
    tb_pos b;
    cmd_selection_range(ed, &a, &b);
    ed->selecting_ = false;

    // Read before the delete: afterwards the cursor is at `a` and this is gone.
    const int cursor_line = tb_ypos(tb);

    // One command, one thing to take back. Without this the range's characters
    // are grouped as keystrokes would be -- split at word boundaries, and worse,
    // joined to whatever was deleted just before, so deleting two selections
    // over the same span took a single undo to reverse both.
    undo_group_begin(&ed->undo_);
    const bool did = tb_range_del(tb, a, b);
    undo_group_end(&ed->undo_);
    if (!did) {
        return false;
    }

    // The cursor is at the start of what was deleted and the lines below have
    // moved up, but the text above it has not moved at all -- so neither should
    // the view.
    if (!keep_cursor_row(ed, a.line, cursor_line)
            || !reshow_delta(ed, b.line - a.line)) {
        reshow(ed);
    }

    return true;
}

// A spill writes over whatever is at <document>.scratch. That name can belong
// to a file the user has, or to a remnant of a session that crashed, so it is
// worth one question before it goes. Only ever asked once a session: a file
// this session wrote is its own to write over again.
static bool may_spill(editor* ed, tb_pos a, tb_pos b) {
    TB(ed);
    UI(ed);
    SCR(ed);

    if (!clip_spill_would_overwrite(&ed->clip_, tb, tb_range_size(tb, a, b))) {
        return true;
    }
    const RESPONSE res = ui_dialog(ui, scr, "Scratch file exists. Overwrite?");
    cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
    scr_show_cursor_ch(scr, tb_peek(tb));

    return res == YES_OPT;
}

void cmd_copy(editor* ed) {
    if (!ed->selecting_) {
        return;
    }
    TB(ed);
    UI(ed);
    SCR(ed);

    tb_pos a;
    tb_pos b;
    cmd_selection_range(ed, &a, &b);
    if (!may_spill(ed, a, b)) {
        return;
    }
    if (!clip_copy(&ed->clip_, tb, a, b)) {
        // Only worth saying anything when there was something to copy: an empty
        // selection failing is not news.
        if (tb_range_size(tb, a, b) > 0) {
            ui_message(ui, scr, "Could not write the scratch file");
            cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
            scr_show_cursor_ch(scr, tb_peek(tb));
        }
    }
    // The selection stays. Copying does not consume it, and keeping it lets a
    // second thought about where it ends cost one keystroke instead of all of
    // them again.
}

void cmd_cut(editor* ed) {
    if (!ed->selecting_) {
        return;
    }
    TB(ed);
    UI(ed);
    SCR(ed);

    tb_pos a;
    tb_pos b;
    cmd_selection_range(ed, &a, &b);
    if (!may_spill(ed, a, b)) {
        return;
    }

    // Copied first, and only deleted if that worked. A cut that removed text
    // the copy did not keep -- because the scratch file could not be written --
    // would be a delete wearing the wrong name.
    if (!clip_copy(&ed->clip_, tb, a, b)) {
        if (tb_range_size(tb, a, b) > 0) {
            ui_message(ui, scr, "Could not write the scratch file");
            cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
            scr_show_cursor_ch(scr, tb_peek(tb));
        }

        return;
    }
    cmd_delete_selection(ed);
}

void cmd_paste(editor* ed) {
    TB(ed);
    UI(ed);
    SCR(ed);

    if (!clip_has(&ed->clip_)) {
        return;     // nothing copied yet; not worth a complaint
    }

    // A selection is what the paste replaces, the same way typing does -- but
    // whether the paste fits is asked before deleting it, not after. Deleting
    // first and then finding there is no room would leave the selection gone
    // and nothing put in its place, with no undo to get it back.
    int free_bytes = 0;
    int free_lines = 0;
    if (ed->selecting_) {
        tb_pos a;
        tb_pos b;
        cmd_selection_range(ed, &a, &b);
        free_bytes = tb_range_size(tb, a, b);
        free_lines = b.line - a.line;
    }
    if (!tb_can_insert(tb, clip_size(&ed->clip_), clip_lines(&ed->clip_),
                       free_bytes, free_lines)) {
        ui_message(ui, scr, "Not enough room to paste");
        cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
        scr_show_cursor_ch(scr, tb_peek(tb));

        return;
    }

    // A spilled copy is read a chunk at a time, so a file that has gone missing
    // or been truncated is only discovered part way through -- by which point
    // the selection it was replacing is already deleted. Asked first, while
    // there is still something to keep.
    if (!clip_verify(&ed->clip_)) {
        ui_message(ui, scr, "The scratch file cannot be read");
        cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
        scr_show_cursor_ch(scr, tb_peek(tb));

        return;
    }

    if (ed->selecting_) {
        cmd_delete_selection(ed);
    }

    // The pasted text is one thing the user did, however many lines it is.
    // Note this is a second group: replacing a selection deletes it in its own
    // group first, so an undo takes the paste back and another restores what it
    // replaced -- two steps for two things, which is what happened.
    undo_group_begin(&ed->undo_);
    const bool pasted = clip_paste(&ed->clip_, tb);
    undo_group_end(&ed->undo_);
    if (!pasted) {
        ui_message(ui, scr, "Not enough room to paste");
        cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
        scr_show_cursor_ch(scr, tb_peek(tb));

        return;
    }

    // The cursor ends after the pasted text, which can be several lines further
    // down, so the view is rebuilt rather than patched.
    place_cursor_row(ed);
    reshow(ed);
}

void cmd_select_all(editor* ed) {
    TB(ed);
    SCR(ed);

    ed->anchor_.line = 1;
    ed->anchor_.x = 0;
    ed->selecting_ = true;

    // To the very end, which is where the cursor belongs after selecting
    // everything and where a paste over the selection would leave it anyway.
    tb_pos end;
    end.line = tb_ymax(tb);
    end.x = 0;
    tb_seek(tb, end);
    tb_end(tb);

    place_cursor_row(ed);
    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
    cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
    scr_show_cursor_ch(scr, tb_peek(tb));
}

void cmd_open(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    // Same bargain as quitting: the document on screen is about to go, so the
    // user gets the same chance to keep it, and answering neither way calls the
    // whole thing off.
    if (tb_changed(tb)) {
        RESPONSE res = ui_dialog(ui, scr, "Save before opening?");
        if (res == CANCEL_OPT) {
            return;
        }
        if (res == YES_OPT && !cmd_save(ed)) {
            return;   // the save was cancelled or failed; keep the document
        }
    }

    char* fname;
    int sz;
    // Prefilled with the current name so the prompt shows the shape of what it
    // wants, and so opening a mistyped name again is a small edit.
    if (ui_text(ui, scr, "Open file: ", tb_fname(tb), &fname, &sz) != YES_OPT) {
        return;
    }

    switch (tb_open(tb, fname, sz)) {
        case TB_TOO_LARGE:
            ui_message(ui, scr, "File too large");
            return;
        case TB_NO_FILE:
            ui_message(ui, scr, "Cannot open file");
            return;
        case TB_OK:
            break;
    }

    // The document has a new name, so it may be a new language: a .c opened
    // over a .bas takes C's grammar, and a .txt over either takes none and
    // gives the user their own colours back.
    ed_pick_syntax(ed);

    // A selection points into the document that just went away, so it goes with
    // it -- its line numbers mean something else now.
    ed->selecting_ = false;

    // scr_clear winds the cursor and the horizontal origin back to the start
    // as well as repainting the banner, which is exactly the reset a whole new
    // document needs.
    scr_clear(scr);
    cmd_show(ed);
}

// Puts the document back after a modal has drawn over it.
//
// scr_clear resets the cursor's row to the top of the text area, and
// refresh_screen reads that row as "how many lines above the cursor are on
// screen" -- so clearing and refreshing without putting it back first paints
// the cursor's line at the top, and the view has apparently scrolled. With the
// cursor on the last line of a file that is exactly what it looked like:
// everything above it gone, and one press of UP bringing it all back.
//
// `moved` says the geometry changed under it, which a font does. The old row is
// meaningless then -- there may not be that many rows any more -- so the
// cursor's line is centred instead, or put as far down as the document allows
// when there is not enough above it to centre against.
static void restore_after_modal(editor* ed, bool moved) {
    SCR(ed);
    TB(ed);

    const char currX = scr->currX_;
    const char currY = scr->currY_;
    const char ch = tb_peek(tb);

    scr_clear(scr);
    scr->currX_ = currX;
    if (moved) {
        centre_line(ed, tb_ypos(tb));
    } else {
        scr->currY_ = currY;
    }
    refresh_screen(ed, tb);
    scr_show_cursor_ch(scr, ch);
}

void cmd_help(editor* ed) {
    SCR(ed);
    UI(ed);
    TB(ed);

    ui_help(ui, scr);

    // The help wrote over the document, and the view cannot put it back on its
    // own -- it has no access to the buffer.
    restore_after_modal(ed, false);
}

void cmd_settings(editor* ed) {
    SCR(ed);
    UI(ed);
    TB(ed);

    // Comes in holding what the settings file says, so the font row can show
    // the one in use, and goes out holding only what was changed.
    config cfg;
    cfg_defaults(&cfg);
    cfg_load(&cfg, CFG_PATH);

    const RESPONSE ret = ui_settings(ui, scr, &cfg);

    // A font changes the cell size, and with it the number of rows and where
    // the footer sits. Everything below is laid out from those, so the font
    // goes in first and the screen is rebuilt from what it leaves behind.
    bool moved = false;
    if (ret == YES_OPT && (cfg.font[0] != 0 || cfg.font_none)) {
        if (cfg.font[0] != 0) {
            scr_load_font(scr, cfg.font);
        } else {
            scr_system_font(scr);
        }

        // The prompt row moved with the geometry; ui_ places everything it
        // draws from it, so a prompt left on the old bottom row would land in
        // the middle of the document.
        ui_resize(ui, scr->bottomY_, scr->cols_);

        // Fewer rows than before can leave the cursor past the bottom. Pulling
        // it back to the last text row keeps it somewhere the screen has, and
        // refresh_screen re-anchors the view from wherever it ends up.
        moved = true;
    }

    restore_after_modal(ed, moved);

    if (ret == YES_OPT) {
        // Only the changed settings are set, and cfg_update copies every other
        // line through as it found it -- comments, spacing, and anything a
        // later version understands and this one does not.
        cfg_update(&cfg, CFG_PATH);
    }
}

void cmd_putc(editor* ed, key k) {
    TB(ed);
    SCR(ed);

    if (!tb_put(tb, k.key)) {
        return;
    }
    split_line ln = tb_curr_line(tb);
    (void) state_at_row(ed, tb, scr->currY_);   // the model, before painting
    const char was = row_below_state(ed, scr->currY_);
    const int moved = scr_putc(scr, k.key, ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_);
    if (moved != 0) {
        resync_after_scroll(ed, tb, tb_peek(tb), moved, true);
    } else {
        resync_rows_below(ed, scr->currY_, was);
    }
}

/*
 * `merged` says what happened above: two lines became one, so the row at the
 * cursor still starts where it did and the rows under it moved up -- against a
 * line being removed, where the row at the cursor shows a different line and
 * moved up with the rest.
 *
 * `in` is what the cursor's row begins inside, and the caller reads it before
 * the edit. Asking here would ask about a document the view does not describe
 * yet: the line count has already changed, so the answers are thrown away and
 * a screenful of lexing buys back what the caller already had.
 */
static void region_up(editor* ed, text_buffer* tb, char ch, bool merged,
                      int in) {
    SCR(ed);
    split_line ln = tb_curr_line(tb);
    // The caller read this before the edit, when the view still described the
    // document. Putting it back into the model is what lets the paints below
    // simply ask.
    if (ed->synTopLine_ != 0 && scr->currY_ < SCR_MAX_ROWS) {
        ed->rowSyn_[scr->currY_] = (char) in;
    }
    const int out = row_leaves(ed, ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_,
                               in);
    scr_scroll_up_split(scr, scr->currY_, scr->bottomY_-1,
                        ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_, ch);

    int diff = scr->bottomY_ - scr->currY_ - 1;
    int last = 0;
    int curr = 0;
    while (diff-- > 0) {
        curr = tb_down(tb);
        if (curr == last) {
            // The document ran out, so the rows below are blank and nothing
            // known about them holds.
            ed->synTopLine_ = 0;
            scr_write_line(scr, scr->bottomY_-1, NULL, 0);
            return;
        }
    }
    ln = tb_curr_line(tb);
    /*
     * The row that the scroll exposed: the rows above it moved as pixels and
     * kept their colours, so only this one has to be worked out -- and what is
     * known about the rows moves with them rather than being worked out again.
     */
    const char first = merged ? (char)(scr->currY_ + 1) : scr->currY_;
    (void) rows_shifted_up(ed, tb, first, merged ? out : -1);
    if (ed->synTopLine_ != 0) {
        ed->synLines_ = tb_ymax(&ed->buf_);
    }
    scr_paint_row(scr, scr->bottomY_-1,
                  ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_);
}

void cmd_show(editor* ed) {
    TB(ed);
    SCR(ed);

    text_buffer cb;
    tb_copy(&cb, tb);
    fill_screen(ed, &cb);
    scr_sync_cursor(scr);

    const char to_ch = tb_peek(tb);
    scr_show_cursor_ch(scr, to_ch);
}

static void cmd_del_merge(editor* ed) {
    TB(ed);
    SCR(ed);
    // Before the merge, while the view still describes the document.
    const int in = state_at_row(ed, tb, scr->currY_);
    if (!tb_del_merge(tb)) {
        return;
    }

    const char ch = tb_peek(tb);
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    region_up(ed, &cp, ch, true, in);
}

void cmd_del(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_eol(tb)) {
        if (tb_bol(tb)) {
            cmd_del_line(ed);
        } else {
            cmd_del_merge(ed);
        }
        return;
    }
    if (!tb_del(tb)) {
        return;
    }
    if (ed->syn_.loaded) {
        /*
         * scr_del paints from the cursor, and a deletion can change the colour
         * of what is left of it -- taking the second character out of a `/*`
         * ends a comment that was covering the rest of the line. The whole row
         * goes instead, which is what the other edits do.
         */
        cmd_repaint_rows(ed, scr->currY_, scr->currY_);
        scr_show_cursor_ch(scr, tb_peek(tb));

        return;
    }
    int sz = 0;
    char* suffix = tb_suffix(tb, &sz);
    scr_del(scr, suffix, sz);
}

static void cmd_bksp_merge(editor* ed) {
    TB(ed);
    SCR(ed);

    /*
     * The merged line ends up on the row above this one, so that is the row
     * whose beginning is wanted -- read now, before the merge, while the view
     * still describes the document.
     */
    const int in = state_at_row(ed, tb,
                                scr->currY_ > scr->topY_
                                    ? (char)(scr->currY_ - 1) : scr->currY_);
    if (!tb_bksp_merge(tb)) {
        return;
    }
    if (scr->currY_ > scr->topY_) {
        scr->currY_--;
    }
    int bsz = 0;
    char* bprefix = tb_prefix(tb, &bsz);
    scr_place_cursor(scr, bprefix, bsz);

    char ch = tb_peek(tb);
    text_buffer cp;

    tb_copy(&cp, tb);
    tb_home(&cp);
    region_up(ed, &cp, ch, true, in);
}

void cmd_bksp(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_bol(tb)) {
        if (tb_ypos(tb) > 1) {
            cmd_bksp_merge(ed);
        }
        return;
    }

    if (!tb_bksp(tb)) {
        return;
    }
    split_line ln = tb_curr_line(tb);
    (void) state_at_row(ed, tb, scr->currY_);   // the model, before painting
    const char was = row_below_state(ed, scr->currY_);
    const int moved = scr_bksp(scr, ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_);
    if (moved != 0) {
        resync_after_scroll(ed, tb, tb_peek(tb), moved, true);
    } else {
        resync_rows_below(ed, scr->currY_, was);
    }
}

void cmd_newl(editor* ed) {
    TB(ed);
    SCR(ed);

    char ch = tb_peek(tb);
    split_line ln = tb_curr_line(tb);
    const int in = state_at_row(ed, tb, scr->currY_);

    if (!tb_newline(tb)) {
        return;
    }
    // What is left on this row is the text before the break, so it is lexed as
    // its own line -- which it now is. It begins where the whole line began,
    // which was read before the split: asking afterwards asks about a document
    // the view does not describe yet, and the answer is worked out again for
    // nothing.
    const int out = row_leaves(ed, ln.prefix_, ln.psz_, NULL, 0, in);
    if (ed->synTopLine_ != 0 && scr->currY_ < SCR_MAX_ROWS) {
        ed->rowSyn_[scr->currY_] = (char) in;
    }
    if (scr->currY_ < scr->bottomY_-1) {
        // Said before either half is painted, so both can simply ask.
        rows_inserted(ed, scr->currY_, out);
    } else {
        ed->synTopLine_ = 0;    // the view scrolls instead; every row moves
    }
    scr_write_line(scr, scr->currY_, ln.prefix_, ln.psz_);

    scr_place_cursor(scr, NULL, 0);
    /*
     * And the text that moved down is a line of its own now too, beginning in
     * whatever the half above it left open. Without this the row the text
     * landed on was painted plain -- which is what pressing return at the
     * start of a line looked like: the line appeared to lose its colouring and
     * got it back the next time anything repainted it.
     */
    if  (scr->currY_ < scr->bottomY_-1) {
        scr->currY_++;
        scr_scroll_down(scr, scr->currY_, scr->bottomY_-1, ln.suffix_, ln.ssz_, ch);
    } else {
        scr_scroll_up(scr, scr->topY_, scr->bottomY_-1, ln.suffix_, ln.ssz_, ch);
    }
}

void cmd_del_line(editor* ed) {
    TB(ed);
    SCR(ed);

    undo_group_begin(&ed->undo_);
    const int in = state_at_row(ed, tb, scr->currY_);
    const bool did = tb_del_line(tb);
    undo_group_end(&ed->undo_);
    if (!did) {
        return;
    }
    scr_place_cursor(scr, NULL, 0);

    const char ch = tb_peek(tb);
    text_buffer cp;

    tb_copy(&cp, tb);
    tb_home(&cp);
    // A line went rather than two becoming one, so the cursor's row shows a
    // different line and moved up with the rest of them.
    region_up(ed, &cp, ch, false, in);
}

/*
 * The four sideways movements are one command with two holes in it: which end
 * of the line has nowhere further to go, and how far a single step reaches.
 * What follows a step is the same in all four -- the screen is told where the
 * cursor went, and scrolled sideways when it went past the edge.
 *
 * They were four copies of that, and the copies had begun to drift: three of
 * the four indented the resync twelve spaces where the first used eight, which
 * is the fingerprint of the paste that made them.
 *
 * One difference between the copies turned out to be nothing. cmd_right alone
 * returned early when the character under the cursor was a zero byte, which
 * cannot happen here: IS_EOL counts zero as the end of a line, so tb_eol has
 * already sent that case up the other branch. The guard went with the copies.
 */
static void stepped(editor* ed, char from_ch, char to_ch) {
    TB(ed);
    SCR(ed);

    split_line ln = tb_curr_line(tb);
    const int moved = scr_move_cursor(scr, from_ch, to_ch, ln.prefix_, ln.psz_);
    if (moved != 0) {
        resync_after_scroll(ed, tb, to_ch, moved, false);
    }
}

// Back one character, or one word when by_word. Off the front of a line is the
// end of the line above, when there is one.
static void step_back(editor* ed, bool by_word) {
    TB(ed);

    if (tb_bol(tb)) {
        if (tb_ypos(tb) > 1) {
            cmd_up(ed);
            cmd_end(ed);
        }

        return;
    }

    const char from_ch = tb_peek(tb);
    stepped(ed, from_ch, by_word ? tb_w_prev(tb, from_ch) : tb_prev(tb));
}

// On one character, or one word. Off the end of a line is the start of the one
// below -- but only if there was a line below to go to.
static void step_on(editor* ed, bool by_word) {
    TB(ed);

    if (tb_eol(tb)) {
        const int ypos = tb_ypos(tb);
        cmd_down(ed);
        if (ypos != tb_ypos(tb)) {
            cmd_home(ed);
        }

        return;
    }

    const char from_ch = tb_peek(tb);
    stepped(ed, from_ch, by_word ? tb_w_next(tb, from_ch) : tb_next(tb));
}

void cmd_left(editor* ed) {
    step_back(ed, false);
}

void cmd_w_left(editor* ed) {
    step_back(ed, true);
}

void cmd_right(editor* ed) {
    step_on(ed, false);
}

void cmd_w_right(editor* ed) {
    step_on(ed, true);
}

void cmd_up(editor* ed) {
    TB(ed);
    SCR(ed);

    // Remember the column, not the byte offset: a tab is one byte but several
    // columns, so carrying x_ across would slide the cursor sideways.
    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    const int want_col = scr_column_of(scr, prefix, psz);

    const int ypos = tb_ypos(tb);
    const char from_ch = tb_peek(tb);
    tb_up(tb);
    if (ypos == tb_ypos(tb)) {
        return;
    }

    // Land on the byte of the new line that renders at that column.
    tb_home(tb);
    int lsz = 0;
    char* row = tb_suffix(tb, &lsz);
    const char to_ch = tb_goto_offset(tb, scr_byte_at(scr, row, lsz, want_col));

    if (scr->currY_ == scr->topY_) {
        scr_hide_cursor_ch(scr, from_ch);
        split_line top = tb_curr_line(tb);
        (void) scr_place_cursor(scr, top.prefix_, top.psz_);

        text_buffer cp;
        tb_copy(&cp, tb);
        tb_home(&cp);
        const split_line cl = tb_curr_line(&cp);
        ed->synTopLine_ = 0;    // the view moved up; every row moved with it
        scr_scroll_down_split(scr, scr->topY_, scr->bottomY_-1,
                              cl.prefix_, cl.psz_, cl.suffix_, cl.ssz_, to_ch);
        return;
    }

    split_line ln = tb_curr_line(tb);
    const int moved = scr_up(scr, from_ch, to_ch, ln.prefix_, ln.psz_);
    if (moved != 0) {
            resync_after_scroll(ed, tb, to_ch, moved, false);
    }
}

void cmd_down(editor* ed) {
    TB(ed);
    SCR(ed);

    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    const int want_col = scr_column_of(scr, prefix, psz);

    const int ypos = tb_ypos(tb);
    const char from_ch = tb_peek(tb);
    tb_down(tb);
    if (ypos == tb_ypos(tb)) {
        return;
    }

    tb_home(tb);
    int lsz = 0;
    char* row = tb_suffix(tb, &lsz);
    const char to_ch = tb_goto_offset(tb, scr_byte_at(scr, row, lsz, want_col));

    if (scr->currY_ >= scr->bottomY_-1) {
        scr_hide_cursor_ch(scr, from_ch);
        split_line bot = tb_curr_line(tb);
        (void) scr_place_cursor(scr, bot.prefix_, bot.psz_);

        text_buffer cp;
        tb_copy(&cp, tb);
        tb_home(&cp);
        const split_line cl = tb_curr_line(&cp);
        /*
         * Every row shows the line that was under it, and one arrives at the
         * bottom. Shifting the answers along costs one lex; working the screen
         * out again costs one per row, which is what holding the arrow key
         * down used to pay for every line.
         */
        (void) rows_shifted_up(ed, &cp, scr->topY_, -1);
        if (ed->synTopLine_ != 0) {
            ed->synTopLine_++;      // the top row shows the line below it now
        }
        scr_scroll_up_split(scr, scr->topY_, scr->bottomY_-1,
                            cl.prefix_, cl.psz_, cl.suffix_, cl.ssz_, to_ch);
        return;
    }

    split_line ln = tb_curr_line(tb);
    const int moved = scr_down(scr, from_ch, to_ch, ln.prefix_, ln.psz_);
    if (moved != 0) {
            resync_after_scroll(ed, tb, to_ch, moved, false);
    }
}

void cmd_home(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_bol(tb)) {
        return;
    }

    char from_ch = tb_peek(tb);
    tb_home(tb);

    split_line ln = tb_curr_line(tb);
    const int moved = scr_move_cursor(scr, from_ch, tb_peek(tb),
                                      ln.prefix_, ln.psz_);
    if (moved != 0) {
        resync_after_scroll(ed, tb, tb_peek(tb), moved, false);
    }
}

void cmd_end(editor* ed) {
    TB(ed);
    SCR(ed);

    const int from_x = tb_xpos(tb);
    char from_ch = tb_peek(tb);
    char to_ch = tb_end(tb);
    if (tb_xpos(tb) != from_x) {
        split_line ln = tb_curr_line(tb);
        const int moved = scr_move_cursor(scr, from_ch, to_ch,
                                          ln.prefix_, ln.psz_);
        if (moved != 0) {
            resync_after_scroll(ed, tb, to_ch, moved, false);
        }
    }
}

void cmd_page_up(editor* ed) {
    TB(ed);
    SCR(ed);

    const int curr = scr->currY_ - scr->topY_;
    const int page = scr->bottomY_ - scr->topY_+1;
    int remaining = tb_ypos(tb)-1 - curr;

    if (remaining <= 0) {
        remaining = tb_ypos(tb)-1;
        scr->currY_ = scr->topY_;
    }
    for (int i = 0; i < page && remaining > 0; i++, remaining--) {
        tb_up(tb);
    }

    char ch = tb_peek(tb);
    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
    refresh_screen(ed, tb);
    scr_show_cursor_ch(scr, ch);
}

void cmd_page_down(editor* ed) {
    TB(ed);
    SCR(ed);

    const int curr = scr->bottomY_ - scr->currY_;
    const int page = scr->bottomY_ - scr->topY_;
    int remaining = tb_ymax(tb) - tb_ypos(tb) - curr + 1;

    if (remaining <= 0) {
        remaining = tb_ymax(tb) - tb_ypos(tb);
        scr->currY_ = scr->bottomY_-1;
    }
    for (int i = 0; i < page && remaining > 0; i++, remaining--) {
        tb_down(tb);
    }
    char ch = tb_peek(tb);
    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
    refresh_screen(ed, tb);
    scr_show_cursor_ch(scr, ch);
}

// Puts the cursor on `line` and the view around it. Shared by everything that
// jumps somewhere far off rather than stepping there: CTRL+G, and CTRL+HOME and
// CTRL+END.
//
// tb_seek, not a walk of tb_up and tb_down. Those move inside what memory is
// holding, and although they settle at its edge now, walking a 7,509 line file
// a line at a time to reach the end of it would slide the window the whole way.
// Seeking goes straight there.
//
// The column is carried over rather than reset, which is what stepping a line
// at a time did -- tb_seek clamps it to the line it lands on.
static void jump_to_line(editor* ed, int line) {
    SCR(ed);
    TB(ed);

    const int ypos = tb_ypos(tb);
    if (ypos == line) {
        return;
    }

    scr_hide_cursor_ch(scr, tb_peek(tb));

    const tb_pos to = { line, tb_xpos(tb) - 1 };
    tb_seek(tb, to);

    // The view follows by however far the cursor actually moved, which is not
    // always how far it was asked to: a line number past the end stops at the
    // end. Further than a screenful puts it against the edge it travelled
    // towards.
    const int diff = ((int) scr->currY_) + (tb_ypos(tb) - ypos);
    if (diff < (int) scr->topY_) {
        scr->currY_ = scr->topY_;
    } else if (diff >= (int) scr->bottomY_) {
        scr->currY_ = scr->bottomY_ - 1;
    } else {
        scr->currY_ = (char) diff;
    }
    scr_sync_cursor(scr);

    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
    refresh_screen(ed, tb);
    scr_show_cursor_ch(scr, tb_peek(tb));
}

void cmd_goto(editor* ed) {
    SCR(ed);
    UI(ed);

    int line = 0;
    if (ui_goto(ui, scr, &line) != YES_OPT) {
        return;
    }
    jump_to_line(ed, line);
}

// CTRL+HOME and CTRL+END: the top and the bottom of the document, as against
// HOME and END, which are the ends of the line.
void cmd_doc_top(editor* ed) {
    jump_to_line(ed, 1);
}

void cmd_doc_end(editor* ed) {
    TB(ed);

    jump_to_line(ed, tb_ymax(tb));
}

