/*
 * Placing a document's cursor without drawing it.
 *
 * A program with several documents -- ade, coming back from a build -- puts
 * each cursor back where it was and then shows one. Recentring through
 * cmd_restore_after_modal repaints the whole screen, a fifth of a second a
 * time on the Agon; ed_doc_place sets the document and its view instead, and
 * the one ed_doc_show after it paints. These check that it draws nothing,
 * that what it leaves paints exactly what recentring would have, that near
 * the top of a document it sits as low as the lines above allow, and that a
 * column past the edge scrolls the view sideways to show it -- and that the
 * scroll survives the show, which clears the screen first.
 *
 * Only the UI's headers: this is linked against the core and the UI alone.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "app.h"
#include "cmd_ops.h"
#include "editor.h"

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
    FILE* r = fopen("/tmp/aed_place_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

static const app_context APP = {
    .name = "place", .syntax_dir = "/g", .theme_dir = "/t", .font_dir = "/f",
};
static const ed_program PROG = { &APP, &ED_KEYS, NULL, NULL, NULL };

static editor ed;
static document other;
static view other_view;
static char text[8192];
static unsigned char want[16384];
static unsigned char got[16384];

int main(void) {
    if (freopen("/tmp/aed_place_capture", "w+", stdout) == NULL) {
        return 2;
    }
    stub_set_screen(80, 25);
    stub_file_reset();
    /* Cursor moves are output too: placing must not send one either. */
    stub_emit_tabs(1);

    /* Sixty numbered lines, the fortieth long enough to run off the right. */
    int n = 0;
    for (int i = 1; i <= 60; i++) {
        n += sprintf(text + n, "line %d", i);
        if (i == 40) {
            for (int k = 0; k < 120; k++) {
                text[n++] = (char) ('a' + k % 26);
            }
        }
        text[n++] = '\r';
        text[n++] = '\n';
    }
    stub_file_add("home.txt", text, n);
    stub_file_add("other.txt", text, n);

    check("an editor starts on its own document",
          ed_init_for(&ed, 8, "home.txt", &PROG) != NULL, 1);
    view* home_view = ed.scr_.v_;
    scr_view_init(&ed.scr_, &other_view);
    check("a second document opens",
          ed_doc_open(&ed, &other, &other_view, 8, "other.txt"), TB_OK);
    ed_doc_show(&ed, &ed.home_, home_view);

    /* --- placing a document that is not on screen draws nothing --- */
    cap_start();
    ed_doc_place(&ed, &other, &other_view, 30, 3);
    check("placing a document off screen sends nothing to the VDP",
          cap_read(got, (int) sizeof(got)), 0);
    check("  and its cursor is where it was put",
          tb_ypos(&other.buf_) * 100 + tb_xpos(&other.buf_), 30 * 100 + 4);
    check("  the document on screen is still current",
          ed.doc_ == &ed.home_, 1);
    check("  and the screen still points at its view",
          ed.scr_.v_ == home_view, 1);
    check("  the placed view has the line centred",
          other_view.currY_,
          other_view.topY_ + (other_view.bottomY_ - other_view.topY_) / 2);

    /* --- what it leaves paints what recentring would have --- */
    /* The way to put a cursor somewhere and centre it until now: seek, then
     * cmd_restore_after_modal with `moved`, which clears and repaints. */
    tb_seek(&ed.home_.buf_, (tb_pos) { .line = 30, .x = 0 });
    tb_settle(&ed.home_.buf_);
    cap_start();
    cmd_restore_after_modal(&ed, true);
    const int wn = cap_read(want, (int) sizeof(want));

    /* Back to the top, then the same place by ed_doc_place and a show. */
    tb_seek(&ed.home_.buf_, (tb_pos) { .line = 1, .x = 0 });
    tb_settle(&ed.home_.buf_);
    cmd_restore_after_modal(&ed, true);
    ed_doc_place(&ed, &ed.home_, home_view, 30, 0);
    cap_start();
    ed_doc_show(&ed, &ed.home_, home_view);
    const int gn = cap_read(got, (int) sizeof(got));
    check("placed and shown paints what recentring paints",
          wn > 0 && gn == wn && memcmp(got, want, (size_t) wn) == 0, 1);

    /* --- near the top there is not enough above to centre against --- */
    ed_doc_place(&ed, &other, &other_view, 3, 0);
    check("line 3 sits as low as the two lines above it allow",
          other_view.currY_ - other_view.topY_, 2);

    /* --- a column past the edge scrolls the view to show it --- */
    ed_doc_place(&ed, &other, &other_view, 40, 100);
    check("a column past the right edge scrolls the view sideways",
          other_view.originX_ > 0, 1);
    check("  and the cursor is on that column",
          other_view.originX_ + other_view.currX_, 100);
    check("  inside the view",
          other_view.currX_ < other_view.cols_, 1);

    /* ...and stays scrolled when shown: putting the document back after a
     * clear keeps the view's origin, as it keeps the cursor's column, or the
     * text would be drawn from the start of the line under a cursor that is
     * still counted from the scroll. */
    ed_doc_show(&ed, &other, &other_view);
    check("shown, the view is still scrolled to the column",
          other_view.originX_ + other_view.currX_, 100);
    check("  and the cursor is on it", tb_xpos(&other.buf_), 101);

    ed_doc_close(&other);
    ed_destroy(&ed);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
