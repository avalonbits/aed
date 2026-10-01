/*
 * The footer names the document on screen, after a program switches to
 * another one.
 *
 * ed_run took the document once, before its loop, and drew every footer from
 * it. A program that keeps several documents switches doc_ while it handles a
 * key -- ade's F3 opens a header beside the source -- and its footer went on
 * naming the first file, at the first file's position. AED has one document
 * and never showed it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "app.h"
#include "editor.h"
#include "keys.h"
#include "user_input.h"

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

static int cap_read(char* buf, int max) {
    fflush(stdout);
    FILE* r = fopen("/tmp/aed_switch_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    const int n = (int) fread(buf, 1, (size_t) max, r);
    fclose(r);

    return n;
}

static int has(const char* got, int n, const char* want) {
    const int w = (int) strlen(want);
    for (int i = 0; i + w <= n; i++) {
        if (memcmp(got + i, want, (size_t) w) == 0) {
            return 1;
        }
    }

    return 0;
}

static editor ed;
static document other;
static view other_view;
static view* home_view;

/* F3, as ade has it: show the other document. */
static void show_other(editor* e) {
    ed_doc_show(e, &other, &other_view);
}

static const key_binding BINDINGS[] = {
    { VK_F3, 0, 0, show_other },
    { VK_q, MOD_CTRL, 0, ed_cmd_quit },
};
static const keymap KEYMAP = { BINDINGS, 2, &ED_KEYS };

/* F3, then CTRL+Q. */
static const key_press KEYS[] = {
    { 0, VK_F3, 0 },
    { 17, VK_q, MOD_CTRL },
};
static int next;
static long after_switch = -1;

static bool scripted_poll(void* ctx, key_press* kp) {
    (void) ctx;
    if (next >= (int) (sizeof(KEYS) / sizeof(KEYS[0]))) {
        fprintf(stderr, "FAIL  the script ran out with the loop still waiting\n");
        exit(1);
    }
    if (next == 0) {
        /* F3 is handed over now: everything from here on was drawn after
         * the switch, the next footer among it. */
        fflush(stdout);
        after_switch = ftell(stdout);
    }
    *kp = KEYS[next++];

    return true;
}

static const key_source SCRIPT = { scripted_poll, NULL, NULL };

static const app_context APP = {
    .name = "switch", .syntax_dir = "/g", .theme_dir = "/t", .font_dir = "/f",
};
static const ed_program PROG = { &APP, &KEYMAP, NULL, NULL, " SWITCH " };

static char got[65536];

int main(void) {
    if (freopen("/tmp/aed_switch_capture", "w+", stdout) == NULL) {
        return 2;
    }
    stub_set_screen(80, 25);
    stub_file_reset();
    stub_file_add("first.c", "int main(void) {\r\n}\r\n", 21);
    stub_file_add("second.h", "a\r\nb\r\nc\r\nd\r\n", 12);

    check("an editor starts on the first", ed_init_for(&ed, 8, "first.c", &PROG) != NULL, 1);
    home_view = ed.scr_.v_;
    scr_view_init(&ed.scr_, &other_view);
    check("  and opens the second", ed_doc_open(&ed, &other, &other_view, 8, "second.h"), TB_OK);
    ed_doc_show(&ed, &ed.home_, home_view);
    ui_set_keys(&ed.ui_, &SCRIPT);

    cap_start();
    ed_run(&ed);
    const int n = cap_read(got, (int) sizeof(got));
    const int from = after_switch >= mark ? (int) (after_switch - mark) : n;

    check("before the switch the footer names the first", has(got, from, "first.c"), 1);
    check("after it, the footer names the second",
          has(got + from, n - from, "second.h"), 1);
    check("  and not the first", has(got + from, n - from, "first.c"), 0);

    ed_doc_close(&other);
    ed_destroy(&ed);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
