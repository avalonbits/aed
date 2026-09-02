/*
 * Host tests for horizontal cursor motion in screen.c.
 *
 * These build the REAL src/screen.c against the stub agon headers in
 * test/stubs, so they exercise the shipping code path rather than a copy.
 *
 * What they pin down: scr_left/scr_right take the motion distance as `int`.
 * They used to take `char`, which is signed and 1 byte on both the eZ80 and
 * the host, so a word motion across a run of >=128 same-class characters
 * (tb_w_next/tb_w_prev are bounded by line length, not screen width)
 * overflowed the parameter negative and desynchronised scr->currX_ from
 * tb->x_. Narrow the parameter back to `char` and these tests fail.
 */

#include <stdio.h>
#include <string.h>

#include "screen.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-44s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-44s got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* An 80x25 screen with the cursor parked at a known column. */
static screen mkscreen(char currX) {
    screen scr;
    memset(&scr, 0, sizeof(scr));
    scr.rows_ = 25;
    scr.cols_ = 80;
    scr.colors_ = 16;
    scr.cursor_ = 32;
    scr.topY_ = 1;
    scr.bottomY_ = 24;
    scr.tab_size_ = 4;
    scr.fg_ = 15;
    scr.bg_ = 0;
    scr.currX_ = currX;
    scr.currY_ = 1;

    return scr;
}

int main(void) {
    /* scr_left/scr_right print through putchar; the assertions are on
     * scr.currX_, so send the glyphs to /dev/null and report on stderr. */
    if (!freopen("/dev/null", "w", stdout)) {
        fprintf(stderr, "could not redirect stdout\n");

        return 2;
    }

    static char line[512];
    memset(line, 'x', sizeof(line));

    /* Sanity: a short motion still lands where it always did. */
    screen scr = mkscreen(10);
    scr_right(&scr, 'a', 'b', 5, line, 300);
    check("scr_right: short motion advances", scr.currX_, 15);

    scr = mkscreen(15);
    scr_left(&scr, 'a', 'b', 5, line, 300);
    check("scr_left: short motion retreats", scr.currX_, 10);

    /* The regression. A 200-column word motion must clamp to the right edge.
     * As a signed char, 200 wrapped to -56, so `x < cols_` held and currX_
     * was assigned -56 instead of clamping. */
    scr = mkscreen(0);
    scr_right(&scr, 'a', 'b', 200, line, 300);
    check("scr_right: 200-col motion clamps to edge", scr.currX_, 79);

    /* Mirror image: a 200-column motion left must clamp to column 0. As a
     * signed char, 200 wrapped to -56 and the cursor moved RIGHT to 135. */
    scr = mkscreen(79);
    scr_left(&scr, 'a', 'b', 200, line, 300);
    check("scr_left: 200-col motion clamps to zero", scr.currX_, 0);

    /* Exactly at the signed-char boundary, where the old code first broke. */
    scr = mkscreen(0);
    scr_right(&scr, 'a', 'b', 128, line, 300);
    check("scr_right: 128-col motion clamps to edge", scr.currX_, 79);

    /* 256 wraps to 0 under truncation, which would leave the cursor parked. */
    scr = mkscreen(0);
    scr_right(&scr, 'a', 'b', 256, line, 300);
    check("scr_right: 256-col motion clamps to edge", scr.currX_, 79);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
