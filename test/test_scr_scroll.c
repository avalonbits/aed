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
    scr.cursor_ = 32;
    scr.topY_ = 1;
    scr.bottomY_ = 24;
    scr.tab_size_ = SCR_DEFAULT_TAB_SIZE;
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

    /* VDU 28,left,bottom,right,top  then  VDU 23,7,0,dir,8  then  VDU 26.
     * Direction 3 is up, 2 is down -- the one byte that distinguishes them. */
    const unsigned char want_up[] = {
        28, 0, 20, 79, 3,     /* viewport: left 0, bottom 20, top 3, and right =
                               * the last column. VDU 28 takes character
                               * positions, so it is cols_-1 and not cols_. */
        23, 7, 0, 3, 8,       /* scroll up */
        26,                   /* reset viewport */
    };
    const unsigned char want_down[] = {
        28, 0, 20, 79, 3,
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
    const unsigned char want_region[] = {28, 0, 12, 79, 7, 23, 7, 0, 3, 8, 26};
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
    const unsigned char want_clear[] = {28, 0, 18, 79, 2};
    check_seq("clear_textarea defines the same viewport shape", got, n,
              want_clear, (int) sizeof(want_clear));
    check("clear_textarea resets the viewport", got[n - 1], 26);

    /* Horizontal region scroll: same VDU 23,7 mechanism, directions 0 and 1.
     * Scrolling the window right means moving the content left, so a positive
     * column count must emit direction 1. */
    const unsigned char want_right[] = {28, 0, 23, 79, 1, 23, 7, 0, 1, 8, 26};
    cap_start();
    scr_scroll_h(&scr, 1);
    n = cap_read(got, sizeof(got));
    check_seq("scroll window right -> direction 1", got, n,
              want_right, (int) sizeof(want_right));

    const unsigned char want_left[] = {28, 0, 23, 79, 1, 23, 7, 0, 0, 8, 26};
    cap_start();
    scr_scroll_h(&scr, -1);
    n = cap_read(got, sizeof(got));
    check_seq("scroll window left  -> direction 0", got, n,
              want_left, (int) sizeof(want_left));

    /* A multi-column hop repeats the scroll inside one viewport definition. */
    cap_start();
    scr_scroll_h(&scr, 3);
    n = cap_read(got, sizeof(got));
    const unsigned char want3[] = {28, 0, 23, 79, 1,
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
