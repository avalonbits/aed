/*
 * Host tests for walkers: the read-only copies a repaint, a search and the
 * range operations make with tb_copy.
 *
 * A walker shares the original's buffers. Moving one used to mean moving the
 * gap, which duplicates rather than destroys -- so the cursor that owns the
 * buffer still reads the same bytes, but only while the gap is wider than the
 * distance the walker has travelled. That is what makes prime_spare keep a
 * third of the free space at the cursor, and what stops the buffer shrinking.
 *
 * These tests hold a walker's position to the arithmetic that replaces the
 * moving: wline_ is the line and woff_ is the byte offset of that line's start
 * from lo_. While the walk is also still moving the buffer, the two have to
 * agree, and that agreement is what says the arithmetic is right before
 * anything depends on it.
 *
 * See .internal/docs/WALKER.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "char_buffer.h"
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

/*
 * What the walker's numbers claim, against where the buffer actually is.
 *
 * woff_ is the start of the line and x_ is the column within it, so together
 * they are the cursor's distance from lo_ -- which is exactly what the gap
 * sitting at curr_ says while a walk is still moving it. Returns the
 * difference, so a failure prints how far out it was rather than just "1".
 */
static int drift(text_buffer* w) {
    const int by_pointer = (int) (w->cb_.curr_ - w->cb_.lo_);
    const int by_number = w->woff_ + w->x_;

    return by_number - by_pointer;
}

/* A document of `lines` lines, each `len` bytes of text plus a CRLF. */
static char* make_doc(int lines, int len, int* size) {
    const int per = len + 2;
    char* doc = (char*) malloc((size_t) (lines * per + 1));
    if (doc == NULL) {
        return NULL;
    }
    int at = 0;
    for (int i = 0; i < lines; i++) {
        for (int k = 0; k < len; k++) {
            doc[at++] = (char) ('a' + ((i + k) % 26));
        }
        doc[at++] = '\r';
        doc[at++] = '\n';
    }
    doc[at] = 0;
    *size = at;

    return doc;
}

