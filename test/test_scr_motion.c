/*
 * Host tests for horizontal motion and the scroll origin.
 *
 * scr_left/scr_right/scr_home/scr_end used to each take a byte delta and do
 * their own byte-indexed slicing of the line. That is where the deltaX
 * truncation lived, and it is also what made real tabs impossible: one byte can
 * occupy several columns, so a byte delta is not a screen delta.
 *
 * They are now one primitive, scr_move_cursor, which is told where the cursor
 * is in the line and moves the horizontal origin itself. There is no delta to
 * truncate any more; what needs pinning down is that the origin follows the
 * cursor correctly in both directions, including across tabs.
 */

#include <stdio.h>
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

static screen mkscreen(char cols) {
    screen scr;
    memset(&scr, 0, sizeof(scr));
    scr.rows_ = 25;
    scr.cols_ = cols;
    scr.cursor_ = 32;
    scr.topY_ = 1;
    scr.bottomY_ = 24;
    scr.tab_size_ = SCR_DEFAULT_TAB_SIZE;
    scr.currY_ = 3;
    scr.originX_ = 0;

    return scr;
}

int main(void) {
    stub_discard_output();

    static char plain[512];
    memset(plain, 'x', sizeof(plain));

    /* --- the window follows the cursor rightwards --- */
    screen scr = mkscreen(80);
    scr_move_cursor(&scr, 'a', 'b', plain, 10, plain + 10, 60);
    check("short move stays at origin 0", scr.originX_, 0);
    check("  and the cursor is at its column", scr.currX_, 10);

    scr_move_cursor(&scr, 'a', 'b', plain, 79, plain + 79, 60);
    check("column 79 still fits", scr.originX_, 0);
    check("  cursor at the last column", scr.currX_, 79);

    scr_move_cursor(&scr, 'a', 'b', plain, 200, plain + 200, 60);
    check("column 200 scrolls the window", scr.originX_, 121);
    check("  cursor pinned to the right edge", scr.currX_, 79);

    /* A further move right by one advances the origin by exactly one. */
    scr_move_cursor(&scr, 'a', 'b', plain, 201, plain + 201, 60);
    check("one more column, origin advances by one", scr.originX_, 122);
    check("  cursor still at the right edge", scr.currX_, 79);

    /* --- and leftwards, which the old code only handled by jumping to 0 --- */
    scr_move_cursor(&scr, 'a', 'b', plain, 150, plain + 150, 60);
    check("moving left inside the window keeps origin", scr.originX_, 122);
    check("  cursor tracks the column", scr.currX_, 28);

    scr_move_cursor(&scr, 'a', 'b', plain, 100, plain + 100, 60);
    check("moving left past the origin scrolls back", scr.originX_, 100);
    check("  cursor at the left edge", scr.currX_, 0);

    scr_move_cursor(&scr, 'a', 'b', NULL, 0, plain, 200);
    check("home returns the window to 0", scr.originX_, 0);
    check("  cursor at column 0", scr.currX_, 0);

    /* --- the same, but where bytes and columns differ --- */
    static char tabs[64];
    memset(tabs, '\t', sizeof(tabs));

    scr = mkscreen(80);
    /* 30 tabs at width 4 is column 120: past the edge, so the window moves. */
    scr_move_cursor(&scr, 'a', 'b', tabs, 30, tabs + 30, 10);
    check("30 tabs is column 120, so origin moves", scr.originX_, 120 - 79);
    check("  cursor at the right edge", scr.currX_, 79);

    /* One tab back is 4 columns back, not 1 -- the whole point. */
    scr_move_cursor(&scr, 'a', 'b', tabs, 29, tabs + 29, 10);
    check("one tab back is four columns back", scr.originX_ + scr.currX_, 116);

    /* A 128+ column motion in one step: the case that used to truncate. */
    scr = mkscreen(80);
    scr_move_cursor(&scr, 'a', 'b', tabs, 40, tabs + 40, 10);
    check("160-column motion lands correctly", scr.originX_ + scr.currX_, 160);
    check("  and stays on screen", scr.currX_, 79);

    /* --- a mixed line: tabs then text --- */
    scr = mkscreen(80);
    char mixed[] = "\t\tab\tc";           /* cols: 0,4 -> 8,9 -> 12 */
    check("mixed: after two tabs, column 8", scr_column_of(&scr, mixed, 2), 8);
    check("mixed: after 'ab', column 10", scr_column_of(&scr, mixed, 4), 10);
    check("mixed: after the third tab, column 12", scr_column_of(&scr, mixed, 5), 12);

    /* Inverse must agree with the forward mapping. */
    check("byte_at(col 0) == 0", scr_byte_at(&scr, mixed, 6, 0), 0);
    check("byte_at(col 8) == 2", scr_byte_at(&scr, mixed, 6, 8), 2);
    check("byte_at(col 10) == 4", scr_byte_at(&scr, mixed, 6, 10), 4);
    check("byte_at inside a tab rounds forward", scr_byte_at(&scr, mixed, 6, 2), 1);
    check("byte_at past the end clamps to len", scr_byte_at(&scr, mixed, 6, 999), 6);

    /* Round trip on every byte offset of the mixed line. */
    int rt = 1;
    for (int i = 0; i <= 6; i++) {
        const int col = scr_column_of(&scr, mixed, i);
        if (scr_byte_at(&scr, mixed, 6, col) != i) {
            rt = 0;
        }
    }
    check("column_of/byte_at round trip on every offset", rt, 1);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
