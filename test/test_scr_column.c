/*
 * Host tests for the byte-offset -> screen-column projection.
 *
 * scr->currX_ is supposed to be the clamped projection of the document column,
 * but that rule used to be re-derived by hand at every command that moved the
 * cursor -- which is how the deltaX truncation shipped. scr_place_cursor is now
 * its single owner.
 *
 * The projection is written tab-aware from the start. Nothing puts a tab in the
 * buffer yet (they are expanded to spaces on load and while typing), so with
 * today's content scr_column_of(line, len) == len and behaviour is unchanged.
 * The tab cases below pin down the mapping ahead of real tab support, which is
 * what removes the expansion.
 */

#include <stdio.h>
#include <string.h>

#include "screen.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static screen mkscreen(char cols) {
    screen scr;
    memset(&scr, 0, sizeof(scr));
    scr.rows_ = 25;
    scr.cols_ = cols;
    scr.topY_ = 1;
    scr.bottomY_ = 24;
    scr.tab_size_ = SCR_DEFAULT_TAB_SIZE;

    return scr;
}

int main(void) {
    screen scr = mkscreen(80);

    /* --- tab width is configuration, with a sane default and bounds --- */
    check("default tab size", scr_tab_size(&scr), SCR_DEFAULT_TAB_SIZE);
    scr_set_tab_size(&scr, 8);
    check("tab size is settable", scr_tab_size(&scr), 8);
    scr_set_tab_size(&scr, 0);
    check("zero is clamped up to 1", scr_tab_size(&scr), 1);
    scr_set_tab_size(&scr, -3);
    check("negative is clamped up to 1", scr_tab_size(&scr), 1);
    scr_set_tab_size(&scr, 100);
    check("oversized is clamped down", scr_tab_size(&scr), SCR_MAX_TAB_SIZE);
    scr_set_tab_size(&scr, SCR_DEFAULT_TAB_SIZE);

    /* --- with no tabs the projection is the identity: today's behaviour --- */
    const char* plain = "hello world";
    check("no tabs: column == byte offset (0)", scr_column_of(&scr, plain, 0), 0);
    check("no tabs: column == byte offset (5)", scr_column_of(&scr, plain, 5), 5);
    check("no tabs: column == byte offset (11)", scr_column_of(&scr, plain, 11), 11);
    check("NULL line is column 0", scr_column_of(&scr, NULL, 0), 0);
    check("negative length is column 0", scr_column_of(&scr, plain, -1), 0);

    /* --- tabs advance to the next multiple of the width --- */
    const char* t = "\tx";              /* tab at column 0 */
    check("tab at col 0, width 4 -> 4", scr_column_of(&scr, t, 1), 4);
    check("and the next byte is col 5", scr_column_of(&scr, t, 2), 5);

    const char* ab = "ab\tc";           /* tab at column 2 */
    check("tab at col 2, width 4 -> 4", scr_column_of(&scr, ab, 3), 4);

    const char* abcd = "abcd\te";       /* tab exactly on a stop */
    check("tab on a stop advances a full width", scr_column_of(&scr, abcd, 5), 8);

    const char* two = "\t\tz";          /* consecutive tabs */
    check("two tabs -> 8", scr_column_of(&scr, two, 2), 8);

    scr_set_tab_size(&scr, 8);
    check("same line, width 8: tab at col 2 -> 8", scr_column_of(&scr, ab, 3), 8);
    scr_set_tab_size(&scr, 2);
    check("same line, width 2: tab at col 2 -> 4", scr_column_of(&scr, ab, 3), 4);
    scr_set_tab_size(&scr, SCR_DEFAULT_TAB_SIZE);

    /* --- placement clamps to the visible width, and never goes negative --- */
    static char longline[512];
    memset(longline, 'x', sizeof(longline));

    scr_place_cursor(&scr, longline, 10);
    check("placement inside the screen", scr.currX_, 10);

    scr_place_cursor(&scr, longline, 79);
    check("placement at the last column", scr.currX_, 79);

    scr_place_cursor(&scr, longline, 200);
    check("placement past the edge clamps", scr.currX_, 79);

    scr_place_cursor(&scr, longline, 500);
    check("far past the edge still clamps", scr.currX_, 79);

    scr_place_cursor(&scr, NULL, 0);
    check("start of line is column 0", scr.currX_, 0);

    /* A tab-heavy line reaches the edge sooner than its byte count suggests --
     * the case that byte-offset arithmetic gets wrong. */
    static char tabs[64];
    memset(tabs, '\t', sizeof(tabs));
    check("16 tabs at width 4 is column 64", scr_column_of(&scr, tabs, 16), 64);
    scr_place_cursor(&scr, tabs, 30);
    check("30 tabs clamps to the edge", scr.currX_, 79);

    /* A one-column screen must still produce a valid column, not -1. */
    screen narrow = mkscreen(1);
    scr_place_cursor(&narrow, longline, 50);
    check("1-column screen clamps to 0", narrow.currX_, 0);

    /* cols_ == 0 is reachable -- scr_destroy sets it, and a bad scrCols sysvar
     * read would too. cols_-1 is then -1, so the placement must floor at 0
     * rather than hand the VDP a negative column. */
    screen zero = mkscreen(0);
    scr_place_cursor(&zero, longline, 10);
    check("zero-width screen floors at 0", zero.currX_, 0);
    scr_place_cursor(&zero, NULL, 0);
    check("zero-width screen, empty line", zero.currX_, 0);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
