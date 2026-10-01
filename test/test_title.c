/*
 * A program's title is on the header row from the first frame.
 *
 * scr_init draws the header before the editor knows the program's title, and
 * the title only reached the screen when something cleared it afterwards --
 * AED's settings step does, when the file sets a colour, but a program with
 * no settings step and no banner (KISS) started with an empty title bar, and
 * the title only appeared after the first full repaint.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "app.h"
#include "editor.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static long mark;

static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

/* What was written since cap_start(), NUL-terminated. */
static int cap_read(char* buf, int max) {
    fflush(stdout);
    FILE* r = fopen("/tmp/aed_title_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    const int n = (int) fread(buf, 1, (size_t) max - 1, r);
    fclose(r);
    buf[n > 0 ? n : 0] = 0;

    return n;
}

/* The stream holds NULs and control bytes, so it is searched as bytes. */
static int has(const char* got, int n, const char* want) {
    const int w = (int) strlen(want);
    for (int i = 0; i + w <= n; i++) {
        if (memcmp(got + i, want, (size_t) w) == 0) {
            return 1;
        }
    }

    return 0;
}

static const app_context APP = {
    .name = "title", .syntax_dir = "/g", .theme_dir = "/t", .font_dir = "/f",
};
/* As KISS has it: its own title, and no settings step or banner. */
static const ed_program BARE = { &APP, &ED_KEYS, NULL, NULL, " KISS " };

static editor ed;
static char got[65536];

int main(void) {
    if (freopen("/tmp/aed_title_capture", "w+", stdout) == NULL) {
        return 2;
    }
    stub_set_screen(80, 25);
    stub_file_reset();
    stub_file_add("prog.kiss", "hello\r\n", 7);

    cap_start();
    check("an editor starts", ed_init_for(&ed, 8, "prog.kiss", &BARE) != NULL, 1);
    const int n = cap_read(got, (int) sizeof(got));
    check("the title is drawn while it starts", has(got, n, " KISS "), 1);
    check("  and the document under it", has(got, n, "hello"), 1);
    ed_destroy(&ed);

    /* With no file the header is all there is to show, so the title most of all. */
    cap_start();
    check("an editor starts with nothing", ed_init_for(&ed, 8, NULL, &BARE) != NULL, 1);
    const int m = cap_read(got, (int) sizeof(got));
    check("  and its title is drawn too", has(got, m, " KISS "), 1);
    ed_destroy(&ed);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
