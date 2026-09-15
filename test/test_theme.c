/*
 * The colours the user chose, and the colours a theme shows.
 *
 * Syntax highlighting is themed, and a theme may want a background the user did
 * not pick. What it must not do is *become* the user's setting: open a file the
 * theme does not cover, or write the settings out, and the pair the user chose
 * has to be the one that comes back.
 *
 * So there are two pairs. fg_/bg_ is what paints, and a theme may move it.
 * baseFg_/baseBg_ is what the user chose, and only the user moves it -- through
 * the colour picker or the settings file. The settings file records the base,
 * so a theme in force while settings are written cannot leak into them.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "screen.h"
#include "editor.h"
#include "config.h"
#include "syntax.h"
#include "user_input.h"
#include "keys.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static screen scr;

static int start(void) {
    stub_discard_output();
    memset(&scr, 0, sizeof(scr));

    return scr_init(&scr, 0) != NULL;
}

int main(void) {
    stub_discard_output();

    /* --- the user's choice moves both pairs --- */
    {
        check("a screen", start(), 1);
        scr_set_scheme(&scr, 7, 1);
        check("  the user picks 7 on 1: it paints with that", scr_fg(&scr), 7);
        check("    and on that background", scr_bg(&scr), 1);
        check("  and it is what the user is set to", scr_base_fg(&scr), 7);
        check("    on that background", scr_base_bg(&scr), 1);
        scr_destroy(&scr);
    }

    /* --- a theme moves only what paints --- */
    {
        check("a screen with the user's colours", start(), 1);
        scr_set_scheme(&scr, 7, 1);

        scr_theme_scheme(&scr, 11, 4);
        check("  a theme takes over the painting", scr_fg(&scr), 11);
        check("    and the background with it", scr_bg(&scr), 4);
        check("  but the user's choice is untouched", scr_base_fg(&scr), 7);
        check("    on the background they picked", scr_base_bg(&scr), 1);

        scr_base_restore(&scr);
        check("  and it goes back when nothing is theming", scr_fg(&scr), 7);
        check("    background too", scr_bg(&scr), 1);
        check("  with the base still the base", scr_base_fg(&scr), 7);
        scr_destroy(&scr);
    }

    /* --- a theme cannot leak into the settings file --- */
    {
        /*
         * The path that matters: a file with a theme is open, the user opens
         * settings and saves. What lands in aed.cfg has to be their own pair.
         * Written against scr_base_fg because that is what editor.c and
         * user_input.c now read; if either went back to scr_fg, a session that
         * had ever opened a .c file would rewrite the user's colours.
         */
        check("a screen to save from", start(), 1);
        scr_set_scheme(&scr, 15, 0);
        scr_theme_scheme(&scr, 3, 6);

        check("  the theme is what is painting", scr_fg(&scr), 3);
        check("  but what would be written is the user's",
              scr_base_fg(&scr), 15);
        check("    and their background", scr_base_bg(&scr), 0);
        scr_destroy(&scr);
    }

    /* --- a theme asking for a colour the mode has not got --- */
    {
        check("a screen with a known mode", start(), 1);
        scr_set_scheme(&scr, 7, 1);
        const char colors = scr.colors_;

        scr_theme_scheme(&scr, (char) (colors + 4), 0);
        check("  a theme past the palette is refused", scr_fg(&scr), 7);
        check("    leaving the background alone too", scr_bg(&scr), 1);
        check("    and the base untouched", scr_base_fg(&scr), 7);
        scr_destroy(&scr);
    }

    /* --- restoring when nothing ever themed --- */
    {
        check("a screen nothing has themed", start(), 1);
        scr_set_scheme(&scr, 5, 2);
        scr_base_restore(&scr);
        check("  restoring is harmless", scr_fg(&scr), 5);
        check("    for the background as well", scr_bg(&scr), 2);
        scr_destroy(&scr);
    }

    /* --- the colour picker, opened while a theme is in force --- */
    {
        /*
         * Through the keys, because that is the only way this one shows up.
         * The picker used to start from what was painting. With a theme in
         * force that is the theme's pair, so opening the picker and pressing
         * RETURN -- accepting what it offered, without moving a key -- wrote
         * the theme's colours in as the user's own. A theme that can become
         * the setting is the failure this whole pair of variables exists to
         * prevent, and asserting on scr_base_fg directly walks straight past
         * it: the picker is what does the damage.
         */
        static user_input ui;
        check("a screen and a prompt", start(), 1);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);

        scr_set_scheme(&scr, 15, 0);        /* the user's choice */
        scr_theme_scheme(&scr, 3, 6);       /* a theme takes over */
        check("  the theme is painting", scr_fg(&scr), 3);

        /* Open the picker and accept immediately. */
        const stub_key accept[] = { { .ch = 13, .vk = VK_RETURN } };
        stub_set_keys(accept, 1);
        check("  the picker accepts", ui_color_picker(&ui, &scr) == YES_OPT, 1);

        check("    and the user still has the colours they picked",
              scr_base_fg(&scr), 15);
        check("      and their background", scr_base_bg(&scr), 0);
        scr_destroy(&scr);
    }

    /* --- a scope name lands on a class --- */
    {
        /*
         * A grammar names TextMate scopes and there are hundreds of them, so a
         * scope's first component decides its class: keyword.control.c and
         * keyword.other are both keywords without either being listed. What
         * makes that safe is the fallback -- a scope nobody anticipated is
         * ordinary text rather than a load failure.
         */
        struct { const char* scope; tok_class want; } CASES[] = {
            { "comment.line.double-slash",   TOK_COMMENT  },
            { "comment.block",               TOK_COMMENT  },
            { "string.quoted.double",        TOK_STRING   },
            { "constant.numeric.hex",        TOK_NUMBER   },
            { "keyword.control",             TOK_KEYWORD  },
            { "keyword.other.whatever",      TOK_KEYWORD  },
            { "storage.type",                TOK_TYPE     },
            { "entity.name.label",           TOK_LABEL    },
            { "punctuation.separator",       TOK_OPERATOR },
            { "keyword.control.preprocessor", TOK_PREPROC },
            { "meta.function.nobody.knows",  TOK_TEXT     },
            { "",                            TOK_TEXT     },
        };
        int wrong = 0;
        for (int i = 0; i < 12; i++) {
            if (syn_class_of(CASES[i].scope, (int) strlen(CASES[i].scope))
                    != CASES[i].want) {
                wrong = i + 1;
            }
        }
        check("every scope lands where it should", wrong, 0);
        check("  and a null one is text",
              syn_class_of(NULL, 0), TOK_TEXT);
        check("  the preprocessor beats the keyword rule it would match",
              syn_class_of("keyword.control.preprocessor", 28), TOK_PREPROC);
    }

    /* --- reading a theme --- */
    {
        static const char DARK[] =
            "# a theme for dark backgrounds\n"
            "[theme]\n"
            "name   = dark\n"
            "covers = 0 1 4 5\n"
            "fg     = 15\n"
            "bg     = 0\n"
            "\n"
            "[colours]\n"
            "text     = 15\n"
            "comment  = 8    ; dim\n"
            "string   = 10\n"
            "keyword  = 14\n"
            "number   = 13\n";
        stub_file_reset();
        stub_file_add("/t/dark.cfg", DARK, (int) sizeof(DARK) - 1);

        static theme th;
        theme_clear(&th);
        check("a theme file loads", theme_load(&th, "/t/dark.cfg") ? 1 : 0, 1);
        check("  with its name", strcmp(th.name, "dark") == 0 ? 1 : 0, 1);
        check("  the backgrounds it is for", th.ncovers, 4);
        check("    and it says so", theme_covers(&th, 4) ? 1 : 0, 1);
        check("    and says no to the others", theme_covers(&th, 7) ? 1 : 0, 0);
        check("  the pair it wants", th.fg, 15);
        check("    on that background", th.bg, 0);

        check("  comments are dim", theme_colour(&th, TOK_COMMENT), 8);
        check("  strings are green", theme_colour(&th, TOK_STRING), 10);
        check("  keywords stand out", theme_colour(&th, TOK_KEYWORD), 14);
        check("  a class the theme says nothing about falls back to text",
              theme_colour(&th, TOK_OPERATOR), 15);
    }

    /* --- a theme that will not read leaves the last one alone --- */
    {
        static const char GOOD[] =
            "[theme]\nname = good\ncovers = 2\n[colours]\ntext = 7\n";
        stub_file_reset();
        stub_file_add("/t/good.cfg", GOOD, (int) sizeof(GOOD) - 1);

        static theme th;
        theme_clear(&th);
        check("a theme loads", theme_load(&th, "/t/good.cfg") ? 1 : 0, 1);
        check("  named", strcmp(th.name, "good") == 0 ? 1 : 0, 1);

        check("  a theme that is not there is refused",
              theme_load(&th, "/t/missing.cfg") ? 1 : 0, 0);
        check("    and the one already loaded is untouched",
              strcmp(th.name, "good") == 0 ? 1 : 0, 1);
        check("      colours and all", theme_colour(&th, TOK_TEXT), 7);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
