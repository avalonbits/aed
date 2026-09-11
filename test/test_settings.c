/*
 * Host tests for the settings modal and the font picker.
 *
 * The part worth pinning down is what gets written. cfg_update copies through
 * every line it is not told about, so the modal must come out holding only what
 * was actually changed -- setting a value the reader never touched would
 * rewrite a line they had deliberately left alone, and the settings file is
 * meant to be edited by hand.
 *
 * The font picker's other job is arithmetic: a font carries no header, so its
 * height is the file size divided by 256, and what the picker shows has to be
 * worked out the same way AED works it out.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "config.h"
#include "editor.h"
#include "screen.h"
#include "user_input.h"
#include "vkey.h"

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
    FILE* r = fopen("/tmp/aed_set_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);
    /* The stream carries NULs; every search here reads it as text. */
    for (int i = 0; i < n; i++) {
        if (buf[i] == 0) {
            buf[i] = 1;
        }
    }
    buf[n > 0 ? n : 0] = 0;

    return n;
}

/* A card with three fonts and two things that are not. */
static const char* const DIR_NAMES[] = {
    "unscii8.bin", "unscii8x10.bin", "unscii16.bin", "notes.txt", "odd.bin",
};
static const unsigned DIR_SIZES[] = {
    2048, 2560, 4096, 1234, 2000,
};

