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
 * Host tests for the undo log's capture stage.
 *
 * Nothing replays yet. What is pinned here is that every mutation reaches the
 * log, that the bulk operations reach it through the primitives they are built
 * from rather than needing their own hooks, and that the two things which must
 * NOT be recorded -- loading a file, and painting the screen -- are not.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "cmd_ops.h"
#include "editor.h"
#include "text_buffer.h"
#include "undo.h"

static int failures = 0;

static long mark;
static char raw[8192];

static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

static int cap_read(char* out, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    long n = end - mark;
    if (n < 0) {
        n = 0;
    }
    if (n > max) {
        n = max;
    }
    fseek(stdout, mark, SEEK_SET);
    const size_t got = fread(out, 1, (size_t) n, stdout);
    fseek(stdout, end, SEEK_SET);

    return (int) got;
}

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void check_txt(const char* name, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-52s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got '%s', want '%s'\n", name, got, want);
        failures++;
    }
}

/* Whether the captured VDU stream contains this run of bytes. Painted text
 * reaches the VDP as ordinary characters, so a line that was drawn is literally
 * in there and one the VDP scrolled into place is not. */
static int stream_has(const char* hay, int n, const char* needle) {
    const int m = (int) strlen(needle);
    for (int i = 0; i + m <= n; i++) {
        if (memcmp(hay + i, needle, (size_t) m) == 0) {
            return 1;
        }
    }

    return 0;
}

/* Field accessors that survive a missing record. Without these, a hook that
 * stopped recording would crash the test on a null dereference instead of
 * reporting which assertion failed -- a worse way to learn it, and how the
 * missing tb_bksp_merge hook first showed up. */
static int rec_op(undo* u, int i) {
    const undo_rec* r = undo_at(u, i);

    return r == NULL ? -1 : (int) r->op;
}

static int rec_x(undo* u, int i) {
    const undo_rec* r = undo_at(u, i);

    return r == NULL ? -1 : r->x;
}

static int rec_line(undo* u, int i) {
    const undo_rec* r = undo_at(u, i);

    return r == NULL ? -1 : r->line;
}

static int rec_len(undo* u, int i) {
    const undo_rec* r = undo_at(u, i);

    return r == NULL ? -1 : r->len;
}

/* The saved bytes of every DELETE record, oldest first, run together. */
static const char* deleted(undo* u) {
    static char out[4096];
    int at = 0;
    for (int i = 0; i < undo_count(u); i++) {
        const undo_rec* r = undo_at(u, i);
        if (r->op != UNDO_DELETE) {
            continue;
        }
        if (at + r->len >= (int) sizeof(out)) {
            break;
        }
        undo_text_of(u, i, out + at, (int) sizeof(out) - at);
        at += r->len;
    }
    out[at] = 0;

    return out;
}

/* A line's text, for comparing the document rather than the log. */
static const char* line_text(text_buffer* tb, int line) {
    static char out[256];
    const tb_pos here = tb_tell(tb);
    tb_seek(tb, (tb_pos){line, 0});
    int sz = 0;
    const char* p = tb_suffix(tb, &sz);
    if (sz > (int) sizeof(out) - 1) {
        sz = (int) sizeof(out) - 1;
    }
    memcpy(out, p, (size_t) sz);
    out[sz] = 0;
    tb_seek(tb, here);

    return out;
}

static void put_str(text_buffer* tb, const char* s) {
    for (; *s; s++) {
        tb_put(tb, *s);
    }
}

