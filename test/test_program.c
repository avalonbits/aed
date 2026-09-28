/*
 * Starting an editor for a program other than AED.
 *
 * ed_init_for takes the program: where its files are, its keys, and its part
 * of starting up -- settings as soon as the screen exists, a report once the
 * prompts do, and a banner for starting with nothing. These start one for a
 * made-up program and check each part is its own and runs where it should.
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

static const app_context MINE_APP = {
    .name = "mine",
    .cfg_path = "/config/mine.ini",
    .syntax_dir = "/config/mine/syntax",
    .theme_dir = "/config/mine/themes",
    .font_dir = "/config/mine",
};

static int f9s;

static void on_f9(editor* ed) {
    (void) ed;
    f9s++;
}

static const key_binding MINE_BINDINGS[] = {
    { VK_F9, 0, 0, on_f9 },
};
static const keymap MINE_KEYS = { MINE_BINDINGS, 1, NULL };

/* What each hook saw, and in which order they ran. */
static int order;
static int settings_at, started_at, banner_at;
static int settings_rows, settings_ui_cols;
static int started_ui_cols;
static int banners;

static void mine_settings(editor* ed) {
    settings_at = ++order;
    settings_rows = ed->scr_.rows_;
    settings_ui_cols = ed->ui_.cols_;
}

static void mine_started(editor* ed) {
    started_at = ++order;
    started_ui_cols = ed->ui_.cols_;
}

static void mine_banner(user_input* ui, screen* scr) {
    (void) ui;
    (void) scr;
    banner_at = ++order;
    banners++;
}

static const ed_program MINE = {
    &MINE_APP, &MINE_KEYS, mine_settings, mine_started, mine_banner,
};

static const ed_program BARE = { &MINE_APP, NULL, NULL, NULL, NULL };

static void reset(void) {
    order = settings_at = started_at = banner_at = 0;
    settings_rows = settings_ui_cols = started_ui_cols = -1;
    banners = 0;
}

int main(void) {
    stub_discard_output();
    static editor ed;

    /* --- started with nothing --- */
    {
        reset();
        stub_set_screen(80, 25);
        stub_file_reset();
        memset(&ed, 0, sizeof(ed));
        check("an editor starts for another program",
              ed_init_for(&ed, 8, NULL, &MINE) != NULL, 1);
        check("  with that program's files in force",
              app_get() == &MINE_APP ? 1 : 0, 1);
        check("  and its keys", ed.keys_ == &MINE_KEYS ? 1 : 0, 1);
        check("its settings run first", settings_at, 1);
        check("  once the screen exists", settings_rows, 25);
        check("  before the prompts do", settings_ui_cols, 0);
        check("its report runs next", started_at, 2);
        check("  once the prompts exist", started_ui_cols > 0 ? 1 : 0, 1);
        check("and its banner last, with no file named", banner_at, 3);
        check("  once", banners, 1);
        check("  and the editor knows one is up", ed.banner_ ? 1 : 0, 1);
        check("its own key runs its own command",
              ed_handle(&ed, ed_translate(ed.keys_,
                                          (key_press) { 0, VK_F9, 0 })) ? 1 : 0, 1);
        check("  once", f9s, 1);
        ed_destroy(&ed);
    }

    /* --- started with a file --- */
    {
        reset();
        static const char DOC[] = "hello\r\n";
        stub_file_reset();
        stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
        memset(&ed, 0, sizeof(ed));
        check("with a file named it starts too",
              ed_init_for(&ed, 8, "doc.txt", &MINE) != NULL, 1);
        check("  running settings and report", settings_at * 10 + started_at, 12);
        check("  and no banner", banners, 0);
        check("  nor thinking one is up", ed.banner_ ? 1 : 0, 0);
        ed_destroy(&ed);
    }

    /* --- a file named, and empty --- */
    {
        /* Opening an empty file is a different thing from starting with
         * nothing: the file was asked for, so the banner is not shown. */
        reset();
        stub_file_reset();
        stub_file_set_content("", 0);
        memset(&ed, 0, sizeof(ed));
        check("an empty file named starts",
              ed_init_for(&ed, 8, "empty.txt", &MINE) != NULL, 1);
        check("  and shows no banner either", banners, 0);
        ed_destroy(&ed);
    }

    /* --- a program with none of the three --- */
    {
        reset();
        stub_file_reset();
        memset(&ed, 0, sizeof(ed));
        check("a program with no hooks and no keys starts",
              ed_init_for(&ed, 8, NULL, &BARE) != NULL, 1);
        check("  with no banner up", ed.banner_ ? 1 : 0, 0);
        check("  and a key still handled", ed_handle(&ed, ed_translate(ed.keys_,
              (key_press) { 'a', VK_a, 0 })) ? 1 : 0, 1);
        ed_destroy(&ed);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
