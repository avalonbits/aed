/*
 * A message or a question on the prompt row, kept to the row.
 *
 * ui_message writes the message, then " (press any key)", then the cursor;
 * ui_dialog writes the question, then " [Y/N/ESC]: ", then the cursor. The
 * prompt row is the bottom one, and a character printed past the bar's barW_
 * columns wraps off it -- the VDP scrolls the whole screen up a line, header
 * and all, and leaves the document a row out of place. A compiler's error
 * line is easily that long. These follow the VDU stream column by column, as
 * the VDP would, and check that nothing is printed past the bar: a message that
 * fits is shown whole, one a character too long is cut with "...", and a
 * question the same.
 *
 * Only the UI's headers: this is linked against the core and the UI alone.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "screen.h"
#include "user_input.h"
#include "vkey.h"

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
    FILE* r = fopen("/tmp/aed_prompt_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

/* The furthest column anything is printed in, following the stream as the VDP
 * would: VDU 31 moves the cursor, VDU 8 steps back, the colour and viewport
 * sequences take their arguments and print nothing, and every other byte from
 * 32 up prints a cell and moves on. -1 when nothing is printed. */
static int furthest(const unsigned char* b, int n) {
    int x = 0;
    int far = -1;
    for (int i = 0; i < n; i++) {
        switch (b[i]) {
            case 31: x = b[i + 1]; i += 2; break;
            case 17: i += 1; break;
            case 18: i += 2; break;
            case 28: i += 4; break;
            case 8: x--; break;
            default:
                if (b[i] >= 32) {
                    if (x > far) {
                        far = x;
                    }
                    x++;
                }
                break;
        }
    }

    return far;
}

/* Whether `s` is in the stream. */
static int has(const unsigned char* b, int n, const char* s) {
    const int k = (int) strlen(s);
    for (int i = 0; i + k <= n; i++) {
        if (memcmp(b + i, s, (size_t) k) == 0) {
            return 1;
        }
    }

    return 0;
}

static screen scr;
static user_input ui;
static unsigned char got[4096];
static char msg[256];

/* A message of `len` letters, shown, its stream into got; returns its size. */
static int message_of(int len) {
    memset(msg, 'm', (size_t) len);
    msg[len] = 0;
    static const stub_key any[] = {{ 'x', VK_x, 0, 0 }};
    stub_set_keys(any, 1);
    cap_start();
    ui_message(&ui, &scr, msg);

    return cap_read(got, (int) sizeof(got));
}

/* The same for a question, answered yes. */
static int dialog_of(int len) {
    memset(msg, 'q', (size_t) len);
    msg[len] = 0;
    static const stub_key yes[] = {{ 'y', VK_y, 0, 0 }};
    stub_set_keys(yes, 1);
    cap_start();
    ui_dialog(&ui, &scr, msg);

    return cap_read(got, (int) sizeof(got));
}

int main(void) {
    if (freopen("/tmp/aed_prompt_capture", "w+", stdout) == NULL) {
        return 2;
    }
    stub_set_screen(80, 25);
    scr_init(&scr, 32);
    stub_emit_tabs(1);
    if (ui_init(&ui, 64, scr.v_->bottomY_, scr.v_->cols_) == NULL) {
        return 2;
    }
    const int last = scr.barW_ - 1;
    const int dismiss = (int) strlen(" (press any key)");
    const int options = (int) strlen(" [Y/N/ESC]: ");

    /* --- a message --- */
    int n = message_of(scr.barW_ - dismiss - 1);
    check("a message that just fits stays inside the bar",
          furthest(got, n) <= last, 1);
    check("  and is shown whole", has(got, n, "..."), 0);

    n = message_of(scr.barW_ - dismiss);
    check("one a character longer stays inside the bar too",
          furthest(got, n) <= last, 1);
    check("  cut, and marked where", has(got, n, "m...") && has(got, n, " (press any key)"), 1);

    /* What a compiler said, as it said it: 71 columns, with the dismissal
     * 87 on an 80-column screen. */
    strcpy(msg, "main.c:6:2: error: this function returns a value, so 'return' needs one");
    static const stub_key any[] = {{ 'x', VK_x, 0, 0 }};
    stub_set_keys(any, 1);
    cap_start();
    ui_message(&ui, &scr, msg);
    n = cap_read(got, (int) sizeof(got));
    check("a compiler's error line stays inside the bar",
          furthest(got, n) <= last, 1);

    /* --- a question --- */
    n = dialog_of(scr.barW_ - options - 1);
    check("a question that just fits stays inside the bar",
          furthest(got, n) <= last, 1);
    check("  and is asked whole", has(got, n, "..."), 0);

    n = dialog_of(scr.barW_ + 20);
    check("a question too long stays inside the bar",
          furthest(got, n) <= last, 1);
    check("  cut, and marked where", has(got, n, "q...") && has(got, n, " [Y/N/ESC]: "), 1);

    ui_destroy(&ui);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
