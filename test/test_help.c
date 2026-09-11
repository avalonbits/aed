/*
 * Host tests for the help screen and the startup banner.
 *
 * Both write over the text area, and the interesting part of each is what
 * happens when they go away. The help screen is closed by the caller putting
 * the document back; the banner has to survive until the first keystroke and
 * then be gone completely, which is not the same as being painted over -- a
 * keystroke repaints one row, and the rest of a banner painted over would sit
 * behind the document.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "screen.h"
#include "user_input.h"
#include "version.h"

static int failures = 0;
static long mark;

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

static int cap_read(char* buf, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    int n = (int)(end - mark);
    if (n > max) {
        n = max;
    }
    FILE* r = fopen("/tmp/aed_help_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    // The stream carries NULs -- a colour of 0 is one, and the cursor character
    // can be -- and every search here treats the capture as text. Left alone,
    // strstr stops at the first of them and everything painted afterwards is
    // invisible to the test, which is a pass that means nothing rather than a
    // failure that says so.
    for (int i = 0; i < n; i++) {
        if (buf[i] == 0) {
            buf[i] = 1;
        }
    }
    buf[n > 0 ? n : 0] = 0;

    return n;
}

/* The last occurrence of `what` in `hay`, or NULL. The stream holds every page
 * that was drawn, so "which came last" is how to ask what is on screen at the
 * end. */
static const char* last_of(const char* hay, const char* what) {
    const char* found = NULL;
    for (const char* p = strstr(hay, what); p != NULL; p = strstr(p + 1, what)) {
        found = p;
    }

    return found;
}

