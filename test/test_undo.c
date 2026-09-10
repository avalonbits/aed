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

    /* Typing. One record per byte at this stage; coalescing comes later. */
    tb_seek(tb, (tb_pos){1, 0});
    put_str(tb, "abc");
    check("typing records one per byte", undo_count(u), 3);
    check("  as inserts", rec_op(u, 0), UNDO_INSERT);
    check("  at the line typed on", rec_line(u, 0), 1);
    check("  advancing across the line", rec_x(u, 2), 2);

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
    check("  one record per byte removed", undo_count(u), len);

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
        check("  twice", undo_apply(lu, lt) ? 1 : 0, 1);
        check_txt("  and the line is back", line_text(lt, 1), "hello world");
        check("  with nothing left", undo_apply(lu, lt) ? 1 : 0, 0);

        /* A delete comes back too, with its bytes. */
        undo_clear(lu);
        tb_seek(lt, (tb_pos){1, 0});
        tb_del(lt);
        tb_del(lt);
        check_txt("deleted two", line_text(lt, 1), "llo world");
        undo_apply(lu, lt);
        undo_apply(lu, lt);
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
