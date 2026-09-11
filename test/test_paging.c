/*
 * Host tests for line numbers that do not assume the whole document is in
 * memory.
 *
 * The line index describes the lines the character buffer is holding, and
 * nothing else. Every line number the editor deals in is "which line of the
 * document", and today those are the same question only because the document is
 * entirely in memory. head_lines_ and tail_lines_ are the difference, and they
 * are zero until there is somewhere else for a document to live.
 *
 * Which makes them hard to test: the arithmetic that matters is the arithmetic
 * that never runs. tb_set_offscreen is the seam -- it says "pretend this much of
 * the document is elsewhere" without there being an elsewhere, so the numbering
 * can be held to its contract now, while a document that really is all in memory
 * can still say what the answers should be.
 *
 * See .internal/docs/PAGING.md.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

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

/* Five lines, so there is somewhere to move to. */
static const char FIVE[] = "one\r\ntwo\r\nthree\r\nfour\r\nfive\r\n";

static int load_five(text_buffer* tb) {
    stub_file_reset();
    stub_file_set_content(FIVE, (int) sizeof(FIVE) - 1);

    return tb_init(tb, 4, "five.txt") != NULL;
}

int main(void) {
    stub_discard_output();

    text_buffer tb;

    /* --- with nothing elsewhere, the numbers are what they always were --- */
    {
        check("a document loads", load_five(&tb), 1);
        /* Five breaks, so six lines: the last CRLF opens an empty one. */
        check("  six lines", tb_ymax(&tb), 6);
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);
        check("  and the top is line one", tb_ypos(&tb), 1);
        tb_destroy(&tb);
    }

    /* --- a head shifts where the cursor is, and how long the document is --- */
    {
        check("a document to put a head in front of", load_five(&tb), 1);
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);

        tb_set_offscreen(&tb, 100, 0);
        check("the first line in memory is line 101", tb_ypos(&tb), 101);
        check("  and the document is 100 lines longer", tb_ymax(&tb), 106);

        /* Moving inside memory still moves one line at a time. */
        tb_down(&tb);
        check("  down one is 102", tb_ypos(&tb), 102);
        tb_down(&tb);
        tb_down(&tb);
        check("  and three down is 104", tb_ypos(&tb), 104);
        tb_up(&tb);
        check("  up one is 103", tb_ypos(&tb), 103);

        /* tb_tell is the position everything else is expressed in. */
        check("  and tb_tell agrees with tb_ypos", tb_tell(&tb).line, tb_ypos(&tb));
        tb_destroy(&tb);
    }

    /* --- a tail lengthens the document without moving the cursor --- */
    {
        check("a document to put a tail behind", load_five(&tb), 1);
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);

        tb_set_offscreen(&tb, 0, 40);
        check("the cursor is still on line one", tb_ypos(&tb), 1);
        check("  and the document is 40 lines longer", tb_ymax(&tb), 46);

        tb_set_offscreen(&tb, 100, 40);
        check("both ends count", tb_ymax(&tb), 146);
        check("  and only the head moves the cursor", tb_ypos(&tb), 101);
        tb_destroy(&tb);
    }

    /* --- a seek is in document lines, not memory lines --- */
    {
        check("a document to seek about in", load_five(&tb), 1);
        tb_set_offscreen(&tb, 100, 40);

        tb_pos at = { 103, 0 };
        tb_seek(&tb, at);
        check("seeking to 103 lands on 103", tb_ypos(&tb), 103);
        const split_line ln = tb_curr_line(&tb);
        check("  which is the third line in memory", ln.psz_ + ln.ssz_,
              (int) strlen("three"));

        /* A line that is in the head cannot be reached, because nothing has
         * been built yet that could fetch it. It stops at the top of memory
         * rather than running away or pretending -- and this is exactly where
         * step 2 hooks the slide. */
        tb_pos in_head = { 50, 0 };
        tb_seek(&tb, in_head);
        check("a line in the head stops at the top of memory",
              tb_ypos(&tb), 101);

        tb_pos in_tail = { 200, 0 };
        tb_seek(&tb, in_tail);
        check("  and one in the tail stops at the bottom", tb_ypos(&tb), 106);
        tb_destroy(&tb);
    }

    /* --- a walker sees what the cursor sees --- */
    {
        check("a document to walk", load_five(&tb), 1);
        tb_set_offscreen(&tb, 100, 40);
        tb_pos at = { 102, 0 };
        tb_seek(&tb, at);

        text_buffer cp;
        tb_copy(&cp, &tb);
        check("a copy starts where the cursor is", tb_ypos(&cp), 102);
        check("  and sees the same document length", tb_ymax(&cp), 146);
        tb_down(&cp);
        check("  and numbers its own movement the same way", tb_ypos(&cp), 103);
        check("  without moving the cursor it came from", tb_ypos(&tb), 102);
        tb_destroy(&tb);
    }

    /* --- emptying the document empties both ends --- */
    {
        check("a document to clear", load_five(&tb), 1);
        tb_set_offscreen(&tb, 100, 40);
        tb_clear(&tb);
        check("a cleared document is one line", tb_ymax(&tb), 1);
        check("  starting at line one", tb_ypos(&tb), 1);
        tb_destroy(&tb);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
