/*
 * A row of the program's own under the header.
 *
 * scr_set_subheader gives a program row 1 -- ade's menu bar, under its title
 * -- and the text area starts a row lower. These check that the whole
 * screen's view and the views made from it start at row 2 once it is set;
 * that clearing the screen draws the title on row 0 and the program's row on
 * row 1; that scr_header_draw draws both again; that laying the screen out
 * again, as a mode or font change does, keeps the text at row 2; and that
 * NULL gives the row back.
 *
 * Only the UI's headers: this is linked against the core and the UI alone.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "screen.h"

static int failures = 0;
static long mark = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

/* What was written to stdout since cap_start(). */
static int cap_read(unsigned char* buf, int max) {
    fflush(stdout);
    int n = (int) (ftell(stdout) - mark);
    if (n > max) {
        n = max;
    }
    FILE* r = fopen("/tmp/aed_subheader_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

/* The row `text` was printed on: the y of the last VDU 31 before it, or -1
 * when it is not in the stream. */
static int row_of(const unsigned char* b, int n, const char* text) {
    const int k = (int) strlen(text);
    int y = -1;
    for (int i = 0; i < n; i++) {
        if (b[i] == 31 && i + 2 < n) {
            y = b[i + 2];
            i += 2;
            continue;
        }
        if (i + k <= n && memcmp(b + i, text, (size_t) k) == 0) {
            return y;
        }
    }

    return -1;
}

static int drawn = 0;

/* The program's row: a word, so the stream shows where it went. */
static void menu_bar(screen* scr, void* ctx) {
    (void) ctx;
    drawn++;
    scr_bar_line(scr, 1, "MENUBAR", 7);
}

static screen scr;
static unsigned char got[8192];

int main(void) {
    if (freopen("/tmp/aed_subheader_capture", "w+", stdout) == NULL) {
        return 2;
    }
    stub_set_screen(80, 25);
    scr_init(&scr, 32);
    stub_emit_tabs(1);
    scr_set_title(&scr, "THE TITLE");
    check("without one, the text starts at row 1", scr.whole_.topY_, 1);

    /* --- setting it --- */
    scr_set_subheader(&scr, menu_bar, NULL);
    check("with one, the whole screen's text starts at row 2",
          scr.whole_.topY_, 2);
    check("  and its cursor is moved down onto it", scr.whole_.currY_, 2);
    view v;
    scr_view_init(&scr, &v);
    check("a view made after starts at row 2 too", v.topY_, 2);
    check("  and its cursor there", v.currY_, 2);

    /* --- drawn on each clear --- */
    cap_start();
    scr_clear(&scr);
    int n = cap_read(got, (int) sizeof(got));
    check("clearing draws the title on row 0", row_of(got, n, "THE TITLE"), 0);
    check("  and the program's row on row 1", row_of(got, n, "MENUBAR"), 1);
    check("  and leaves the cursor at the top of the text",
          scr.v_->currY_, 2);

    /* --- and again on asking --- */
    drawn = 0;
    cap_start();
    scr_header_draw(&scr);
    n = cap_read(got, (int) sizeof(got));
    check("scr_header_draw draws the program's row again", drawn, 1);
    check("  on row 1", row_of(got, n, "MENUBAR"), 1);

    /* --- laid out again --- */
    scr_resume(&scr);
    check("laid out again, the text still starts at row 2",
          scr.whole_.topY_, 2);

    /* --- given back --- */
    scr_set_subheader(&scr, NULL, NULL);
    check("NULL gives the row back to the text", scr.whole_.topY_, 1);
    drawn = 0;
    cap_start();
    scr_clear(&scr);
    n = cap_read(got, (int) sizeof(got));
    check("  and clearing no longer draws it", drawn, 0);
    check("  the title still on row 0", row_of(got, n, "THE TITLE"), 0);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
