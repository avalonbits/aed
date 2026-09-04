/*
 * Host tests for the region-scroll primitives.
 *
 * These used to live in cmd_ops.c, which meant the controller emitted raw VDU
 * sequences and carried its own copy of define_viewport -- a second, differently
 * shaped one from the copy in screen.c. This pins the byte sequences down now
 * that both live in the view.
 *
 * The stub vdp_* calls are inert, so the captured stream contains only what
 * reaches the VDP through mos_puts and putchar: the viewport and scroll control
 * sequences, then the painted line.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "screen.h"

static int failures = 0;
static FILE* cap;
static long mark;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-50s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-50s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

/* Reads back what was written to stdout since cap_start(). */
static int cap_read(unsigned char* buf, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    int n = (int)(end - mark);
    if (n > max) {
        n = max;
    }
    FILE* r = fopen("/tmp/aed_vdu_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

static void check_seq(const char* name, const unsigned char* got, int gotsz,
                      const unsigned char* want, int wantsz) {
    if (gotsz >= wantsz && memcmp(got, want, (size_t) wantsz) == 0) {
        fprintf(stderr, "PASS  %-50s %d control bytes\n", name, wantsz);
    } else {
        fprintf(stderr, "FAIL  %-50s got", name);
        for (int i = 0; i < gotsz && i < wantsz + 2; i++) {
            fprintf(stderr, " %d", got[i]);
        }
        fprintf(stderr, ", want");
        for (int i = 0; i < wantsz; i++) {
            fprintf(stderr, " %d", want[i]);
        }
        fprintf(stderr, "\n");
        failures++;
    }
}

static screen mkscreen(void) {
    screen scr;
    memset(&scr, 0, sizeof(scr));
    scr.rows_ = 25;
    scr.cols_ = 80;
    /* Bars one wider than the text, and the text starting one column in: what
     * scr_init derives. Left at zero this fixture would paint the text area
     * against the screen edge and none of the viewport bytes would match. */
    scr.barW_ = 81;
    scr.textX_ = 1;
    scr.cursor_ = 32;
    scr.topY_ = 1;
    scr.bottomY_ = 24;
    scr.tab_size_ = SCR_DEFAULT_TAB_SIZE;
    /* What scr_init derives from the pixel dimensions for the stock 8x8 system
     * font. A hand-built fixture that leaves these zero would emit a movement
     * byte of 0, which on VDP 1.04 means no movement at all. */
    scr.charW_ = 8;
    scr.charH_ = 8;
    scr.currX_ = 0;
    scr.currY_ = 5;

    return scr;
}

int main(void) {
    cap = freopen("/tmp/aed_vdu_capture", "w+", stdout);
    if (cap == NULL) {
        fprintf(stderr, "could not capture stdout\n");

        return 2;
    }

    screen scr = mkscreen();
    unsigned char got[64];
    char line[] = "hello";

    /* The fixture above sets the layout by hand, so the assertions below are
     * about the shape of the sequence rather than the width production runs at.
     * This one goes through scr_init instead: MOS reports 80 columns, the
     * rightmost is never written, and the text area is inset one further on
     * each side. So the bars span 79 columns from 0, and the text is 78 wide
     * starting at column 1 -- which puts its viewport at columns 1 to 78 and
     * leaves column 79 blank to match column 0. */
    {
        stub_set_screen(80, 25);
        screen real;
        scr_init(&real, 32);
        check("the bars span the drawable width", real.barW_, 79);
        check("the text area is inset on both sides", real.cols_, 78);
        check("and starts one column in", real.textX_, 1);

        cap_start();
        scr_scroll_h(&real, 1);
        const int rn = cap_read(got, sizeof(got));
        /* VDU 28,left,bottom,right,top -- left is the margin, right the last
         * text column. Neither edge column belongs to the scrolling region. */
        check("a scroll viewport starts after the left margin",
              rn > 1 && got[1] == 1, 1);
        check("and stops before the right one", rn > 3 && got[3] == 78, 1);

        /* Where the paints actually land. A cursor tab emits nothing into the
         * captured stream, so the byte sequences above say how wide the text
         * area is but not where it starts -- and the margin is entirely a
         * question of where it starts. */
        char row[8] = "abc";
        scr_write_line(&real, real.topY_, row, 3);
        check("a text row is painted from the margin, not column 0",
              stub_last_tab_x(), 1);

        /* Drawing the footer must hand the cursor back into the text area
         * rather than leave it parked on the bar it just drew. The bar spans
         * from column 0, the text does not, so the two disagree by the margin
         * and returning to the wrong one is invisible in the byte stream. */
        scr_footer_invalidate(&real);
        scr_footer(&real, "a.txt", false, 1, 1);
        check("the footer returns the cursor to the text row",
              stub_last_tab_y(), real.currY_);
        check("  at the text column, margin included",
              stub_last_tab_x(), real.currX_ + real.textX_);

        /* The cursor sits at text column 0 after a clear, which is screen
         * column 1. Reading back the raw tab is what pins the offset down. */
        cap_start();
        scr_clear(&real);
        /* Its own buffer: `got` is 64 bytes and a full-width header is longer,
         * so reusing it would silently measure the truncation instead. */
        unsigned char hdr[256];
        const int hn = cap_read(hdr, (int) sizeof(hdr));
        check("the cursor after a clear sits on the margin",
              stub_last_tab_x(), 1);
        /* The header is a bar and must fill one. Halving the spare columns and
         * using that on both sides drops the odd one, which left the rule a
         * column short -- invisible until the text area reached the column
         * before the bar's last and started sticking out past it. */
        check("the header spans the whole bar", hn, real.barW_);
        scr_destroy(&real);
    }

    /* The VDU 23,7 movement byte is a pixel count, and it has to be the real
     * cell size rather than the 8 the system font happens to use. Movement 0
     * would say "one character cell" and would be the obvious choice, but that
     * meaning only arrived in Console8 VDP 2.5.0 -- on the VDP 1.04 this editor
     * supports it means no movement, so 0 must never reach the wire. A 16-pixel
     * font therefore has to scroll by 16. */
    {
        stub_set_cell(16, 16);
        stub_set_screen(80, 25);
        screen big;
        scr_init(&big, 32);
        check("cell height derived from the pixel dimensions", big.charH_, 16);
        check("cell width derived from the pixel dimensions", big.charW_, 16);

        cap_start();
        scr_scroll_up(&big, 3, 20, line, 5, 'x');
        const int bn = cap_read(got, sizeof(got));
        check("vertical scroll moves by one cell, not a fixed 8",
              bn > 9 && got[9] == 16, 1);

        cap_start();
        scr_scroll_h(&big, 1);
        const int hn = cap_read(got, sizeof(got));
        check("horizontal scroll moves by one cell too",
              hn > 9 && got[9] == 16, 1);

        scr_destroy(&big);
        stub_set_cell(8, 8);
    }

    /* VDU 28,left,bottom,right,top  then  VDU 23,7,0,dir,8  then  VDU 26.
     * Direction 3 is up, 2 is down -- the one byte that distinguishes them. */
    const unsigned char want_up[] = {
        28, 1, 20, 80, 3,     /* viewport: left = the margin, bottom 20, top 3,
                               * right = the last text column. VDU 28 takes
                               * character positions, so right is
                               * textX_ + cols_ - 1, not a count. */
        23, 7, 0, 3, 8,       /* scroll up */
        26,                   /* reset viewport */
    };
    const unsigned char want_down[] = {
        28, 1, 20, 80, 3,
        23, 7, 0, 2, 8,       /* scroll down -- direction byte differs */
        26,
    };

    cap_start();
    scr_scroll_up(&scr, 3, 20, line, 5, 'x');
    int n = cap_read(got, sizeof(got));
    check("scroll up produced output", n > 0, 1);
    check_seq("scroll up: viewport, VDU 23,7,0,3,8, reset", got, n,
              want_up, (int) sizeof(want_up));

    cap_start();
    scr_scroll_down(&scr, 3, 20, line, 5, 'x');
    n = cap_read(got, sizeof(got));
    check_seq("scroll down: same, direction byte 2", got, n,
              want_down, (int) sizeof(want_down));

    /* The viewport must track the caller's region rather than a fixed one --
     * the bug the duplicated define_viewport risked reintroducing. */
    const unsigned char want_region[] = {28, 1, 12, 80, 7, 23, 7, 0, 3, 8, 26};
    cap_start();
    scr_scroll_up(&scr, 7, 12, line, 5, 'x');
    n = cap_read(got, sizeof(got));
    check_seq("viewport follows the requested region", got, n,
              want_region, (int) sizeof(want_region));

    /* The exposed line is painted after the control sequence. */
    cap_start();
    scr_scroll_up(&scr, 3, 20, line, 5, 'z');
    n = cap_read(got, sizeof(got));
    check("line text follows the control bytes", memcmp(got + 11, "hello", 5) == 0, 1);

    /* scr_clear_textarea shares the viewport primitive; it must still bracket
     * its clear with a viewport definition and a reset. */
    cap_start();
    scr_clear_textarea(&scr, 2, 18);
    n = cap_read(got, sizeof(got));
    const unsigned char want_clear[] = {28, 1, 18, 80, 2};
    check_seq("clear_textarea defines the same viewport shape", got, n,
              want_clear, (int) sizeof(want_clear));
    check("clear_textarea resets the viewport", got[n - 1], 26);

    /* Horizontal region scroll: same VDU 23,7 mechanism, directions 0 and 1.
     * Scrolling the window right means moving the content left, so a positive
     * column count must emit direction 1. */
    const unsigned char want_right[] = {28, 1, 23, 80, 1, 23, 7, 0, 1, 8, 26};
    cap_start();
    scr_scroll_h(&scr, 1);
    n = cap_read(got, sizeof(got));
    check_seq("scroll window right -> direction 1", got, n,
              want_right, (int) sizeof(want_right));

    const unsigned char want_left[] = {28, 1, 23, 80, 1, 23, 7, 0, 0, 8, 26};
    cap_start();
    scr_scroll_h(&scr, -1);
    n = cap_read(got, sizeof(got));
    check_seq("scroll window left  -> direction 0", got, n,
              want_left, (int) sizeof(want_left));

    /* A multi-column hop repeats the scroll inside one viewport definition. */
    cap_start();
    scr_scroll_h(&scr, 3);
    n = cap_read(got, sizeof(got));
    const unsigned char want3[] = {28, 1, 23, 80, 1,
                                   23, 7, 0, 1, 8,
                                   23, 7, 0, 1, 8,
                                   23, 7, 0, 1, 8, 26};
    check_seq("three columns -> three scroll commands", got, n,
              want3, (int) sizeof(want3));

    /* Zero must emit nothing at all. */
    cap_start();
    scr_scroll_h(&scr, 0);
    n = cap_read(got, sizeof(got));
    check("a zero-column scroll emits nothing", n, 0);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