int main(void) {
    stub_discard_output();
    if (freopen("/tmp/aed_undo_capture", "w+", stdout) == NULL) {
        fprintf(stderr, "cannot capture stdout\n");

        return 2;
    }

    /* --- the log on its own --- */
    {
        undo u;
        check("a log starts", undo_init(&u, 64, 4) != NULL, 1);
        check("  empty", undo_count(&u), 0);

        tb_pos p = {1, 0};
        undo_insert(&u, p, "z", 1);
        check("an insert is recorded", undo_count(&u), 1);
        /* Inserts keep their bytes too. Undoing one only needs to delete, but
         * doing it again afterwards needs to know what to put back. */
        check("  and holds its byte", undo_text_used(&u), 1);

        undo_delete(&u, p, "ab", 2);
        check("a delete is recorded", undo_count(&u), 2);
        check("  and holds its bytes", undo_text_used(&u), 3);
        check_txt("  which come back out", deleted(&u), "ab");

        /* The record ring is four deep, so a fifth drops the oldest. */
        undo_insert(&u, p, "z", 1);
        undo_insert(&u, p, "z", 1);
        check("four records fit", undo_count(&u), 4);
        undo_insert(&u, p, "z", 1);
        check("the fifth drops the oldest", undo_count(&u), 4);
        check("  and gives back its text", undo_text_used(&u), 6);

        /* An edit larger than the whole ring cannot be undone at all. Keeping
         * the records either side would leave a history that silently skips
         * it, so the log goes. */
        static char big[128];
        memset(big, 'z', sizeof(big));
        undo_delete(&u, p, big, (int) sizeof(big));
        check("an edit too big for the ring clears the log", undo_count(&u), 0);

        /* Suspended, nothing lands. */
        undo_suspend(&u);
        undo_insert(&u, p, "z", 1);
        check("suspended, nothing is recorded", undo_count(&u), 0);
        undo_resume(&u);
        undo_insert(&u, p, "z", 1);
        check("resumed, it records again", undo_count(&u), 1);

        undo_destroy(&u);
    }

    /* --- what the model sends it --- */
    static const char DOC[] = "hello world\r\nsecond line\r\n";
    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    editor ed;
    check("an editor starts", ed_init(&ed, 8, "doc.txt") != NULL, 1);
    undo* u = &ed.undo_;
    text_buffer* tb = &ed.buf_;

    /* Loading normalises line endings through the same primitives an edit uses.
     * The log is attached afterwards, so none of that is undoable -- otherwise
     * the first CTRL+Z would unpick the file's own CRLFs. */
    check("loading records nothing", undo_count(u), 0);

    /* Typing runs together into one record, so one CTRL+Z takes back the run
     * rather than a letter. */
    tb_seek(tb, (tb_pos){1, 0});
    put_str(tb, "abc");
    check("typing coalesces into one record", undo_count(u), 1);
    check("  an insert", rec_op(u, 0), UNDO_INSERT);
    check("  at the line typed on", rec_line(u, 0), 1);
    check("  starting where the run began", rec_x(u, 0), 0);
    check("  as long as the run", rec_len(u, 0), 3);

    /* Moving the cursor away ends the run -- not because anything watches the
     * cursor, but because the next edit is no longer adjacent to the last. */
    tb_seek(tb, (tb_pos){1, 8});
    put_str(tb, "Z");
    check("an edit elsewhere starts a new record", undo_count(u), 2);

    /* A newline is two puts, so it needs no hook of its own. */
    undo_clear(u);
    tb_newline(tb);
    /* One record, not two. Undoing half a line break is not representable. */
    check("a newline is a single record", undo_count(u), 1);
    check("  an insert", rec_op(u, 0), UNDO_INSERT);
    check("  of both bytes", rec_len(u, 0), 2);

    /* Deleting keeps what it removed. */
    undo_clear(u);
    tb_seek(tb, (tb_pos){1, 0});
    tb_del(tb);
    check("a delete is recorded", undo_count(u), 1);
    check("  as a delete", rec_op(u, 0), UNDO_DELETE);
    check_txt("  keeping the byte", deleted(u), "a");

    /* Backspace removes the byte before the cursor, and the record has to point
     * at where that byte was, not at where the cursor started. */
    undo_clear(u);
    tb_seek(tb, (tb_pos){1, 2});
    const int before = tb_xpos(tb);
    tb_bksp(tb);
    check("a backspace is recorded", undo_count(u), 1);
    check("  at the byte it removed, not the cursor",
          rec_x(u, 0), before - 2);

    /* Backspace at the start of a line joins it to the one before, and it does
     * that by calling cb_bksp directly rather than tb_bksp -- the line
     * bookkeeping either side is different. So it is the one mutation that has
     * to record for itself, and the only one a missing hook would lose in
     * silence. */
    undo_clear(u);
    tb_seek(tb, (tb_pos){2, 0});
    check("a merging backspace happens", tb_bksp_merge(tb) ? 1 : 0, 1);
    check("  and is recorded", undo_count(u), 1);
    check("  as a delete", rec_op(u, 0), UNDO_DELETE);
    check("  of two bytes", rec_len(u, 0), 2);
    check_txt("  which are the line break", deleted(u), "\r\n");

    /* The point of recording at the primitives: bulk operations are built from
     * them, so they need no hook of their own. */
    undo_clear(u);
    tb_seek(tb, (tb_pos){1, 0});
    const int len = tb_range_size(tb, (tb_pos){1, 0}, (tb_pos){1, 4});
    tb_range_del(tb, (tb_pos){1, 0}, (tb_pos){1, 4});
    check("a range delete reaches the log without its own hook",
          undo_count(u) > 0, 1);
    check("  coalesced into one record", undo_count(u), 1);
    check("  holding every byte it removed", rec_len(u, 0), len);

    undo_clear(u);
    tb_del_line(tb);
    check("deleting a line reaches it too", undo_count(u) > 0, 1);

    /* Painting must not record. refresh_screen and cmd_repaint_rows walk a
     * tb_copy, and a copy that carried the log could record an edit while
     * drawing. */
    undo_clear(u);
    cmd_repaint_rows(&ed, ed.scr_.topY_, ed.scr_.bottomY_);
    check("painting the screen records nothing", undo_count(u), 0);
    {
        text_buffer cp;
        tb_copy(&cp, tb);
        check("  because a copy carries no log", cp.undo_ == NULL, 1);
    }

    ed_destroy(&ed);

    /* --- replay: does the document actually come back --- */

    /* The whole point, on the simplest case: type, undo, and the line is what
     * it was. Compared as text rather than by counting records, because a log
     * that is right and a document that is wrong is the failure worth catching. */
    {
        stub_file_reset();
        static const char D2[] = "hello world\r\nsecond line\r\n";
        stub_file_set_content(D2, (int) sizeof(D2) - 1);
        editor e;
        check("an editor to undo in", ed_init(&e, 8, "u.txt") != NULL, 1);
        undo* lu = &e.undo_;
        text_buffer* lt = &e.buf_;

        tb_seek(lt, (tb_pos){1, 5});
        put_str(lt, "XY");
        check_txt("typed", line_text(lt, 1), "helloXY world");
        check("undo reports something to do", undo_apply(lu, lt) ? 1 : 0, 1);
        check_txt("  and the whole run is gone at once",
                  line_text(lt, 1), "hello world");
        check("  with nothing left", undo_apply(lu, lt) ? 1 : 0, 0);

        /* A delete comes back too, with its bytes. */
        undo_clear(lu);
        tb_seek(lt, (tb_pos){1, 0});
        tb_del(lt);
        tb_del(lt);
        check_txt("deleted two", line_text(lt, 1), "llo world");
        check("undoing the pair is one step", undo_apply(lu, lt) ? 1 : 0, 1);
        check_txt("  and they are back", line_text(lt, 1), "hello world");

        /* A line break is one record, and undoing it joins the lines again --
         * the case that would break a line index if it went back a byte at a
         * time. */
        undo_clear(lu);
        tb_seek(lt, (tb_pos){1, 5});
        tb_newline(lt);
        check_txt("split the line", line_text(lt, 1), "hello");
        check_txt("  in two", line_text(lt, 2), " world");
        check("undoing the split is one step", undo_apply(lu, lt) ? 1 : 0, 1);
        check_txt("  and the line is whole", line_text(lt, 1), "hello world");
        check("  with the line count back", tb_ymax(lt), 3);

        /* And the reverse: joining two lines, then undoing that. */
        undo_clear(lu);
        tb_seek(lt, (tb_pos){1, 11});
        check("lines merge", tb_del_merge(lt) ? 1 : 0, 1);
        check_txt("  into one", line_text(lt, 1), "hello worldsecond line");
        check("undoing the merge is one step", undo_apply(lu, lt) ? 1 : 0, 1);
        check_txt("  and the first is back", line_text(lt, 1), "hello world");
        check_txt("  and so is the second", line_text(lt, 2), "second line");

        /* Replay must not record, or the log grows as it is consumed. */
        undo_clear(lu);
        tb_seek(lt, (tb_pos){1, 0});
        put_str(lt, "abc");
        const int held = undo_count(lu);
        undo_apply(lu, lt);
        check("replaying does not record", undo_count(lu), held);

        ed_destroy(&e);
    }

    /* --- coalescing and redo --- */
    {
        stub_file_reset();
        static const char D4[] = "abcdefgh\r\nsecond\r\n";
        stub_file_set_content(D4, (int) sizeof(D4) - 1);
        editor e;
        check("an editor for runs", ed_init(&e, 8, "c.txt") != NULL, 1);
        undo* lu = &e.undo_;
        text_buffer* lt = &e.buf_;

        /* Backspace runs leftward, so its bytes arrive reversed. Getting that
         * wrong puts the text back inside out, which is why the run is checked
         * as text and not as a byte count. */
        tb_seek(lt, (tb_pos){1, 5});
        tb_bksp(lt);
        tb_bksp(lt);
        tb_bksp(lt);
        check_txt("three backspaces", line_text(lt, 1), "abfgh");
        check("  coalesce into one record", undo_count(lu), 1);
        check("  a backward delete", rec_op(lu, 0), UNDO_DELETE_BACK);
        check("  starting at the leftmost byte", rec_x(lu, 0), 2);
        check("undoing the run is one step", undo_apply(lu, lt) ? 1 : 0, 1);
        check_txt("  and the text comes back in order",
                  line_text(lt, 1), "abcdefgh");

        /* Redo puts it back again, which is the only reason an insert record
         * carries its bytes. */
        check("there is something to redo", undo_can_redo(lu) ? 1 : 0, 1);
        check("redo reports something to do", redo_apply(lu, lt) ? 1 : 0, 1);
        check_txt("  and the run is gone again", line_text(lt, 1), "abfgh");
        check("  with nothing further to redo", undo_can_redo(lu) ? 1 : 0, 0);

        /* Undo, then a new edit: the redo tail describes a document that no
         * longer exists, so it goes. */
        undo_apply(lu, lt);
        check("undone again", undo_can_redo(lu) ? 1 : 0, 1);
        tb_seek(lt, (tb_pos){1, 0});
        put_str(lt, "Q");
        check("a new edit discards the redo tail",
              undo_can_redo(lu) ? 1 : 0, 0);

        /* A run stops at UNDO_RUN_MAX so one step never swallows a paragraph. */
        undo_clear(lu);
        tb_seek(lt, (tb_pos){1, 0});
        for (int i = 0; i < UNDO_RUN_MAX + 20; i++) {
            tb_put(lt, 'x');
        }
        check("a long run is capped, not endless", undo_count(lu) > 1, 1);
        check("  at the cap", rec_len(lu, 0), UNDO_RUN_MAX);

        /* undo_break ends a run that position alone would have joined. */
        undo_clear(lu);
        tb_seek(lt, (tb_pos){2, 0});
        put_str(lt, "ab");
        check("a run of two", undo_count(lu), 1);
        undo_break(lu);
        put_str(lt, "cd");
        check("  broken, so the next is its own record", undo_count(lu), 2);

        ed_destroy(&e);
    }

    /* Typing then ENTER coalesces, and undoing that has to take the line break
     * with it -- the delete_span loop needs tb_del_merge for the CRLF. */
    {
        stub_file_reset();
        static const char D5[] = "start\r\n";
        stub_file_set_content(D5, (int) sizeof(D5) - 1);
        editor e;
        check("an editor for a run ending in a break",
              ed_init(&e, 8, "n.txt") != NULL, 1);
        tb_seek(&e.buf_, (tb_pos){1, 5});
        put_str(&e.buf_, "XY");
        tb_newline(&e.buf_);
        check("the run absorbed the line break", undo_count(&e.undo_), 1);
        check("  four bytes of it", rec_len(&e.undo_, 0), 4);
        undo_apply(&e.undo_, &e.buf_);
        check_txt("undoing takes the break with it",
                  line_text(&e.buf_, 1), "start");
        check("  and the line count is back", tb_ymax(&e.buf_), 2);
        ed_destroy(&e);
    }

    /* --- the save point --- */
    {
        stub_file_reset();
        static const char D6[] = "one\r\ntwo\r\n";
        stub_file_set_content(D6, (int) sizeof(D6) - 1);
        editor e;
        check("an editor to save from", ed_init(&e, 8, "s.txt") != NULL, 1);
        undo* lu = &e.undo_;
        text_buffer* lt = &e.buf_;

        check("a freshly opened file is clean", tb_changed(lt) ? 1 : 0, 0);
        tb_seek(lt, (tb_pos){1, 3});
        put_str(lt, "ZZ");
        check("typing makes it dirty", tb_changed(lt) ? 1 : 0, 1);

        /* Undoing back to what is on disk means the document matches the file
         * again, so the marker goes -- which is the whole point of tracking a
         * save point rather than a boolean. */
        undo_apply(lu, lt);
        check("undoing back to the file makes it clean",
              tb_changed(lt) ? 1 : 0, 0);
        redo_apply(lu, lt);
        check("  and redoing makes it dirty again", tb_changed(lt) ? 1 : 0, 1);

        /* Saving moves the point, so undoing past it is dirty once more. */
        stub_file_reset();
        check("it saves", tb_save(lt) ? 1 : 0, 1);
        check("  and is clean at the new point", tb_changed(lt) ? 1 : 0, 0);
        undo_apply(lu, lt);
        check("  undoing past a save is dirty again",
              tb_changed(lt) ? 1 : 0, 1);

        ed_destroy(&e);
    }

    /* The save point is an index, and dropping the oldest record shifts every
     * index down. Whether it survives depends on which side of it was dropped.
     *
     * Two logs rather than one, because reaching into cur_ and then recording
     * again is a new edit after an undo -- which discards the log, and would
     * quietly test something else. */
    {
        tb_pos p = {1, 0};

        /* Records older than the save point are dropped. The save is still
         * reachable: undoing everything held lands on it. */
        undo keep;
        check("a small log", undo_init(&keep, 4096, 4) != NULL, 1);
        undo_insert(&keep, p, "a", 1);
        undo_break(&keep);
        undo_insert(&keep, p, "b", 1);
        undo_break(&keep);
        undo_mark_saved(&keep);
        check("saved at the second record",
              undo_at_save_point(&keep) ? 1 : 0, 1);
        for (int i = 0; i < 4; i++) {
            undo_insert(&keep, p, "x", 1);
            undo_break(&keep);
        }
        while (keep.cur_ > 0) {
            keep.cur_--;
        }
        check("dropping older records keeps the save point reachable",
              undo_at_save_point(&keep) ? 1 : 0, 1);
        undo_destroy(&keep);

        /* Enough further edits and records from *after* the save go too. The
         * earliest state the log can reach is then already past it, so no
         * amount of undoing gets back and the document counts as changed until
         * it is written again. */
        undo lost;
        check("another small log", undo_init(&lost, 4096, 4) != NULL, 1);
        undo_insert(&lost, p, "a", 1);
        undo_break(&lost);
        undo_insert(&lost, p, "b", 1);
        undo_break(&lost);
        undo_mark_saved(&lost);
        for (int i = 0; i < 6; i++) {
            undo_insert(&lost, p, "x", 1);
            undo_break(&lost);
        }
        while (lost.cur_ > 0) {
            lost.cur_--;
        }
        check("dropping newer ones loses it for good",
              undo_at_save_point(&lost) ? 1 : 0, 0);
        undo_destroy(&lost);
    }

    /* --- what an undo costs to draw --- */
    {
        static char many[8192];
        int mn = 0;
        for (int i = 1; i <= 100; i++) {
            mn += sprintf(many + mn, "line %03d alpha beta\r\n", i);
        }

        /* A record holds line breaks or it does not, and that decides the
         * shape: undoing an insert of k breaks takes k lines out, undoing a
         * delete of k puts k back, and a record with none touches one line. So
         * the count before and after is enough to pick the repaint, and none of
         * the three redraws the screen. */
        stub_file_reset();
        stub_file_set_content(many, mn);
        editor e;
        check("an editor to measure in", ed_init(&e, 8, "m.txt") != NULL, 1);
        e.scr_.currY_ = (char) (e.scr_.topY_ + 10);
        const int full = e.scr_.cols_ * (e.scr_.bottomY_ - e.scr_.topY_);

        tb_seek(&e.buf_, (tb_pos){50, 4});
        put_str(&e.buf_, "XYZ");
        cap_start();
        cmd_undo(&e);
        int n = cap_read(raw, (int) sizeof(raw));
        check("undoing a one-line edit costs about a row",
              n < e.scr_.cols_ * 3, 1);
        check("  not a screenful", n < full / 4, 1);

        /* Lines removed: the rows below scroll up. One break per record here --
         * two newlines land on different lines and so cannot join, which is
         * why one undo takes one line back rather than both. */
        undo_clear(&e.undo_);
        tb_seek(&e.buf_, (tb_pos){50, 4});
        tb_newline(&e.buf_);
        tb_newline(&e.buf_);
        check("two newlines are two records", undo_count(&e.undo_), 2);
        cap_start();
        cmd_undo(&e);
        n = cap_read(raw, (int) sizeof(raw));
        check("undoing an added line scrolls rather than redraws",
              n < full / 4, 1);
        {
            const char up[4] = {23, 7, 0, 3};
            int found = 0;
            for (int i = 0; i + 4 <= n; i++) {
                if (memcmp(raw + i, up, 4) == 0) {
                    found++;
                }
            }
            check("  scrolling up, once per line taken back", found, 1);
        }

        /* Lines restored: the rows below scroll down, the mirror case. */
        undo_clear(&e.undo_);
        tb_seek(&e.buf_, (tb_pos){50, 0});
        tb_range_del(&e.buf_, (tb_pos){50, 0}, (tb_pos){52, 0});
        cap_start();
        cmd_undo(&e);
        n = cap_read(raw, (int) sizeof(raw));
        check("undoing removed lines scrolls the other way",
              n < full / 2, 1);
        {
            const char down[4] = {23, 7, 0, 2};
            int found = 0;
            for (int i = 0; i + 4 <= n; i++) {
                if (memcmp(raw + i, down, 4) == 0) {
                    found++;
                }
            }
            check("  scrolling down, once per line", found, 2);
        }
        /* Counting scrolls is not enough: the rows the scroll opens up hold
         * whatever was there before, and have to be drawn with the lines that
         * came back. */
        check("  and the restored lines are drawn",
              stream_has(raw, n, "line 051"), 1);

        ed_destroy(&e);
    }

    /* A bulk delete undone puts every byte back, and it never had a hook of its
     * own -- so this is the primitives-are-enough bet, checked end to end. */
    {
        stub_file_reset();
        static const char D3[] = "the quick brown fox\r\n";
        stub_file_set_content(D3, (int) sizeof(D3) - 1);
        editor e;
        check("an editor for a range delete", ed_init(&e, 8, "r.txt") != NULL, 1);

        tb_range_del(&e.buf_, (tb_pos){1, 4}, (tb_pos){1, 10});
        check_txt("a range is gone", line_text(&e.buf_, 1), "the brown fox");
        // Bounded on purpose. An undo that never reports "nothing left" is a
        // real failure mode -- cur_ failing to decrement is one -- and a drain
        // loop that trusts the return value turns that into a hung test rather
        // than a failed assertion.
        int steps = 0;
        while (undo_apply(&e.undo_, &e.buf_) && steps < 1000) {
            steps++;
        }
        check("undoing it all terminates", steps < 1000, 1);
        check_txt("  and undoing it all puts it back",
                  line_text(&e.buf_, 1), "the quick brown fox");
        ed_destroy(&e);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