int main(void) {
    stub_discard_output();

    static text_buffer tb;

    /* --- a walker's numbers say where it is --- */
    {
        #define LINES 60
        #define LEN   20
        int size = 0;
        char* doc = make_doc(LINES, LEN, &size);
        if (doc == NULL) {
            fprintf(stderr, "FAIL  out of memory\n");

            return 1;
        }
        stub_file_reset();
        stub_file_set_content(doc, size);
        check("a document to walk", tb_init(&tb, 16, "/walk.txt") != NULL, 1);

        /* From the middle, so there is buffer on both sides of the cursor and
         * the walk crosses it in both directions. */
        tb_pos mid = { LINES / 2, 0 };
        tb_seek(&tb, mid);

        text_buffer w;
        tb_copy(&w, &tb);
        check("a walker starts on the line the cursor is on",
              w.wline_ + 1, tb_ypos(&tb));
        check("  and its offset is where the buffer says it is", drift(&w), 0);

        /* Down to the end of the document, one line at a time. */
        int worst = 0;
        int steps = 0;
        while (tb_ypos(&w) < LINES) {
            const int before = tb_ypos(&w);
            tb_down(&w);
            if (tb_ypos(&w) == before) {
                break;
            }
            steps++;
            const int d = drift(&w);
            if (d != 0 && worst == 0) {
                worst = d;
            }
        }
        check("  it steps down the rest of the document", steps, LINES / 2);
        check("    and its numbers keep up the whole way", worst, 0);

        /* And back up to the top. */
        worst = 0;
        steps = 0;
        while (tb_ypos(&w) > 1) {
            const int before = tb_ypos(&w);
            tb_up(&w);
            if (tb_ypos(&w) == before) {
                break;
            }
            steps++;
            const int d = drift(&w);
            if (d != 0 && worst == 0) {
                worst = d;
            }
        }
        check("  and back up to the first line", steps, LINES - 1);
        check("    with its numbers still keeping up", worst, 0);
        check("  landing on line one", w.wline_, 0);
        check("    at offset zero", w.woff_, 0);

        tb_destroy(&tb);
        free(doc);
    }

    /* --- a column is not a line --- */
    {
        /* woff_ is the start of the line, so moving along a line must leave it
         * alone and moving between lines must not lose the column. Getting
         * these two mixed up is the arithmetic error that the walk above
         * cannot catch, because it only ever sits in column zero. */
        int size = 0;
        char* doc = make_doc(10, 20, &size);
        stub_file_reset();
        stub_file_set_content(doc, size);
        check("a document to walk along", tb_init(&tb, 16, "/cols.txt") != NULL, 1);
        tb_pos at = { 5, 7 };
        tb_seek(&tb, at);

        text_buffer w;
        tb_copy(&w, &tb);
        check("a walker inherits the column", w.x_, 7);
        check("  and its numbers agree there too", drift(&w), 0);
        const int line_start = w.woff_;

        tb_end(&w);
        check("  to the end of the line", w.x_, 20);
        check("    without moving the line it is on", w.woff_, line_start);
        check("    and still agreeing", drift(&w), 0);

        tb_home(&w);
        check("  and home again", w.x_, 0);
        check("    same line", w.woff_, line_start);
        check("    still agreeing", drift(&w), 0);

        tb_destroy(&tb);
        free(doc);
    }

    /* --- walking down from a column that is not zero --- */
    {
        /* tb_down measures the rest of the line it is leaving, so the line's
         * whole length is that plus the column the walker was standing in.
         * Dropping the column is invisible from column zero, which is where
         * every repaint starts -- and wrong by x_ for every line after the
         * first anywhere else. */
        int size = 0;
        char* doc = make_doc(20, 30, &size);
        stub_file_reset();
        stub_file_set_content(doc, size);
        check("a document to walk down the middle of",
              tb_init(&tb, 16, "/mid.txt") != NULL, 1);
        tb_pos at = { 3, 11 };
        tb_seek(&tb, at);

        text_buffer w;
        tb_copy(&w, &tb);
        check("a walker starting in column eleven", w.x_, 11);

        int worst = 0;
        int steps = 0;
        while (tb_ypos(&w) < 20) {
            const int before = tb_ypos(&w);
            tb_down(&w);
            if (tb_ypos(&w) == before) {
                break;
            }
            steps++;
            if (drift(&w) != 0 && worst == 0) {
                worst = drift(&w);
            }
        }
        check("  walks down without losing its place", steps, 17);
        check("    and its numbers keep up", worst, 0);
        check("  still in column eleven at the bottom", w.x_, 11);

        tb_destroy(&tb);
        free(doc);
    }

    /* --- a buffer whose text does not start at the allocation --- */
    {
        /* lo_ is where the live bytes start, and it is only the same as buf_
         * while nothing has slid. A walker's offset is measured from lo_, so a
         * copy that measures from buf_ agrees with the pointers on every
         * document that fits in memory and is wrong by the free space below
         * lo_ on every one that does not. */
        #define PAGED_LINES 4000
        #define PAGED_LEN   40
        static char big[PAGED_LINES * PAGED_LEN + 1];
        for (int i = 0; i < PAGED_LINES; i++) {
            for (int k = 0; k < PAGED_LEN - 2; k++) {
                big[i * PAGED_LEN + k] = (char) ('a' + ((i + k) % 26));
            }
            big[i * PAGED_LEN + PAGED_LEN - 2] = '\r';
            big[i * PAGED_LEN + PAGED_LEN - 1] = '\n';
        }
        stub_file_reset();
        stub_file_set_content(big, PAGED_LINES * PAGED_LEN);
        check("a paged document", tb_init(&tb, 64, "/paged.txt") != NULL, 1);
        check("  really pages", tb.paged_ ? 1 : 0, 1);

        /* Far enough down that the window has slid and left free space below
         * the live text, which is the whole point of this case. */
        tb_pos deep = { PAGED_LINES / 2, 0 };
        tb_seek(&tb, deep);
        check("  with its text away from the start of the buffer",
              tb.cb_.lo_ > tb.cb_.buf_ ? 1 : 0, 1);

        text_buffer w;
        tb_copy(&w, &tb);
        check("  a walker still lands on the right byte", drift(&w), 0);

        int worst = 0;
        for (int i = 0; i < 40; i++) {
            tb_down(&w);
            if (drift(&w) != 0 && worst == 0) {
                worst = drift(&w);
            }
        }
        check("    and stays on it walking down", worst, 0);
        for (int i = 0; i < 80; i++) {
            tb_up(&w);
            if (drift(&w) != 0 && worst == 0) {
                worst = drift(&w);
            }
        }
        check("    and walking back up past where it started", worst, 0);

        tb_destroy(&tb);
    }

    /* --- the owner is where the walker started --- */
    {
        /* tb_copy reads the owner's position out of its pointers. A walker
         * seeded from a cursor sitting mid-line has to account for the column,
         * or every line it reads afterwards is x_ bytes out. */
        int size = 0;
        char* doc = make_doc(10, 20, &size);
        stub_file_reset();
        stub_file_set_content(doc, size);
        tb_init(&tb, 16, "/seed.txt");

        int wrong = 0;
        for (int line = 1; line <= 10 && wrong == 0; line++) {
            for (int x = 0; x <= 20; x += 5) {
                tb_pos p = { line, x };
                tb_seek(&tb, p);
                text_buffer w;
                tb_copy(&w, &tb);
                if (drift(&w) != 0 || w.wline_ + 1 != tb_ypos(&tb)) {
                    wrong = line * 100 + x;
                    break;
                }
            }
        }
        check("a walker seeded from anywhere lands on the same byte", wrong, 0);

        tb_destroy(&tb);
        free(doc);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
