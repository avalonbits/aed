/*
 * A cursor that flashes as the MOS prompt's does, for a program that asks for
 * one -- KISS wanted its editor to feel like the prompt.
 *
 * It is the VDP's own cursor, shown over the editor's while the document waits
 * for a key and hidden again when one arrives. Steady is the default and sends
 * nothing; flashing must never be on while a prompt is up, or the VDP flashes
 * it wherever the prompt's last write left it.
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

static int cap_read(unsigned char* buf, int max) {
    fflush(stdout);
    FILE* r = fopen("/tmp/aed_flash_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    const int n = (int) fread(buf, 1, (size_t) max, r);
    fclose(r);

    return n;
}

/* Every place `want` starts in `got`, up to `max` of them. */
static int find_all(const unsigned char* got, int n, const unsigned char* want, int w,
                    int* at, int max) {
    int found = 0;
    for (int i = 0; i + w <= n; i++) {
        if (memcmp(got + i, want, (size_t) w) == 0) {
            if (found < max) {
                at[found] = i;
            }
            found++;
        }
    }

    return found;
}

/* Type an x, quit, and say not to save it: three waits in the editor's loop
 * and one in the save question it asks on the way out. */
static const key_press KEYS[] = {
    { 'x', VK_x, 0 },
    { 17, VK_q, MOD_CTRL },
    { 'n', VK_n, 0 },
};
static int next;

static bool scripted_poll(void* ctx, key_press* kp) {
    (void) ctx;
    if (next >= (int) (sizeof(KEYS) / sizeof(KEYS[0]))) {
        /* Out of script: a loop still asking has failed to leave, and waiting
         * on it would hang the suite rather than fail it. */
        fprintf(stderr, "FAIL  the script ran out with the loop still waiting\n");
        fflush(stdout);
        exit(1);
    }
    *kp = KEYS[next++];

    return true;
}

static const key_source SCRIPT = { scripted_poll, NULL, NULL };

static const app_context APP = {
    .name = "flash", .syntax_dir = "/g", .theme_dir = "/t", .font_dir = "/f",
};
/* Quit is the program's to bind, as KISS and AED do; ED_KEYS has none. */
static const key_binding QUIT[] = { { VK_q, MOD_CTRL, 0, ed_cmd_quit } };
static const keymap KEYMAP = { QUIT, 1, &ED_KEYS };
static const ed_program PROG = { &APP, &KEYMAP, NULL, NULL, " FLASH " };

static editor ed;
static unsigned char got[65536];

static int run(bool flash) {
    stub_file_reset();
    stub_file_add("doc.txt", "hello\r\n", 7);
    if (ed_init_for(&ed, 8, "doc.txt", &PROG) == NULL) {
        return -1;
    }
    /* Steady is whatever a new editor does: nothing is asked for. */
    if (flash) {
        ed_set_cursor_flash(&ed, true);
    }
    ui_set_keys(&ed.ui_, &SCRIPT);
    next = 0;

    cap_start();
    ed_run(&ed);
    const int n = cap_read(got, (int) sizeof(got));
    ed_destroy(&ed);

    return n;
}

int main(void) {
    if (freopen("/tmp/aed_flash_capture", "w+", stdout) == NULL) {
        return 2;
    }
    stub_set_screen(80, 25);

    static const unsigned char on[] = {23, 1, 3};
    static const unsigned char off[] = {23, 1, 0};
    static const char ask[] = "[Y/N/ESC]";
    int ons[8];
    int offs[8];
    int asks[2];

    /* --- steady, the default --- */
    int n = run(false);
    check("steady: the loop ran all three keys", next, 3);
    check("  and never turned the VDP's cursor on",
          find_all(got, n, on, 3, ons, 8), 0);

    /* --- flashing --- */
    n = run(true);
    check("flashing: the loop ran all three keys", next, 3);
    const int non = find_all(got, n, on, 3, ons, 8);
    const int noff = find_all(got, n, off, 3, offs, 8);
    check("  the cursor is shown for each of the editor's two waits", non, 2);
    check("  and hidden after each key", noff, 2);
    check("  each wait opens with it on and closes with it off",
          non == 2 && noff == 2 && ons[0] < offs[0] && offs[0] < ons[1]
              && ons[1] < offs[1], 1);
    const int nask = find_all(got, n, (const unsigned char*) ask, (int) strlen(ask),
                              asks, 2);
    check("the save question is asked", nask, 1);
    check("  while the cursor is hidden",
          nask == 1 && noff == 2 && asks[0] > offs[1], 1);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
