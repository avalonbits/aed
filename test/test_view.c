/*
 * Two views on one screen: each paints into its own rectangle.
 *
 * The screen is the device and paints through whichever view it is pointed
 * at, so a program showing two documents keeps a view for each. This splits
 * an 80-column screen into a left and a right view and checks that painting,
 * scrolling and clearing in each stays inside that view's rectangle, and that
 * each keeps its own cursor and scroll while the other is drawn.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "aed.h"
#include "aed_config.h"
#include "screen.h"
#include "user_input.h"
#include "vkey.h"

static int failures = 0;
static long mark;

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
    FILE* r = fopen("/tmp/aed_view_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

/* How many times `ch` was sent -- the characters of a line that reached the
 * VDP, when the line is made of one character nothing else sends. */
static int count(const unsigned char* buf, int n, char ch) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == (unsigned char) ch) {
            c++;
        }
    }

    return c;
}

/* The first VDU 31 (move the text cursor) in a sequence: where a paint
 * started. -1 in both when there is none. */
static void first_tab(const unsigned char* buf, int n, int* x, int* y) {
    *x = *y = -1;
    for (int i = 0; i + 2 < n; i++) {
        if (buf[i] == 31) {
            *x = buf[i + 1];
            *y = buf[i + 2];

            return;
        }
    }
}

/* The VDU 28 (define viewport) that opens a sequence: left, bottom, right,
 * top, or -1 in each when there is none. */
static void viewport(const unsigned char* buf, int n, int* l, int* b, int* r,
                     int* t) {
    *l = *b = *r = *t = -1;
    for (int i = 0; i + 4 < n; i++) {
        if (buf[i] == 28) {
            *l = buf[i + 1];
            *b = buf[i + 2];
            *r = buf[i + 3];
            *t = buf[i + 4];

            return;
        }
    }
}

