/*
 * Host tests at the controller level.
 *
 * These drive whole commands rather than view primitives, because the two worst
 * bugs so far lived in the controller/view interaction and nothing below that
 * level could reach them:
 *
 *   - inserting a character repainted from the cursor, which sits *after* it,
 *     so the character itself was never drawn;
 *   - an edit that also scrolled the window let the VDP region scroll shift the
 *     row's old pixels, which only reproduces the document while the text is
 *     unchanged -- so the inserted character vanished.
 *
 * Both were found by driving the emulator by hand. This is the cheaper net.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "cmd_ops.h"

static int failures = 0;
static long mark;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void cap_start(void) { fflush(stdout); mark = ftell(stdout); }

static int cap_read(char* buf, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    int n = (int)(end - mark);
    if (n > max) {
        n = max;
    }
    FILE* r = fopen("/tmp/aed_cmd_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

/* The characters the window should be showing, taken from the document rather
 * than hardcoded, so the expectation cannot drift from the model. No tabs in
 * these fixtures, so a column is a byte. */
static void expected_window(editor* ed, char* out, int max) {
    char line[512];
    int n = 0;
    int psz = 0, ssz = 0;
    char* pre = tb_prefix(&ed->buf_, &psz);
    char* suf = tb_suffix(&ed->buf_, &ssz);
    for (int i = 0; i < psz && n < (int) sizeof(line); i++) {
        line[n++] = pre[i];
    }
    for (int i = 0; i < ssz && n < (int) sizeof(line); i++) {
        line[n++] = suf[i];
    }

    int w = 0;
    for (int c = ed->scr_.originX_; c < ed->scr_.originX_ + ed->scr_.cols_
                                    && c < n && w < max - 1; c++) {
        out[w++] = line[c];
    }
    out[w] = 0;
}

static int contains(const char* hay, int hn, const char* needle) {
    const int nn = (int) strlen(needle);
    for (int i = 0; i + nn <= hn; i++) {
        if (memcmp(hay + i, needle, (size_t) nn) == 0) {
            return 1;
        }
    }

    return 0;
}

/* A 20-column screen so the window has to move after only a few columns. */
static void setup(editor* ed) {
    memset(ed, 0, sizeof(*ed));
    ed->scr_.rows_ = 25;
    ed->scr_.cols_ = 20;
    ed->scr_.cursor_ = 32;
    ed->scr_.topY_ = 1;
    ed->scr_.bottomY_ = 24;
    ed->scr_.tab_size_ = SCR_DEFAULT_TAB_SIZE;
    ed->scr_.currY_ = 1;
    ed->scr_.currX_ = 0;
    ed->scr_.originX_ = 0;

    if (!tb_init(&ed->buf_, 4, NULL)) {
        fprintf(stderr, "tb_init failed\n");
        return;
    }
    if (!ui_init(&ed->ui_, 64, ed->scr_.bottomY_, ed->scr_.cols_)) {
        fprintf(stderr, "ui_init failed\n");
    }
}

int main(void) {
    if (freopen("/tmp/aed_cmd_capture", "w+", stdout) == NULL) {
        fprintf(stderr, "capture failed\n");

        return 2;
    }

    char buf[4096];
    editor ed;
    setup(&ed);

    /* "abcdefghij..." long enough that the cursor at the end is off screen. */
    for (int i = 0; i < 40; i++) {
        tb_put(&ed.buf_, (char)('a' + (i % 26)));
    }
    /* Put the view where the cursor is, as the editor would have. */
    int psz = 0;
    char* pre = tb_prefix(&ed.buf_, &psz);
    (void) scr_place_cursor(&ed.scr_, pre, psz);
    check("cursor is off the left edge of the window", ed.scr_.originX_, 40 - 19);

    /* Insert at the right edge: this both changes the row and moves the window.
     * The document becomes ...xyzZ with the cursor after Z, so the window shows
     * the last 20 columns of that. */
    const key k = { 'Z', VK_Z };
    cap_start();
    cmd_putc(&ed, k);
    int n = cap_read(buf, sizeof(buf));

    char want[64];
    expected_window(&ed, want, sizeof(want));
    check("the inserted character reaches the screen",
          contains(buf, n, "Z"), 1);
    check("the whole visible window is redrawn from the document",
          contains(buf, n, want), 1);

    /* A backspace that does not move the window only needs the tail redrawn,
     * which is the cheaper path and correct. */
    cap_start();
    cmd_bksp(&ed);
    n = cap_read(buf, sizeof(buf));
    check("non-scrolling backspace leaves no Z on screen",
          contains(buf, n, "Z"), 0);

    /* Now a backspace that *does* move the window: walk the cursor to the left
     * edge first, so deleting pushes it past the origin. */
    while (ed.scr_.currX_ > 0) {
        cmd_left(&ed);
    }
    const int origin_before = ed.scr_.originX_;
    cap_start();
    cmd_bksp(&ed);
    n = cap_read(buf, sizeof(buf));
    check("backspace at the left edge moves the window",
          ed.scr_.originX_ < origin_before, 1);
    expected_window(&ed, want, sizeof(want));
    check("and redraws the row from the document",
          contains(buf, n, want), 1);

    tb_destroy(&ed.buf_);
    ui_destroy(&ed.ui_);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
