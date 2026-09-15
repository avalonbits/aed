/*
 * The four ways of moving the cursor sideways.
 *
 * cmd_left, cmd_right, cmd_w_left and cmd_w_right are the same command four
 * times over: check for the edge of the line, take one step, and tell the
 * screen where the cursor went. Only the edge to check and the step to take
 * differ. Nothing tested them -- the fuzz pressed the keys and test_input
 * checked which key reaches which function, but what they do once called was
 * covered nowhere, so the shape they share could be changed without anything
 * noticing.
 *
 * Written against the behaviour as it stood, so that collapsing the four into
 * their common shape has something to answer to.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "cmd_ops.h"
#include "text_buffer.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static editor ed;

/* line, column -- the pair every one of these tests is about. */
static int at_line(void) { return tb_ypos(&ed.buf_); }
static int at_col(void)  { return ed.buf_.x_; }

static int load(const char* doc) {
    stub_file_reset();
    stub_file_set_content(doc, (int) strlen(doc));

    return ed_init(&ed, 8, "/m.txt") != NULL;
}

static void seek(int line, int x) {
    tb_pos p = { line, x };
    tb_seek(&ed.buf_, p);
}

int main(void) {
    stub_discard_output();

    /* "one two" / "three four" / "five" */
    static const char DOC[] = "one two\r\nthree four\r\nfive\r\n";

    /* --- a step at a time --- */
    {
        check("a document to walk across", load(DOC), 1);

        seek(1, 0);
        cmd_right(&ed);
        check("  RIGHT from the start of a line", at_col(), 1);
        check("    stays on the line", at_line(), 1);

        cmd_left(&ed);
        check("  LEFT puts it back", at_col(), 0);
        check("    still on the line", at_line(), 1);
        ed_destroy(&ed);
    }

    /* --- LEFT at the start of a line goes up and to the end --- */
    {
        check("a document to step off the front of", load(DOC), 1);
        seek(2, 0);
        cmd_left(&ed);
        check("  LEFT at the start of line 2 goes up", at_line(), 1);
        check("    landing at the end of the line above", at_col(), 7);

        /* and on the first line there is nowhere to go */
        seek(1, 0);
        cmd_left(&ed);
        check("  LEFT at the very start stays put", at_line(), 1);
        check("    at column zero", at_col(), 0);
        ed_destroy(&ed);
    }

    /* --- RIGHT at the end of a line goes down and home --- */
    {
        check("a document to step off the end of", load(DOC), 1);
        seek(1, 7);
        cmd_right(&ed);
        check("  RIGHT at the end of line 1 goes down", at_line(), 2);
        check("    landing at the start of it", at_col(), 0);

        /* the last line has nothing below it */
        const int last = tb_ymax(&ed.buf_);
        seek(last, 0);
        const int before = at_line();
        cmd_right(&ed);
        check("  RIGHT at the end of the document stays put", at_line(), before);
        ed_destroy(&ed);
    }

    /* --- by word --- */
    {
        /* "one two" and "three four": the stops are the exact columns, so a
         * word step that quietly became a single step is caught. */
        check("a document to cross by word", load(DOC), 1);
        seek(1, 0);
        cmd_w_right(&ed);
        check("  CTRL+RIGHT stops at the end of the first word", at_col(), 3);
        cmd_w_right(&ed);
        check("    then at the start of the next", at_col(), 4);
        cmd_w_right(&ed);
        check("    then at its end", at_col(), 7);
        check("      all on the same line", at_line(), 1);
        cmd_w_right(&ed);
        check("    and then onto the next line", at_line(), 2);
        check("      at its start", at_col(), 0);

        seek(2, 10);
        cmd_w_left(&ed);
        check("  CTRL+LEFT stops at the start of the last word", at_col(), 5);
        cmd_w_left(&ed);
        check("    then at the end of the one before", at_col(), 4);
        cmd_w_left(&ed);
        check("    then at the line's start", at_col(), 0);
        check("      still on the line", at_line(), 2);
        cmd_w_left(&ed);
        check("    and then up to the line above", at_line(), 1);
        check("      at its end", at_col(), 7);
        ed_destroy(&ed);
    }

    /* --- by word, off the edges: the same edge rules as a single step --- */
    {
        check("a document to cross the edges by word", load(DOC), 1);
        seek(2, 0);
        cmd_w_left(&ed);
        check("  CTRL+LEFT at the start of a line goes up", at_line(), 1);
        check("    to the end of the line above", at_col(), 7);

        seek(1, 7);
        cmd_w_right(&ed);
        check("  CTRL+RIGHT at the end of a line goes down", at_line(), 2);
        check("    to the start of it", at_col(), 0);

        seek(1, 0);
        cmd_w_left(&ed);
        check("  CTRL+LEFT at the very start stays put", at_line(), 1);
        check("    at column zero", at_col(), 0);
        ed_destroy(&ed);
    }

    /* --- the whole line, one step at a time, ends where the line ends --- */
    {
        check("a line to cross a step at a time", load(DOC), 1);
        seek(1, 0);
        int guard = 0;
        while (guard++ < 40 && at_line() == 1) {
            cmd_right(&ed);
        }
        check("  RIGHT reaches the next line", at_line(), 2);
        check("    without running away", guard < 40, 1);

        guard = 0;
        while (guard++ < 40 && at_line() == 2) {
            cmd_left(&ed);
        }
        check("  and LEFT comes back to the first", at_line(), 1);
        check("    landing at its end", at_col(), 7);
        ed_destroy(&ed);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
