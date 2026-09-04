/*
 * Host tests for the footer repaint.
 *
 * The footer is a whole row of characters plus two colour changes, and the
 * event loop asks for it on every pass. Drawing it every time is not just
 * wasteful: those bytes go down the same serial link the VDP sends key packets
 * back on, and the flood delayed the packets. Holding CTRL+SHIFT and tapping an
 * arrow did nothing at all until the keys were released -- the events were
 * queued behind AED's own output.
 *
 * So what is pinned here is that an unchanged footer sends *nothing*, and that
 * a changed one still sends what it should.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "screen.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* The VDU stream is the only place the footer exists, so measure it there.
 * stdout has to be a real file for that: on a terminal it cannot be seeked. */
static long mark;

static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

static int cap_len(void) {
    fflush(stdout);
    const long end = ftell(stdout);

    return (int) (end - mark);
}

int main(void) {
    if (freopen("/tmp/aed_footer_capture", "w+", stdout) == NULL) {
        fprintf(stderr, "cannot redirect stdout\n");

        return 1;
    }

    screen scr;
    scr_init(&scr, 32);

    /* The first one has to draw: nothing is on screen yet. */
    cap_start();
    scr_footer(&scr, "a.txt", false, 1, 1);
    const int first = cap_len();
    check("the first footer is drawn", first > 0, 1);

    /* The event loop asks again with the cursor in the same place. */
    cap_start();
    scr_footer(&scr, "a.txt", false, 1, 1);
    check("an unchanged footer sends nothing", cap_len(), 0);

    cap_start();
    for (int i = 0; i < 50; i++) {
        scr_footer(&scr, "a.txt", false, 1, 1);
    }
    check("...still nothing after fifty passes", cap_len(), 0);

    /* Each of the three things it shows has to bring it back. */
    cap_start();
    scr_footer(&scr, "a.txt", false, 2, 1);
    check("a moved column redraws it", cap_len() > 0, 1);

    cap_start();
    scr_footer(&scr, "a.txt", false, 2, 3);
    check("a moved line redraws it", cap_len() > 0, 1);

    cap_start();
    scr_footer(&scr, "a.txt", true, 2, 3);
    check("the dirty flag redraws it", cap_len() > 0, 1);

    cap_start();
    scr_footer(&scr, "b.txt", true, 2, 3);
    check("a new filename redraws it", cap_len() > 0, 1);

    /* Same length, different name -- a pointer comparison would miss this. */
    cap_start();
    scr_footer(&scr, "c.txt", true, 2, 3);
    check("a same-length rename redraws it", cap_len() > 0, 1);

    /* A name arriving as NULL is shown as [NO FILE], and comparing it must not
     * dereference the null either. */
    cap_start();
    scr_footer(&scr, NULL, true, 2, 3);
    check("no filename redraws it", cap_len() > 0, 1);
    cap_start();
    scr_footer(&scr, NULL, true, 2, 3);
    check("...and then settles", cap_len(), 0);

    /* A prompt takes the footer row, so the editor must not believe its own
     * footer is still there when the prompt closes. */
    scr_footer(&scr, "a.txt", false, 1, 1);
    cap_start();
    scr_footer(&scr, "a.txt", false, 1, 1);
    check("settled again", cap_len(), 0);
    scr_footer_invalidate(&scr);
    cap_start();
    scr_footer(&scr, "a.txt", false, 1, 1);
    check("invalidating forces a redraw", cap_len() > 0, 1);

    /* The common case is that only the cursor moved. That must cost a fraction
     * of a full row: the row is redrawn a column at a time -- the padding is a
     * putchar loop, one MOS call per space -- and all of it except the position
     * field is unchanged. */
    {
        scr_footer_invalidate(&scr);
        cap_start();
        scr_footer(&scr, "a.txt", false, 1, 1);
        const int full = cap_len();

        cap_start();
        scr_footer(&scr, "a.txt", false, 2, 1);
        const int moved = cap_len();

        check("a full footer covers the row", full >= scr.cols_, 1);
        check("a moved cursor sends far less", moved * 2 < full, 1);
        check("...and it is not nothing", moved > 0, 1);

        /* The filename still forces the whole row. */
        cap_start();
        scr_footer(&scr, "bb.txt", false, 2, 1);
        check("a new filename redraws the row", cap_len() >= scr.cols_, 1);
    }

    /* The position field is fixed width, so a long line number must not push
     * the column out of it. Before, a four-digit line added a whole field of
     * padding instead of none. */
    {
        scr_footer_invalidate(&scr);
        cap_start();
        scr_footer(&scr, "a.txt", false, 1, 1);
        const int narrow = cap_len();

        scr_footer_invalidate(&scr);
        cap_start();
        scr_footer(&scr, "a.txt", false, 1, 1234);
        const int wide = cap_len();

        check("a four-digit line does not widen the row", wide, narrow);
    }

    /* A document can outgrow four digits of line number. The position field
     * then needs more columns, and the padding to its left has to give them up
     * -- otherwise the row runs past the end of the screen, and the fast path
     * would go on refreshing only the field, leaving the overflow there for
     * good. */
    {
        scr_footer_invalidate(&scr);
        cap_start();
        scr_footer(&scr, "a.txt", false, 1, 1);
        check("a short line number fills the row", cap_len(), scr.cols_);

        scr_footer_invalidate(&scr);
        cap_start();
        scr_footer(&scr, "a.txt", false, 1, 123456);
        check("a six-digit line number also fills it", cap_len(), scr.cols_);

        /* Growing into a wider field moves everything left of it, so the whole
         * row has to be drawn even though the file did not change. */
        scr_footer_invalidate(&scr);
        scr_footer(&scr, "a.txt", false, 1, 9999);
        cap_start();
        scr_footer(&scr, "a.txt", false, 1, 10000);
        check("widening the field redraws the row", cap_len(), scr.cols_);

        /* Within one width it stays on the fast path. */
        cap_start();
        scr_footer(&scr, "a.txt", false, 1, 10001);
        check("...but moving within it does not", cap_len() * 2 < scr.cols_, 1);
    }

    /* A full-screen refresh clears the text area with the viewport running to
     * bottomY_ -- which is the footer row -- and then paints only the rows
     * above it. So it erases the footer without drawing it back. */
    scr_footer(&scr, "a.txt", false, 1, 1);
    cap_start();
    scr_footer(&scr, "a.txt", false, 1, 1);
    check("settled before the refresh", cap_len(), 0);
    scr_clear_textarea(&scr, scr.topY_, scr.bottomY_);
    cap_start();
    scr_footer(&scr, "a.txt", false, 1, 1);
    check("a full refresh brings the footer back", cap_len() > 0, 1);

    /* A clear that stops above the footer leaves it alone, and must not throw
     * the saving away. */
    scr_footer(&scr, "a.txt", false, 1, 1);
    scr_clear_textarea(&scr, scr.topY_, (char) (scr.bottomY_ - 1));
    cap_start();
    scr_footer(&scr, "a.txt", false, 1, 1);
    check("a partial clear still sends nothing", cap_len(), 0);

    /* Clearing the screen takes the footer with it. */
    scr_footer(&scr, "a.txt", false, 1, 1);
    scr_clear(&scr);
    cap_start();
    scr_footer(&scr, "a.txt", false, 1, 1);
    check("a cleared screen forces a redraw", cap_len() > 0, 1);

    /* Every write is an entry into MOS -- putchar is one per byte -- and the
     * paint paths batch a run into a single one. The stream is the same either
     * way, so this is measured in calls rather than bytes. */
    {
        scr_footer_invalidate(&scr);
        stub_writes_reset();
        scr_footer(&scr, "a.txt", false, 1, 1);
        const int calls = stub_writes();
        check("a whole footer row is a handful of writes", calls < 10, 1);
        check("...and the row itself goes in one", stub_max_write() > 40, 1);

        /* The cursor is put back after the row, not before it: buffered output
         * has to be flushed before anything moves the cursor, or it lands in
         * the wrong place. */
        check("the cursor is moved after the row is written",
              stub_tab_at(), stub_writes());
    }

    scr_destroy(&scr);
    fclose(stdout);
    remove("/tmp/aed_footer_capture");

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
