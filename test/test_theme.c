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

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