int main(void) {
    if (freopen("/tmp/aed_view_capture", "w+", stdout) == NULL) {
        return 2;
    }

    static screen scr;
    static unsigned char got[512];
    stub_set_screen(80, 25);
    scr_init(&scr, 32);
    stub_emit_tabs(1);

    /* Split the whole text area -- columns 1 to 78 -- into two views with a
     * gutter column between them: 1..38 and 40..78. */
    view left = scr.whole_;
    left.cols_ = 38;
    view right = scr.whole_;
    right.textX_ = 40;
    right.cols_ = 39;

    static char wide[100];
    memset(wide, 'L', sizeof(wide));

    /* --- a row painted in each lands in that view, cut to its width --- */
    {
        scr_set_view(&scr, &left);
        cap_start();
        scr_write_line(&scr, 3, wide, (int) sizeof(wide));
        int n = cap_read(got, sizeof(got));
        int x, y;
        first_tab(got, n, &x, &y);
        check("a row in the left view starts at its first column", x, 1);
        check("  on the row asked for", y, 3);
        check("  and stops at its width", count(got, n, 'L'), 38);

        memset(wide, 'R', sizeof(wide));
        scr_set_view(&scr, &right);
        cap_start();
        scr_write_line(&scr, 3, wide, (int) sizeof(wide));
        n = cap_read(got, sizeof(got));
        first_tab(got, n, &x, &y);
        check("a row in the right view starts at its first column", x, 40);
        check("  on the row asked for", y, 3);
        check("  and stops at its width", count(got, n, 'R'), 39);
    }

    /* --- a selected row, and the tail from the cursor, stop at the width --- */
    {
        memset(wide, 'S', sizeof(wide));
        scr_set_view(&scr, &right);
        cap_start();
        scr_write_line_sel(&scr, 4, wide, (int) sizeof(wide), 0, 10);
        int n = cap_read(got, sizeof(got));
        int x, y;
        first_tab(got, n, &x, &y);
        check("a selected row in the right view starts at its edge", x, 40);
        check("  and stops at its width", count(got, n, 'S'), 39);

        memset(wide, 'T', sizeof(wide));
        right.currX_ = 0;
        right.currY_ = 6;
        cap_start();
        scr_paint_tail(&scr, wide, (int) sizeof(wide));
        n = cap_read(got, sizeof(got));
        first_tab(got, n, &x, &y);
        check("the tail from the cursor starts at the view's edge", x, 40);
        check("  on the cursor's row", y, 6);
        check("  and stops at its width", count(got, n, 'T'), 39);
    }

    /* --- placing the cursor scrolls by the view's own width --- */
    {
        static char line[60];
        memset(line, 'x', sizeof(line));
        left.originX_ = 0;
        right.originX_ = 0;
        scr_set_view(&scr, &right);
        scr_place_cursor(&scr, line, 50);
        check("a cursor past the right view's width scrolls it",
              right.originX_, 50 - 38);
        check("  to its last column", right.currX_, 38);
        scr_set_view(&scr, &left);
        scr_place_cursor(&scr, line, 50);
        check("the left view scrolls by its own width", left.originX_, 50 - 37);
        check("  leaving the right view's scroll alone", right.originX_, 12);
    }

    /* --- a scroll moves only its own view's columns --- */
    {
        int l, b, r, t;
        scr_set_view(&scr, &right);
        cap_start();
        scr_scroll_up(&scr, right.topY_, (char) (right.bottomY_ - 1), wide, 5, ' ');
        viewport(got, cap_read(got, sizeof(got)), &l, &b, &r, &t);
        check("a scroll in the right view starts at its left edge", l, 40);
        check("  and stops at its right edge", r, 78);

        scr_set_view(&scr, &left);
        cap_start();
        scr_scroll_up(&scr, left.topY_, (char) (left.bottomY_ - 1), wide, 5, ' ');
        viewport(got, cap_read(got, sizeof(got)), &l, &b, &r, &t);
        check("a scroll in the left view starts at its left edge", l, 1);
        check("  and stops at its right edge", r, 38);
    }

    /* --- the multi-row and sideways scrolls do too --- */
    {
        int l, b, r, t;
        scr_set_view(&scr, &right);
        cap_start();
        scr_scroll_rows_up(&scr, right.topY_, (char) (right.bottomY_ - 1), 2);
        viewport(got, cap_read(got, sizeof(got)), &l, &b, &r, &t);
        check("scrolling rows up in the right view", l * 100 + r, 40 * 100 + 78);

        scr_set_view(&scr, &left);
        cap_start();
        scr_scroll_rows_down(&scr, left.topY_, (char) (left.bottomY_ - 1), 2);
        viewport(got, cap_read(got, sizeof(got)), &l, &b, &r, &t);
        check("scrolling rows down in the left view", l * 100 + r, 1 * 100 + 38);

        scr_set_view(&scr, &right);
        cap_start();
        scr_scroll_h(&scr, 3);
        viewport(got, cap_read(got, sizeof(got)), &l, &b, &r, &t);
        check("scrolling sideways in the right view", l * 100 + r, 40 * 100 + 78);
        check("  over its rows", t * 100 + b,
              right.topY_ * 100 + right.bottomY_ - 1);
    }

    /* --- clearing a view clears only its rectangle --- */
    {
        int l, b, r, t;
        scr_set_view(&scr, &right);
        cap_start();
        scr_clear_textarea(&scr, 2, 10);
        viewport(got, cap_read(got, sizeof(got)), &l, &b, &r, &t);
        check("clearing the right view starts at its left edge", l, 40);
        check("  stops at its right edge", r, 78);
        check("  and covers the rows asked for", t * 100 + b, 2 * 100 + 10);
    }

    /* --- each view keeps its own cursor and scroll --- */
    {
        left.currY_ = 5;
        left.originX_ = 0;
        right.currY_ = 9;
        right.originX_ = 12;
        scr_set_view(&scr, &left);
        scr_set_view(&scr, &right);
        check("switching views leaves the left cursor", left.currY_, 5);
        check("  and the right one", right.currY_, 9);
        check("  and each scroll", left.originX_ * 100 + right.originX_, 12);
        check("the screen is drawing the right view",
              scr.v_ == &right ? 1 : 0, 1);
    }

    /* --- the footer is the screen's, below a view that stops short of it --- */
    {
        view upper = scr.whole_;
        upper.bottomY_ = 12;
        scr_set_view(&scr, &upper);
        scr_footer_invalidate(&scr);
        cap_start();
        scr_footer(&scr, "f.c", false, 1, 1);
        const int n = cap_read(got, sizeof(got));
        int l, b, r, t, x, y;
        viewport(got, n, &l, &b, &r, &t);
        first_tab(got, n, &x, &y);
        check("the footer's viewport is the screen's last row",
              t * 100 + b, (scr.rows_ - 1) * 100 + scr.rows_ - 1);
        check("  below a view that stops at row 12", upper.bottomY_, 12);
        check("  and it is written there", y, scr.rows_ - 1);
    }

    /* --- whole-screen dialogs draw across the whole text area --- */
    {
        static user_input ui;
        ui_init(&ui, 256, scr.whole_.bottomY_, scr.whole_.cols_);
        const stub_key esc[] = { { .ch = 27, .vk = VK_ESCAPE } };
        int x, y;

        scr_set_view(&scr, &right);
        stub_set_keys(esc, 1);
        cap_start();
        ui_help(&ui, &scr);
        first_tab(got, cap_read(got, sizeof(got)), &x, &y);
        check("help over the right view starts at the whole screen's edge", x, 1);
        check("  and puts the right view back", scr.v_ == &right ? 1 : 0, 1);

        stub_set_keys(esc, 1);
        config cfg;
        cfg_defaults(&AED_CONFIG, &cfg);
        cap_start();
        ui_settings(&ui, &scr, &cfg);
        first_tab(got, cap_read(got, sizeof(got)), &x, &y);
        check("settings over the right view start at the whole screen's edge",
              x, 1);
        check("  and put the right view back", scr.v_ == &right ? 1 : 0, 1);

        /* The banner is centred, so across the whole screen it starts well
         * left of where the right view begins. */
        cap_start();
        ui_banner(&ui, &scr);
        first_tab(got, cap_read(got, sizeof(got)), &x, &y);
        check("the banner is centred on the whole screen", x > 0 && x < 40, 1);
        check("  and puts the right view back", scr.v_ == &right ? 1 : 0, 1);
        ui_destroy(&ui);
    }

    /* --- NULL goes back to the whole screen --- */
    {
        scr_set_view(&scr, NULL);
        check("no view means the whole screen's text area",
              scr.v_ == &scr.whole_ ? 1 : 0, 1);
        check("  which is still the full width", scr.v_->cols_, 78);
    }

    scr_destroy(&scr);
    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