int main(void) {
    if (freopen("/tmp/aed_set_capture", "w+", stdout) == NULL) {
        return 2;
    }

    static char got[200000];
    screen scr;
    user_input ui;

    /* --- the picker lists fonts, with the geometry it works out itself --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);
        stub_set_dir(DIR_NAMES, DIR_SIZES, 5);

        const stub_key esc[] = { { .ch = 27, .vk = VK_ESCAPE } };
        stub_set_keys(esc, 1);

        config cfg;
        cfg_defaults(&cfg);
        cap_start();
        ui_settings(&ui, &scr, &cfg);   /* draws the settings, not the picker */
        cap_read(got, sizeof(got) - 1);
        check("the settings list every setting", strstr(got, "tab width") != NULL, 1);
        check("  including the font", strstr(got, "font") != NULL, 1);
        /* ctrl_pause_frames is not offered. It is the one setting that can do
         * harm to get wrong, and a row in a list is no place to explain that;
         * it stays in the file for anyone who wants it. */
        check("  and not the ctrl pause", strstr(got, "ctrl") == NULL, 1);
        check("  which the file still carries", strstr(got, "pause") == NULL, 1);
        ui_destroy(&ui);
    }

    /* --- the font picker --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);
        stub_set_dir(DIR_NAMES, DIR_SIZES, 5);

        /* Into the font row, then straight out of the picker. */
        const stub_key open_picker[] = {
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },   /* leave the picker */
            { .ch = 27, .vk = VK_ESCAPE },   /* leave the settings */
        };
        stub_set_keys(open_picker, 5);

        config cfg;
        cfg_defaults(&cfg);
        cap_start();
        ui_settings(&ui, &scr, &cfg);
        cap_read(got, sizeof(got) - 1);

        check("the picker lists a font", strstr(got, "unscii16.bin") != NULL, 1);
        /* 4096 bytes is 16 rows, and 480 pixels of height is 30 of them. All
         * worked out from the file size: the file says nothing about it. */
        check("  with the cell height from its size",
              strstr(got, "8x16") != NULL, 1);
        check("  and the screen it gives", strstr(got, "80x30") != NULL, 1);
        check("  a 2560-byte font is ten rows", strstr(got, "8x10") != NULL, 1);
        /* Not a whole number of 256-byte rows, so not a font. */
        check("  and what is not a font is left out",
              strstr(got, "odd.bin") == NULL, 1);
        check("  as is what is not one at all",
              strstr(got, "notes.txt") == NULL, 1);
        check("  with a way to want none of them",
              strstr(got, "(none") != NULL, 1);
        ui_destroy(&ui);
    }

    /* --- leaving without changing anything writes nothing --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);
        stub_set_dir(DIR_NAMES, DIR_SIZES, 5);

        const stub_key esc[] = { { .ch = 27, .vk = VK_ESCAPE } };
        stub_set_keys(esc, 1);

        config cfg;
        cfg_defaults(&cfg);
        cfg.tab_size = 8;               /* as if read from the file */
        check("nothing changed, so nothing to write",
              ui_settings(&ui, &scr, &cfg), CANCEL_OPT);
        /* And it came out holding nothing, so a write could not have rewritten
         * a line the reader had left alone. */
        check("  and no setting is left set", cfg.tab_size, -1);
        check("  nor the colours", cfg.fg, -1);
        ui_destroy(&ui);
    }

    /* --- changing the tab width --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);
        stub_set_dir(DIR_NAMES, DIR_SIZES, 5);

        /* RETURN on the tab row, clear the prefill, type 8, accept, close. */
        const stub_key settab[] = {
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 0,  .vk = VK_BACKSPACE },
            { .ch = '8', .vk = VK_8 },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(settab, 5);

        config cfg;
        cfg_defaults(&cfg);
        check("changing something is worth writing",
              ui_settings(&ui, &scr, &cfg), YES_OPT);
        check("  and the new width is what comes out", cfg.tab_size, 8);
        check("  with everything else left unset", cfg.fg, -1);
        check("  and the screen using it now", scr_tab_size(&scr), 8);
        ui_destroy(&ui);
    }

    /* --- picking colours repaints the whole screen, not just the list --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);
        stub_set_dir(DIR_NAMES, DIR_SIZES, 5);

        /* Down to the colours, into the picker, change one, accept, close. */
        const stub_key pick[] = {
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 0,  .vk = VK_UP },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(pick, 5);

        config cfg;
        cfg_defaults(&cfg);
        cap_start();
        check("picking colours is worth writing",
              ui_settings(&ui, &scr, &cfg), YES_OPT);
        cap_read(got, sizeof(got) - 1);

        /* The title bar is not one of the rows this modal draws on, so it kept
         * whatever colours it had and the new scheme looked as though it had
         * reached only the middle of the screen. Repainting puts the title
         * back on the wire, which is what to look for. */
        check("  and the title bar is repainted with them",
              strstr(got, "Another Text Editor") != NULL, 1);
        ui_destroy(&ui);
    }

    /* --- a chosen font is written down, and takes effect at once --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);
        stub_set_dir(DIR_NAMES, DIR_SIZES, 5);

        /* Down to the font row, into the picker, down to unscii16, take it,
         * then close. */
        const stub_key pickfont[] = {
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(pickfont, 8);

        config cfg;
        cfg_defaults(&cfg);
        check("picking a font is worth writing",
              ui_settings(&ui, &scr, &cfg), YES_OPT);
        check("  and the path comes out",
              strcmp(cfg.font, "/config/aed/unscii16.bin"), 0);

        /* And it survives the writer. #106's writers skipped string settings
         * entirely, so a font could be picked and was then silently dropped. */
        static const char before[] = "[editor]\r\ntab = 4\r\n";
        stub_file_reset();
        stub_file_set_content(before, (int) sizeof(before) - 1);
        cfg_update(&cfg, "/config/aed.cfg");
        check("  and reaches the file",
              strstr(stub_file_bytes(), "font = /config/aed/unscii16.bin") != NULL, 1);
        ui_destroy(&ui);
    }

    /* --- choosing "none" in the picker --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        ui_init(&ui, 256, scr.bottomY_, scr.cols_);
        stub_set_dir(DIR_NAMES, DIR_SIZES, 5);

        /* To the font row, into the picker, and take the first entry, which is
         * "none". The picker opens on it, so no motion is needed. */
        const stub_key nofont[] = {
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(nofont, 5);

        config cfg;
        cfg_defaults(&cfg);
        strcpy(cfg.font, "/config/aed/unscii16.bin");   /* as the file has it */
        cap_start();
        check("choosing none is a change worth writing",
              ui_settings(&ui, &scr, &cfg), YES_OPT);
        cap_read(got, sizeof(got) - 1);
        check("  recorded as wanting none", cfg.font_none, 1);
        check("  and not as a path", cfg.font[0], 0);

        /* And the row says so. It has three states, not two -- chosen, none,
         * and nothing said -- and reading only the path conflated "none" with
         * "nothing said", so the row went on showing the old font and choosing
         * none looked as though it had done nothing at all. */
        {
            // The last time the row was drawn is the one that matters: the
            // list is redrawn on every pass, so the stream holds the row as it
            // looked before the choice as well as after.
            const char* last = NULL;
            for (const char* q = strstr(got, "font ");
                 q != NULL; q = strstr(q + 1, "font ")) {
                last = q;
            }
            char row[48];
            int rn = 0;
            for (const char* q = last; q != NULL && *q != 0 && rn < 47; q++) {
                row[rn++] = *q;
            }
            row[rn] = 0;
            check("  and the row stops showing the old font",
                  last != NULL && strstr(row, "unscii16") == NULL, 1);
            check("  saying the machine's own instead",
                  last != NULL && strstr(row, "machine") != NULL, 1);
        }
        ui_destroy(&ui);
    }

    /* --- an existing font line is replaced, not duplicated --- */
    {
        static const char had[] =
            "[editor]\r\ntab = 4\r\nfont = /config/aed/old.bin  # keep this\r\n";
        config cfg;
        cfg_defaults(&cfg);
        strcpy(cfg.font, "/config/aed/unscii16.bin");
        stub_file_reset();
        stub_file_set_content(had, (int) sizeof(had) - 1);
        cfg_update(&cfg, "/config/aed.cfg");

        const char* out = stub_file_bytes();
        check("the new font replaces the old one",
              strstr(out, "font = /config/aed/unscii16.bin") != NULL, 1);
        check("  and the old path is gone", strstr(out, "old.bin") == NULL, 1);
        check("  and the comment on the line survives",
              strstr(out, "# keep this") != NULL, 1);
        int n = 0;
        for (const char* q = strstr(out, "font ="); q != NULL; q = strstr(q + 1, "font =")) {
            n++;
        }
        check("  written once, not twice", n, 1);
    }

    /* --- asking for no font is a different answer from saying nothing --- */
    {
        static const char had[] = "[editor]\r\nfont = /config/aed/unscii16.bin\r\n";

        /* Said nothing: the line is left exactly as it was. */
        config quiet;
        cfg_defaults(&quiet);
        stub_file_reset();
        stub_file_set_content(had, (int) sizeof(had) - 1);
        cfg_update(&quiet, "/config/aed.cfg");
        check("saying nothing leaves the font line alone",
              strstr(stub_file_bytes(), "font = /config/aed/unscii16.bin") != NULL, 1);

        /* Asked for none: the line is written out empty, which is how the file
         * says no font -- cfg_parse ignores a setting with no value. */
        config none;
        cfg_defaults(&none);
        none.font_none = true;
        stub_file_reset();
        stub_file_set_content(had, (int) sizeof(had) - 1);
        cfg_update(&none, "/config/aed.cfg");
        check("  but asking for none empties it",
              strstr(stub_file_bytes(), "unscii16") == NULL, 1);
        check("  leaving the setting there, with no value",
              strstr(stub_file_bytes(), "font =") != NULL, 1);

        /* And read back, that is no font. */
        config back;
        cfg_defaults(&back);
        cfg_parse(&back, stub_file_bytes(), (int) strlen(stub_file_bytes()));
        check("  which reads back as no font", back.font[0], 0);
    }

    /* --- the view is where it was when a modal closes --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);

        /* Enough lines to fill the screen and then some, so there is a view to
         * lose in the first place. */
        static char doc[4000];
        int n = 0;
        for (int i = 0; i < 120; i++) {
            n += sprintf(doc + n, "line %d\n", i + 1);
        }
        /* A real 16-row font, so picking it actually changes the geometry.
         * Served by name: the document, the settings and the font are three
         * different files and the font is refused on its size if it is handed
         * the document's bytes. */
        static char font16[256 * 16];
        memset(font16, 0, sizeof(font16));

        stub_file_reset();
        stub_file_add("doc.txt", doc, n);
        stub_file_add("/config/aed/unscii16.bin", font16, (int) sizeof(font16));
        stub_file_set_content(doc, n);
        stub_file_set_objsize((uint32_t) n);

        editor ed;
        check("editor starts", ed_init(&ed, 64, "doc.txt") != NULL, 1);
        stub_set_dir(DIR_NAMES, DIR_SIZES, 5);

        /* Put the cursor a long way down, which is where the view is lost:
         * scr_clear resets the row to the top, and refresh_screen reads that
         * row as how much is on screen above the cursor. */
        for (int i = 0; i < 80; i++) {
            cmd_down(&ed);
        }
        const char row_before = ed.scr_.currY_;
        const int line_before = tb_ypos(&ed.buf_);
        check("the cursor is well down the screen", row_before > 10, 1);

        /* Open the settings and close them again, changing nothing. */
        const stub_key esc[] = { { .ch = 27, .vk = VK_ESCAPE } };
        stub_set_keys(esc, 1);
        cmd_settings(&ed);

        check("  and is still on the same row afterwards",
              ed.scr_.currY_, row_before);
        check("  still on the same line", tb_ypos(&ed.buf_), line_before);

        /* A font change is different: the row count moves, so the old row may
         * not exist any more. The cursor's line is centred instead. */
        const stub_key pickfont[] = {
            { .ch = 0,  .vk = VK_DOWN },      /* colours */
            { .ch = 0,  .vk = VK_DOWN },      /* font */
            { .ch = 13, .vk = VK_RETURN },    /* into the picker */
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 0,  .vk = VK_DOWN },      /* unscii16.bin, 16 rows */
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(pickfont, 8);
        cmd_settings(&ed);

        check("a font change leaves 30 rows", ed.scr_.rows_, 30);
        check("  and centres the cursor's line",
              ed.scr_.currY_,
              (char)(ed.scr_.topY_ + (ed.scr_.bottomY_ - ed.scr_.topY_) / 2));
        check("  which is still the same line", tb_ypos(&ed.buf_), line_before);
        ed_destroy(&ed);
    }

    fflush(stdout);

    return failures == 0 ? 0 : 1;
}
