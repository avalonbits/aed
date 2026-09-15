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

/* The screen asks for a row's colouring; these tests answer with a fixed set. */
static const tok_run* fixed_runs = NULL;
static int fixed_n = 0;

static int answer_fixed(void* ctx, char ypos, const char* pre, int presz,
                        const char* suf, int sufsz, const tok_run** runs) {
    (void) ctx; (void) ypos; (void) pre; (void) presz; (void) suf; (void) sufsz;
    *runs = fixed_runs;

    return fixed_n;
}

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}


/* stdout capture, so the emitted colour bytes can be read back. */
static long mark;
static void cap_start(void) { fflush(stdout); mark = ftell(stdout); }
static int cap_read(char* buf, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    int n = (int) (end - mark);
    if (n > max) { n = max; }
    FILE* r = fopen("/tmp/aed_theme_capture", "rb");
    if (r == NULL) { return -1; }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

/* The foreground colour in force at each painted column, as a string of digits
 * -- which is what a colour map looks like when every colour is under ten. */
static const char* fg_map(int cols, char start_fg) {
    static char raw[4096];
    const int n = cap_read(raw, sizeof(raw));
    static char out[256];
    int col = 0;
    // Seeded with the colour already in force. A row that wants no change
    // emits none -- that early return is the whole reason a run of columns in
    // one colour costs one write and not eighty -- so a map that started from
    // "unknown" would read a correctly painted plain row as blank.
    char fg = (char) ('0' + (start_fg % 10));
    for (int i = 0; i < n && col < cols && col < (int) sizeof(out) - 1; i++) {
        if (raw[i] == 17 && i + 1 < n) {
            const unsigned char c = (unsigned char) raw[i + 1];
            if (c < 128) { fg = (char) ('0' + (c % 10)); }
            i++;
            continue;
        }
        if (raw[i] == 31 && i + 2 < n) { i += 2; continue; }
        if ((unsigned char) raw[i] < 32) { continue; }
        out[col++] = fg;
    }
    out[col] = 0;

    return out;
}

/* How many colour changes the last capture emitted. One VDU 17 pair is one
 * change, and the count is the thing the feature's cost is measured in -- see
 * test/probes/vducost.c, where each one is about five characters of text. */
static int colour_changes(void) {
    static char raw[4096];
    const int n = cap_read(raw, sizeof(raw));
    int k = 0;
    for (int i = 0; i < n - 1; i++) {
        if (raw[i] == 17 && (unsigned char) raw[i + 1] < 128) {
            k++;
            i++;
        }
    }

    return k;
}

static void check_map(const char* name, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-54s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %s\n%*swant %s\n",
                name, got, 58, "", want);
        failures++;
    }
}

static screen scr;

static int start(void) {
    stub_discard_output();
    memset(&scr, 0, sizeof(scr));

    return scr_init(&scr, 0) != NULL;
}

/* As start(), but leaves stdout where the caller put it -- the painting tests
 * capture it, and stub_discard_output would send it to /dev/null instead. */