int main(void) {
    if (freopen("/tmp/aed_help_capture", "w+", stdout) == NULL) {
        fprintf(stderr, "could not capture stdout\n");

        return 2;
    }

    static char got[200000];
    screen scr;
    user_input ui;

    /* --- the help screen --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);

        /* One key closes it when everything fits on a 60-row screen. */
        const stub_key close[] = { { .ch = 27, .vk = VK_ESCAPE } };
        stub_set_keys(close, 1);

        cap_start();
        ui_help(&ui, &scr);
        cap_read(got, sizeof(got) - 1);

        check("the version is on the help screen",
              strstr(got, AED_VERSION) != NULL, 1);
        /* A few bindings that must be there, and must say what they do. */
        check("CTRL+S is listed", strstr(got, "- CTRL+S") != NULL, 1);
        check("CTRL+H is listed, so the screen documents itself",
              strstr(got, "CTRL+H") != NULL, 1);
        check("and the sections are headed", strstr(got, "EDITING") != NULL, 1);
        /* Sections are separated by a blank row. The row is painted as a full
         * width of spaces, so what to look for is the run of blanks that ends
         * where the next heading begins -- not an empty write, which would
         * leave whatever was underneath. */
        {
            const char* h = strstr(got, "  EDITING");
            int blanks = 0;
            for (const char* p = h; p != NULL && p > got && *(p - 1) == ' '; p--) {
                blanks++;
            }
            check("  with a blank row before them", blanks >= 40, 1);
        }
        /* CTRL+DELETE deletes a whole line, the same as CTRL+D, and for a long
         * time only CTRL+D was listed. */
        check("the delete-line synonym is listed too",
              strstr(got, "CTRL+D / CTRL+DEL") != NULL, 1);

        /* The descriptions line up in a column, and the longest key is now
         * within two characters of reaching it. A key longer than the column
         * runs straight into its own description with no gap at all, and the
         * only thing that stops it is HELP_GAP being wide enough -- which
         * nothing else here would notice. */
        {
            int worst = 1 << 20;
            static const char* const keys[] = {
                "CTRL+O", "CTRL+ALT+S", "CTRL+LEFT/RIGHT", "HOME / END",
                "PAGE UP/DOWN", "BACKSPACE", "CTRL+D / CTRL+DEL",
                "SHIFT+motion", "CTRL+F", "CTRL+E",
            };
            for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++) {
                const char* at = strstr(got, keys[i]);
                if (at == NULL) {
                    worst = -1;
                    break;
                }
                const char* p = at + strlen(keys[i]);
                int gap = 0;
                while (*p == ' ') {
                    gap++;
                    p++;
                }
                if (gap < worst) {
                    worst = gap;
                }
            }
            check("  and every key clears its description by two spaces",
                  worst >= 2, 1);
        }

        check("it closed on one key", stub_keys_read(), 1);
        ui_destroy(&ui);
    }

    /* --- it pages when the list does not fit --- */
    {
        /* Sixteen-row font: thirty rows, which the list outgrows. */
        stub_set_screen(80, 30);
        stub_set_cell(8, 16);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);

        const stub_key page[] = {
            { .ch = ' ', .vk = VK_SPACE },
            { .ch = ' ', .vk = VK_SPACE },
            { .ch = ' ', .vk = VK_SPACE },
            { .ch = 27,  .vk = VK_ESCAPE },
        };
        stub_set_keys(page, 4);

        cap_start();
        ui_help(&ui, &scr);
        const int n = cap_read(got, sizeof(got) - 1);

        check("a short screen offers more", strstr(got, "for more") != NULL, 1);
        /* SPACE moved on rather than closing: more than one key was consumed. */
        check("SPACE pages instead of closing", stub_keys_read() > 1, 1);
        /* And paging reached the end: the last section is on screen by then. */
        check("paging reaches the last entry", strstr(got, "SETTINGS") != NULL, 1);
        (void) n;
        ui_destroy(&ui);
    }

    /* --- paging back up --- */
    {
        stub_set_screen(80, 30);
        stub_set_cell(8, 16);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);

        /* Forward twice, then back twice, then close. Ending up on page one
         * means the first section is on screen again at the end. */
        const stub_key updown[] = {
            { .ch = ' ', .vk = VK_SPACE },
            { .ch = ' ', .vk = VK_SPACE },
            { .ch = 0,   .vk = VK_UP },
            { .ch = 0,   .vk = VK_UP },
            { .ch = 27,  .vk = VK_ESCAPE },
        };
        stub_set_keys(updown, 5);

        cap_start();
        ui_help(&ui, &scr);
        cap_read(got, sizeof(got) - 1);

        check("every key was used, so UP paged rather than closing",
              stub_keys_read(), 5);
        /* The last thing drawn is page one, so the last FILE on the stream is
         * after the last FINDING -- which only holds if UP went back. */
        {
            const char* last_file = last_of(got, "  FILE");
            const char* last_find = last_of(got, "  FINDING");
            check("  and ended back on the first page",
                  last_file != NULL && last_find != NULL && last_file > last_find, 1);
        }
        ui_destroy(&ui);
    }

    /* --- a paging key with nowhere to go does not close --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);

        /* Everything fits, so DOWN and UP have nowhere to go. Neither should
         * drop out of the help; only the ESC should. */
        const stub_key stuck[] = {
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 0,  .vk = VK_UP },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(stuck, 3);
        ui_help(&ui, &scr);
        check("paging with nowhere to go stays put", stub_keys_read(), 3);
        ui_destroy(&ui);
    }

    /* --- a font named in the settings file that will not load --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);

        /* The stub serves the same bytes to every open, so the font open gets
         * the settings text back -- which is not a whole number of 256-byte
         * rows and is refused for its size. A font that is not there is refused
         * one step earlier, at the open, and reaches the same place: the
         * message says only that it did not load, because from here the reasons
         * are not worth telling apart. */
        static const char cfg[] = "[editor]\r\nfont = /config/aed/gone.bin\r\n";
        stub_file_reset();
        stub_file_set_content(cfg, (int) sizeof(cfg) - 1);
        stub_file_set_objsize((uint32_t)(sizeof(cfg) - 1));
        const stub_key any[] = { { .ch = 27, .vk = VK_ESCAPE } };
        stub_set_keys(any, 1);

        cap_start();
        editor missing;
        check("the editor still starts", ed_init(&missing, 8, NULL) != NULL, 1);
        cap_read(got, sizeof(got) - 1);

        check("  in the stock font", missing.scr_.fontLoaded_, 0);
        check("  and says which font it could not load",
              strstr(got, "font not loaded: /config/aed/gone.bin") != NULL, 1);
        ed_destroy(&missing);

        /* The longest path the settings reader will hold. The message is built
         * by hand -- snprintf is 4,994 bytes of nanoprintf for one %s, which is
         * eight per cent of the binary -- so the end of that buffer is worth a
         * test of its own. ASan is what makes this one bite. */
        static char longcfg[CFG_FONT_MAX + 32];
        int at = 0;
        static const char head[] = "[editor]\r\nfont = ";
        memcpy(longcfg + at, head, sizeof(head) - 1);
        at += (int) sizeof(head) - 1;
        for (int i = 0; i < CFG_FONT_MAX - 1; i++) {
            longcfg[at++] = (char) ('a' + (i % 26));
        }
        longcfg[at++] = '\r';
        longcfg[at++] = '\n';
        stub_file_reset();
        stub_file_set_content(longcfg, at);
        stub_file_set_objsize((uint32_t) at);
        stub_set_keys(any, 1);

        cap_start();
        editor longest;
        check("a font path that fills the setting still starts",
              ed_init(&longest, 8, NULL) != NULL, 1);
        cap_read(got, sizeof(got) - 1);
        check("  and the whole of it is in the message",
              strstr(got, "font not loaded: abcdefghij") != NULL, 1);
        ed_destroy(&longest);
    }

    /* --- a file that will not load still hands the machine back --- */
    /* ed_init sets the screen up first: it measures the colours the machine was
     * using, puts the user's scheme on, and may load a font. Every way it could
     * fail after that returned without scr_destroy, so naming a file too large
     * for memory left the user at the prompt in AED's colours and AED's font.
     *
     * The message has to survive that too. It used to be printed from inside
     * tb_load, before the screen was handed back -- and handing it back clears
     * it, so restoring the colours would have wiped the one thing that said
     * why. It is said after the restore now. */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        stub_set_screen_colours(7, 1);

        /* A settings file asking for colours of its own, so what the editor
         * uses and what it must give back are different. */
        static const char scheme[] = "[colours]\r\nfg = 3\r\nbg = 4\r\n";
        stub_file_reset();
        stub_file_set_content(scheme, (int) sizeof(scheme) - 1);

        text_buffer probe;
        if (tb_init(&probe, 8, NULL) == NULL) {
            fprintf(stderr, "probe init failed\n");

            return 2;
        }
        const uint32_t toobig = (uint32_t) tb_size(&probe) + 64;
        tb_destroy(&probe);

        stub_file_set_objsize(toobig);
        stub_colours_reset();
        cap_start();
        editor big;
        check("a file too large is refused", ed_init(&big, 8, "big.asm") == NULL, 1);
        cap_read(got, sizeof(got) - 1);
        check("  and the machine gets its foreground back", stub_last_fg(), 7);
        check("  and its background", stub_last_bg(), 1);
        /* After the clear, not before it. On the real screen scr_destroy's
         * VDU 12 wipes everything written up to that point, so a message
         * printed first is destroyed by the very act of putting the colours
         * right -- and in the capture, where nothing is really erased, only the
         * order gives that away. */
        const char* said = strstr(got, "file too large");
        const char* cleared = strrchr(got, 12);
        check("  and is told why", said != NULL, 1);
        check("  after the screen is handed back, not before",
              said != NULL && cleared != NULL && said > cleared, 1);
        stub_file_set_objsize(0);
        stub_set_screen_colours(15, 0);
    }

    /* --- the banner --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);

        cap_start();
        ui_banner(&ui, &scr);
        cap_read(got, sizeof(got) - 1);

        check("the banner names the version", strstr(got, "AED " AED_VERSION) != NULL, 1);
        check("and points at the help", strstr(got, "CTRL+H") != NULL, 1);

        /* Framed with + - | rather than box-drawing characters: a font carries
         * 256 glyphs and the box-drawing ones live well above that, so what
         * they land on differs from font to font. */
        check("  and is framed", strstr(got, "+---") != NULL, 1);
        {
            // And centred across the width, not flush against the left. The
            // padding is inside the painted row rather than in the tab, so it
            // is the run of spaces before the corner that says where the box
            // starts.
            const char* corner = strstr(got, "+---");
            int before = 0;
            for (const char* q = corner; q != NULL && q > got && *(q - 1) == ' '; q--) {
                before++;
            }
            check("  and centred across the width", before > 5, 1);
        }
        check("  with sides", strstr(got, "|") != NULL, 1);

        /* Centred in the text area, not parked at the top. With the tabs in
         * the stream the rows it painted can be read off directly: the middle
         * of the box should sit at the middle of the area. */
        {
            stub_emit_tabs(1);
            scr_init(&scr, 32);
            cap_start();
            ui_banner(&ui, &scr);
            const int m = cap_read(got, sizeof(got) - 1);
            stub_emit_tabs(0);

            // Every painted row is followed by the cursor being tabbed back to
            // where it was, so the stream is full of tabs to the cursor's own
            // row. Those are not the banner and would drag the range down to
            // the top of the area.
            int lo = 999;
            int hi = -1;
            for (int i = 0; i + 2 < m; i++) {
                if ((unsigned char) got[i] != 31) {
                    continue;
                }
                const int y = (unsigned char) got[i + 2];
                i += 2;
                if (y == scr.currY_) {
                    continue;
                }
                if (y < lo) { lo = y; }
                if (y > hi) { hi = y; }
            }
            const int mid = (lo + hi) / 2;
            const int want = (scr.topY_ + scr.bottomY_) / 2;
            /* Within a row: the box is an even number of rows tall, so its
             * middle can land either side of the area's. */
            check("  and centred in the text area", mid >= want - 1 && mid <= want + 1, 1);
        }
        ui_destroy(&ui);
    }

    /* --- the banner is put up only when no file was named, and goes on the
     * first key --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);

        static const char doc[] = "hello\n";
        stub_file_reset();
        stub_file_set_content(doc, (int) sizeof(doc) - 1);
        stub_file_set_objsize((uint32_t)(sizeof(doc) - 1));

        editor named;
        check("editor starts with a file", ed_init(&named, 8, "doc.txt") != NULL, 1);
        check("  and puts up no banner", named.banner_, 0);

        /* A named file that happens to be empty is not the same as no file:
         * whoever named one has already said what they came to do, so the
         * banner would just be in the way. This is the only case the fname
         * check decides -- with a file that has anything in it, the document is
         * drawn and the question never comes up. */
        stub_file_reset();
        stub_file_set_content("", 0);
        stub_file_set_objsize(0);
        editor blank;
        check("editor starts with a named but empty file",
              ed_init(&blank, 8, "blank.txt") != NULL, 1);
        check("  and still puts up no banner", blank.banner_, 0);
        ed_destroy(&blank);

        stub_file_reset();
        editor empty;
        check("editor starts with no file", ed_init(&empty, 8, NULL) != NULL, 1);
        check("  and puts one up", empty.banner_, 1);

        /* The first key takes it down, and takes down all of it: the text area
         * is cleared rather than painted over. */
        cap_start();
        ed_clear_banner(&empty);
        const int n = cap_read(got, sizeof(got) - 1);
        check("  which the first key takes down", empty.banner_, 0);
        /* A viewport over the text area and a clear inside it is how the area
         * is emptied; anything less would leave the banner behind. */
        check("  by clearing the area, not painting over it",
              memchr(got, 28, (size_t) n) != NULL && memchr(got, 12, (size_t) n) != NULL, 1);

        /* And it is a no-op after that, so every later keystroke is not paying
         * for a screen clear. */
        cap_start();
        ed_clear_banner(&empty);
        check("  and does nothing on the next key", cap_read(got, sizeof(got) - 1), 0);

        /* And the cursor is put back after the area is cleared.
         *
         * Clearing resets the viewport, and VDU 26 homes the text cursor as a
         * side effect. scr_show_cursor_ch draws wherever the cursor is rather
         * than tabbing first, so the block landed on row 0 and took the title
         * bar's dash with it.
         *
         * Asserted on the stream rather than on the stub's idea of where the
         * cursor is: the stub only sees vdp_cursor_tab, so it never learns that
         * a VDU 26 moved the cursor and its answer stayed stale and correct
         * whether or not the fix was there. */
        {
            stub_emit_tabs(1);
            cap_start();
            scr_clear_textarea(&empty.scr_, empty.scr_.topY_,
                               (char) (empty.scr_.bottomY_ - 1));
            const int m = cap_read(got, sizeof(got) - 1);
            stub_emit_tabs(0);

            int reset = -1;
            for (int i = 0; i < m; i++) {
                if ((unsigned char) got[i] == 26) {
                    reset = i;
                }
            }
            int tabbed = 0;
            for (int i = reset; i >= 0 && i + 2 < m; i++) {
                if ((unsigned char) got[i] == 31) {
                    tabbed = (unsigned char) got[i + 2];
                    break;
                }
            }
            check("the viewport reset is followed by a tab", tabbed > 0, 1);
            check("  back to the document, not the title bar",
                  tabbed, empty.scr_.currY_);
        }
        ed_destroy(&empty);
        ed_destroy(&named);
    }

    fflush(stdout);

    return failures == 0 ? 0 : 1;
}
