/*
 * Starting an editor for a program other than AED.
 *
 * ed_init_for takes the program: where its files are, its keys, and its part
 * of starting up -- settings as soon as the screen exists, returning anything
 * to report once the prompts do, and a banner for starting with nothing. These
 * start one for a made-up program and check each part is its own and runs
 * where it should; then two AED editors, one after the other, to show a report
 * belongs to the start that made it.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "aed.h"
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
static int settings_at, banner_at;
static int settings_rows, settings_ui_cols;
static int banners;
static const char* report;

static const char* mine_settings(editor* ed) {
    settings_at = ++order;
    settings_rows = ed->scr_.rows_;
    settings_ui_cols = ed->ui_.cols_;

    return report;
}

static void mine_banner(user_input* ui, screen* scr) {
    (void) ui;
    (void) scr;
    banner_at = ++order;
    banners++;
}

static const ed_program MINE = {
    &MINE_APP, &MINE_KEYS, mine_settings, mine_banner, "MINE: its own title",
};

static const ed_program BARE = { &MINE_APP, NULL, NULL, NULL, NULL };

/* A report waits for a key, so whether one was shown is whether the key
 * scripted here was taken. */
static const stub_key ANY[] = { { 'k', 32, 0, 0 } };

static void reset(void) {
    order = settings_at = banner_at = 0;
    settings_rows = settings_ui_cols = -1;
    banners = 0;
    report = NULL;
    stub_set_keys(ANY, 1);
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
        check("  and its title on the header row",
              ed.scr_.title_ != NULL && strcmp(ed.scr_.title_, "MINE: its own title") == 0
              ? 1 : 0, 1);
        check("its settings run first", settings_at, 1);
        check("  once the screen exists", settings_rows, 25);
        check("  before the prompts do", settings_ui_cols, 0);
        check("with nothing to report, nothing waits for a key",
              stub_keys_read(), 0);
        check("and its banner comes after, with no file named", banner_at, 2);
        check("  once", banners, 1);
        check("  and the editor knows one is up", ed.banner_ ? 1 : 0, 1);
        const key_press f9 = { 0, VK_F9, 0 };
        check("its own key runs its own command",
              ed_handle(&ed, ed_translate(ed.keys_, f9)) ? 1 : 0, 1);
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
        check("  running its settings", settings_at, 1);
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

    /* --- something to report --- */
    {
        reset();
        report = "look at this";
        stub_file_reset();
        memset(&ed, 0, sizeof(ed));
        check("a program with something to report starts",
              ed_init_for(&ed, 8, NULL, &MINE) != NULL, 1);
        check("  and shows it, waiting for a key", stub_keys_read(), 1);
        ed_destroy(&ed);
    }

    /* --- a report belongs to the start that made it --- */
    {
        /* Two AED editors, one after the other: the first asks for a font that
         * is not there, the second asks for none. Only the first may say so. */
        static const char BAD_FONT[] =
            "[editor]\r\nfont = /config/aed/gone.bin\r\n";
        reset();
        stub_file_reset();
        stub_file_add("/config/aed.ini", BAD_FONT, (int) sizeof(BAD_FONT) - 1);
        memset(&ed, 0, sizeof(ed));
        check("AED with a font that will not load starts",
              ed_init(&ed, 8, NULL) != NULL, 1);
        check("  and says so, waiting for a key", stub_keys_read(), 1);
        check("  under AED's own title",
              ed.scr_.title_ != NULL
              && strcmp(ed.scr_.title_, "AED: Another Text Editor") == 0 ? 1 : 0, 1);
        ed_destroy(&ed);

        reset();
        stub_file_reset();
        memset(&ed, 0, sizeof(ed));
        check("AED again, asking for no font, starts",
              ed_init(&ed, 8, NULL) != NULL, 1);
        check("  and reports nothing of the first one's", stub_keys_read(), 0);
        ed_destroy(&ed);
    }

    /* --- a program with neither hook, and no keys --- */
    {
        reset();
        stub_file_reset();
        memset(&ed, 0, sizeof(ed));
        check("a program with no hooks and no keys starts",
              ed_init_for(&ed, 8, NULL, &BARE) != NULL, 1);
        check("  with no banner up", ed.banner_ ? 1 : 0, 0);
        const key_press a = { 'a', VK_a, 0 };
        check("  and a key still handled",
              ed_handle(&ed, ed_translate(ed.keys_, a)) ? 1 : 0, 1);
        ed_destroy(&ed);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