static int start_painting(void) {
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
         * settings and saves. What lands in aed.ini has to be their own pair.
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

    /* --- a row painted with token colours --- */
    {
        /*
         * The whole point of the two sections above, seen at the only place it
         * shows: the bytes that reach the VDP. The runs say which columns are
         * which class, the theme says what colour a class is, and highlight()
         * turns the pair into colour changes -- one a run rather than one a
         * column, which is what makes the cost bearable.
         */
        FILE* cap = freopen("/tmp/aed_theme_capture", "w+", stdout);
        if (cap == NULL) {
            fprintf(stderr, "could not capture stdout\n");

            return 1;
        }
        stub_emit_colours(1);
        check("a screen to paint on", start_painting(), 1);
        scr_set_scheme(&scr, 7, 0);

        static theme th;
        theme_clear(&th);
        th.loaded = true;
        th.colour[TOK_TEXT] = 7;
        th.colour[TOK_KEYWORD] = 4;
        th.colour[TOK_COMMENT] = 2;

        /* "int x;  // hi"  ->  keyword, text, comment */
        static const tok_run RUNS[] = {
            { 3,  TOK_KEYWORD },      /* columns 0-2  "int"   */
            { 8,  TOK_TEXT    },      /* columns 3-7  " x;  " */
            { 13, TOK_COMMENT },      /* columns 8-12 "// hi" */
        };

        /* Without a theme, nothing is coloured. */
        scr_set_theme(&scr, NULL);
        fixed_runs = RUNS;
        fixed_n = 3;
        scr_set_colourer(&scr, answer_fixed, NULL, NULL);
        cap_start();
        scr_write_line(&scr, scr.topY_, "int x;  // hi", 13);
        check_map("no theme paints the row in one colour",
                  fg_map(13, 7), "7777777777777");

        /* With it, each run takes its class's colour. */
        scr_set_theme(&scr, &th);
        fixed_runs = RUNS;
        fixed_n = 3;
        scr_set_colourer(&scr, answer_fixed, NULL, NULL);
        cap_start();
        scr_write_line(&scr, scr.topY_, "int x;  // hi", 13);
        check_map("a theme colours each run", fg_map(13, 7), "4447777722222");

        /* A class the theme says nothing about falls back to its text colour,
         * so a partial theme still paints a whole row. */
        static const tok_run ODD[] = { { 13, TOK_OPERATOR } };
        fixed_runs = ODD;
        fixed_n = 1;
        scr_set_colourer(&scr, answer_fixed, NULL, NULL);
        cap_start();
        scr_write_line(&scr, scr.topY_, "int x;  // hi", 13);
        check_map("an uncoloured class falls back to text",
                  fg_map(13, 7), "7777777777777");

        scr_destroy(&scr);
    }

    /* --- a colour change a run, not a column --- */
    {
        /*
         * The cost of this feature is counted in colour changes: each is four
         * bytes and a call, about five characters of text on the wire
         * (test/probes/vducost.c). What keeps that affordable is that a run of
         * columns wanting one colour costs one change, which is the early
         * return in highlight().
         *
         * Without it every column would emit its own, and a row would cost
         * eighty changes rather than three -- the row would still *look*
         * right, which is why the colour map above cannot catch it and this
         * counts instead.
         */
        stub_emit_colours(1);
        check("a screen to count changes on", start_painting(), 1);
        scr_set_scheme(&scr, 7, 0);

        static theme th;
        theme_clear(&th);
        th.loaded = true;
        th.colour[TOK_TEXT] = 7;
        th.colour[TOK_KEYWORD] = 4;
        th.colour[TOK_COMMENT] = 2;
        scr_set_theme(&scr, &th);

        static const tok_run RUNS[] = {
            { 3,  TOK_KEYWORD },
            { 8,  TOK_TEXT    },
            { 13, TOK_COMMENT },
        };
        fixed_runs = RUNS;
        fixed_n = 3;
        scr_set_colourer(&scr, answer_fixed, NULL, NULL);
        cap_start();
        scr_write_line(&scr, scr.topY_, "int x;  // hi", 13);
        const int changes = colour_changes();
        check("  thirteen columns in three runs", changes <= 4 ? 1 : 0, 1);
        check("    and it really is about three", changes >= 2 ? 1 : 0, 1);

        /* And a row wanting nothing but the document's own colour emits none
         * at all, which is what an unhighlighted file must keep costing. */
        scr_set_theme(&scr, NULL);
        scr_set_colourer(&scr, NULL, NULL, NULL);
        cap_start();
        scr_write_line(&scr, scr.topY_, "int x;  // hi", 13);
        check("  a plain row still costs no colour change at all",
              colour_changes(), 0);

        scr_destroy(&scr);
    }

    /* --- the selection still wins --- */
    {
        /*
         * A user who has selected text needs to see what they selected more
         * than they need to see its syntax, so the selection reverses the pair
         * over the top of any token colour. Worth pinning: the token lookup
         * sits in the same function and an `else` in the wrong place would let
         * a keyword paint straight through a highlight.
         */
        stub_emit_colours(1);
        check("a screen with a selection", start_painting(), 1);
        scr_set_scheme(&scr, 7, 0);

        static theme th;
        theme_clear(&th);
        th.loaded = true;
        th.colour[TOK_TEXT] = 7;
        th.colour[TOK_KEYWORD] = 4;
        scr_set_theme(&scr, &th);

        static const tok_run ALL_KW[] = { { 8, TOK_KEYWORD } };
        fixed_runs = ALL_KW;
        fixed_n = 1;
        scr_set_colourer(&scr, answer_fixed, NULL, NULL);
        cap_start();
        /* columns 2..4 selected: those show the reversed pair, bg as fg */
        scr_write_line_sel(&scr, scr.topY_, "abcdefgh", 8, 2, 5);
        check_map("a selection reverses over the token colour",
                  fg_map(8, 7), "44000444");

        scr_destroy(&scr);
    }

    /* --- a theme too big to hold is refused, not half read --- */
    {
        /*
         * The same fault the grammar loader had, in the file beside it. Half a
         * theme colours some classes and leaves the rest on the document's own
         * colour, which looks like a theme somebody wrote with gaps in it
         * rather than a file that did not fit.
         */
        static char big[4096];
        int at = sprintf(big, "[theme]\nname = big\ncovers = 0\n[colours]\n"
                              "comment = 8\n");
        while (at < 3000) {
            at += sprintf(big + at, "# padding to make this file too long\n");
        }
        stub_file_reset();
        stub_file_add("/big.cfg", big, at);

        static theme fat;
        theme_clear(&fat);
        check("a theme longer than the loader can hold is refused",
              theme_load(&fat, "/big.cfg") ? 1 : 0, 0);
        check("  and nothing of it is left behind", fat.loaded ? 1 : 0, 0);

        int fits = sprintf(big, "[theme]\nname = fits\ncovers = 0\n[colours]\n"
                                "comment = 8\n");
        while (fits < 700) {
            fits += sprintf(big + fits, "# padding that still fits\n");
        }
        stub_file_reset();
        stub_file_add("/fits.cfg", big, fits);
        theme_clear(&fat);
        check("one that fits loads", theme_load(&fat, "/fits.cfg") ? 1 : 0, 1);
        check("  with its colours", theme_colour(&fat, TOK_COMMENT), 8);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
