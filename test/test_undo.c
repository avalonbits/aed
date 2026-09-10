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
        undo_insert(&u, p, 1);
        check("an insert is recorded", undo_count(&u), 1);
        check("  and holds no text", undo_text_used(&u), 0);

        undo_delete(&u, p, "ab", 2);
        check("a delete is recorded", undo_count(&u), 2);
        check("  and holds its bytes", undo_text_used(&u), 2);
        check_txt("  which come back out", deleted(&u), "ab");

        /* The record ring is four deep, so a fifth drops the oldest. */
        undo_insert(&u, p, 1);
        undo_insert(&u, p, 1);
        check("four records fit", undo_count(&u), 4);
        undo_insert(&u, p, 1);
        check("the fifth drops the oldest", undo_count(&u), 4);
        check("  and gives back its text", undo_text_used(&u), 2);

        /* An edit larger than the whole ring cannot be undone at all. Keeping
         * the records either side would leave a history that silently skips
         * it, so the log goes. */
        static char big[128];
        memset(big, 'z', sizeof(big));
        undo_delete(&u, p, big, (int) sizeof(big));
        check("an edit too big for the ring clears the log", undo_count(&u), 0);

        /* Suspended, nothing lands. */
        undo_suspend(&u);
        undo_insert(&u, p, 1);
        check("suspended, nothing is recorded", undo_count(&u), 0);
        undo_resume(&u);
        undo_insert(&u, p, 1);
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
    check("a newline records its CR and LF", undo_count(u), 2);
    check("  both as inserts", rec_op(u, 1), UNDO_INSERT);

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

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
