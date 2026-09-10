/*
 * Host tests for loading a font.
 *
 * A font is a raw bitmap with no header: 256 glyphs, 8 pixels wide, one byte
 * per row, and the height is whatever the file size divided by 256 says it is.
 * That is what lets a font of any height be dropped in -- including one padded
 * with a blank row so the text lines do not touch -- and it is also why the
 * validation here matters. There is no magic number to reject a wrong file on,
 * only the size.
 *
 * The sequence sent to the VDP is pinned byte for byte because none of it can
 * be checked at run time: MOS cannot report the VDP version, so AED sends the
 * font on the user's say-so and never learns whether it landed. The one thing
 * it must never do is send a partial upload -- the VDP's buffer write reads a
 * byte count off the stream, so a short one leaves it consuming the editor's
 * first screenful as glyph data.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "config.h"
#include "screen.h"

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

static int cap_read(unsigned char* buf, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    int n = (int)(end - mark);
    if (n > max) {
        n = max;
    }
    FILE* r = fopen("/tmp/aed_font_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

/* Where `want` first appears in `got`, or -1. */
static int find_seq(const unsigned char* got, int gotsz,
                    const unsigned char* want, int wantsz) {
    for (int i = 0; i + wantsz <= gotsz; i++) {
        if (memcmp(got + i, want, (size_t) wantsz) == 0) {
            return i;
        }
    }

    return -1;
}

static void check_has(const char* name, const unsigned char* got, int gotsz,
                      const unsigned char* want, int wantsz) {
    const int at = find_seq(got, gotsz, want, wantsz);
    if (at >= 0) {
        fprintf(stderr, "PASS  %-54s at %d\n", name, at);
    } else {
        fprintf(stderr, "FAIL  %-54s not in %d bytes, wanted", name, gotsz);
        for (int i = 0; i < wantsz; i++) {
            fprintf(stderr, " %d", want[i]);
        }
        fprintf(stderr, "\n");
        failures++;
    }
}

/* A font of `height` rows whose capitals reach `capbottom` and nothing lower,
 * so the ascent the loader measures is a value the height alone cannot give. */
static char fontbuf[256 * 32];

static int make_font(int height, int capbottom) {
    const int size = 256 * height;
    memset(fontbuf, 0, (size_t) size);
    for (int c = 'A'; c <= 'Z'; c++) {
        for (int r = 0; r <= capbottom; r++) {
            fontbuf[c * height + r] = (char) 0xFF;
        }
    }
    /* Lowercase with a descender one row below the capitals, to prove the
     * ascent comes from the capitals and not from the lowest ink anywhere. */
    if (capbottom + 1 < height) {
        fontbuf['p' * height + capbottom + 1] = (char) 0xFF;
    }

    return size;
}

static void install_font(int height, int capbottom) {
    const int size = make_font(height, capbottom);
    stub_file_reset();
    stub_file_set_content(fontbuf, size);
    stub_file_set_objsize((uint32_t) size);
}

/* A file the loader is expected to reject on its size alone, without reading
 * it. The content is deliberately far shorter than the size claimed: if the
 * rejection ever moves to after the read, this stops matching and says so.
 */
static void install_oversize(uint32_t size) {
    memset(fontbuf, 0, 256);
    stub_file_reset();
    stub_file_set_content(fontbuf, 256);
    stub_file_set_objsize(size);
}

/* A file of `size` bytes that is not necessarily a whole number of glyph rows. */
static void install_raw(int size) {
    memset(fontbuf, 0, (size_t) (size > 0 ? size : 1));
    stub_file_reset();
    stub_file_set_content(fontbuf, size);
    stub_file_set_objsize((uint32_t) size);
}

int main(void) {
    if (freopen("/tmp/aed_font_capture", "w+", stdout) == NULL) {
        fprintf(stderr, "could not capture stdout\n");

        return 2;
    }

    unsigned char got[16384];
    screen scr;

    /* --- a well-formed 8x9 font is uploaded and selected --- */
    stub_set_screen(80, 60);
    stub_set_cell(8, 8);
    scr_init(&scr, 32);

    install_font(9, 6);
    cap_start();
    const bool ok = scr_load_font(&scr, "/config/aed/f.bin");
    int n = cap_read(got, sizeof(got));

    check("a well-formed font loads", ok, 1);

    /* VDU 23,0,&A0,bufferId;2 -- clear whatever was in the buffer first. */
    const unsigned char clear[] = {23, 0, 0xA0, 0xED, 0x0A, 2};
    check_has("the buffer is cleared before the upload", got, n, clear, sizeof(clear));

    /* VDU 23,0,&A0,bufferId;0,length; -- 2304 is 0x0900. */
    const unsigned char write[] = {23, 0, 0xA0, 0xED, 0x0A, 0, 0x00, 0x09};
    check_has("the upload declares the whole file length", got, n, write, sizeof(write));

    /* VDU 23,0,&95,1,bufferId;width,height,ascent,flags. Height 9 comes from
     * the file size; ascent 7 is measured, being one past the lowest row the
     * capitals reach. Neither is derivable from the other: a 9-row font padded
     * from an 8-row one has its baseline where the 8-row font had it. */
    const unsigned char create[] = {23, 0, 0x95, 1, 0xED, 0x0A, 8, 9, 7, 0};
    check_has("the font is created at the height the file implies",
              got, n, create, sizeof(create));

    const unsigned char select[] = {23, 0, 0x95, 0, 0xED, 0x0A, 0};
    check_has("and then selected", got, n, select, sizeof(select));

    /* The glyph bytes have to be all there, and *exactly* there: the VDP is
     * counting 2304 bytes off the stream and will take any shortfall out of
     * whatever AED sends next, which is the editor's first screenful.
     *
     * Measured as the gap between the end of the write header and the start of
     * the create command, because that is the only thing that pins the count
     * down. "at least 2304 bytes followed" is nearly free -- the rest of the
     * stream is in it -- and would pass on a badly truncated upload. */
    {
        const int hat = find_seq(got, n, write, sizeof(write));
        const int cat = find_seq(got, n, create, sizeof(create));
        const int glyphs = (hat >= 0 && cat >= 0) ? cat - (hat + (int) sizeof(write)) : -1;
        check("exactly the declared number of glyph bytes is sent", glyphs, 2304);
    }

    /* --- the ascent is measured, not assumed --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        install_font(16, 13);
        cap_start();
        scr_load_font(&scr, "/f.bin");
        n = cap_read(got, sizeof(got));
        const unsigned char c16[] = {23, 0, 0x95, 1, 0xED, 0x0A, 8, 16, 14, 0};
        check_has("a 16-row font's ascent follows its capitals",
                  got, n, c16, sizeof(c16));
    }

    /* --- the geometry moves together with the font --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        check("before: rows", scr.rows_, 60);
        check("before: cell height", scr.charH_, 8);

        install_font(9, 6);
        /* No faking the result: the stub applies the font itself, the way the
         * VDP does, so 480 pixels of height over 9-row cells becomes 53 rows
         * without this test saying so. */
        scr_load_font(&scr, "/f.bin");

        check("after: rows", scr.rows_, 53);
        check("after: the last row moved with them", scr.bottomY_, 52);
        check("after: cell height, which VDU 23,7 scrolls by", scr.charH_, 9);
        check("after: the width is untouched", scr.cols_, 78);
        check("after: and so is the bar", scr.barW_, 79);
    }

    /* --- the footer cache does not survive the row count changing --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        scr_footer(&scr, "a.txt", false, 0, 1);
        check("the footer was drawn", scr.footerDrawn_, 1);

        install_font(9, 6);
        scr_load_font(&scr, "/f.bin");
        check("and is forgotten, its row having moved", scr.footerDrawn_, 0);
    }

    /* --- rejections change nothing and send nothing --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);

        struct { const char* why; int size; } bad[] = {
            { "a size that is not a whole number of glyph rows", 2300 },
            { "an empty file",                                   0    },
        };
        for (int i = 0; i < 2; i++) {
            scr_init(&scr, 32);
            install_raw(bad[i].size);
            cap_start();
            const bool loaded = scr_load_font(&scr, "/f.bin");
            n = cap_read(got, sizeof(got));
            check(bad[i].why, loaded, 0);
            check("  and nothing was sent", n, 0);
            check("  and no font to put back", scr.fontLoaded_, 0);
        }

        /* 480 pixels over a 160-row font is 3 rows: a header, a footer and
         * nothing to edit in. Rejected before a byte is sent, because the mode
         * packet arrives either way and there is no undoing it afterwards. */
        scr_init(&scr, 32);
        install_oversize(256 * 160);
        cap_start();
        const bool tall = scr_load_font(&scr, "/f.bin");
        n = cap_read(got, sizeof(got));
        check("a font leaving too few rows to edit in", tall, 0);
        check("  and nothing was sent", n, 0);

        /* A missing file is the ordinary case, not an error. */
        scr_init(&scr, 32);
        stub_file_reset();
        stub_file_fail_open(1);
        cap_start();
        const bool missing = scr_load_font(&scr, "/nope.bin");
        n = cap_read(got, sizeof(got));
        stub_file_fail_open(0);
        check("a font file that is not there", missing, 0);
        check("  and nothing was sent", n, 0);

        scr_init(&scr, 32);
        check("an empty path", scr_load_font(&scr, ""), 0);
        check("no path at all", scr_load_font(&scr, NULL), 0);
    }

    /* --- a short read still delivers the promised byte count --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        install_font(9, 6);
        stub_file_short_read(64);   /* every read returns at most 64 bytes */
        cap_start();
        scr_load_font(&scr, "/f.bin");
        n = cap_read(got, sizeof(got));
        stub_file_short_read(-1);
        const unsigned char write9[] = {23, 0, 0xA0, 0xED, 0x0A, 0, 0x00, 0x09};
        const unsigned char cre9[] = {23, 0, 0x95, 1, 0xED, 0x0A, 8, 9, 7, 0};
        const int hat = find_seq(got, n, write9, sizeof(write9));
        const int cat = find_seq(got, n, cre9, sizeof(cre9));
        const int glyphs = (hat >= 0 && cat >= 0) ? cat - (hat + (int) sizeof(write9)) : -1;
        check("a chunked read still sends every promised byte", glyphs, 2304);
    }

    /* --- a file shorter than it claims still delivers the promised count --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        /* The directory says 2304 bytes and the file has 1024. The VDP has
         * already been told to expect 2304 and will keep reading until it has
         * them, so the shortfall has to be made up here -- otherwise the next
         * 1280 bytes AED paints become glyph data and the screen is lost. */
        make_font(9, 6);
        stub_file_reset();
        stub_file_set_content(fontbuf, 1024);
        stub_file_set_objsize(2304);
        cap_start();
        scr_load_font(&scr, "/f.bin");
        n = cap_read(got, sizeof(got));
        const unsigned char w9[] = {23, 0, 0xA0, 0xED, 0x0A, 0, 0x00, 0x09};
        const unsigned char c9[] = {23, 0, 0x95, 1, 0xED, 0x0A, 8, 9, 7, 0};
        const int hat = find_seq(got, n, w9, sizeof(w9));
        const int cat = find_seq(got, n, c9, sizeof(c9));
        const int glyphs = (hat >= 0 && cat >= 0) ? cat - (hat + (int) sizeof(w9)) : -1;
        check("a truncated file is padded out to the declared length",
              glyphs, 2304);
    }

    /* --- the system font goes back on the way out, and only if AED changed it --- */
    {
        const unsigned char sysfont[] = {23, 0, 0x95, 0, 0xFF, 0xFF, 0};

        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        cap_start();
        scr_destroy(&scr);
        n = cap_read(got, sizeof(got));
        check("no font loaded, so none is put back",
              find_seq(got, n, sysfont, sizeof(sysfont)), -1);

        scr_init(&scr, 32);
        install_font(9, 6);
        scr_load_font(&scr, "/f.bin");
        check("a loaded font is remembered", scr.fontLoaded_, 1);
        cap_start();
        scr_destroy(&scr);
        n = cap_read(got, sizeof(got));
        check_has("and the system font is selected again",
                  got, n, sysfont, sizeof(sysfont));
    }

    /* --- a VDP with no font API is detected before anything is uploaded --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        install_font(9, 6);

        /* A VDP that has the font API answers a font selection with mode
         * information. One that does not answers nothing, which is what the
         * probe reads -- and is also what stops the editor waiting forever. */
        stub_vdp_mode_reply(0);
        cap_start();
        const bool loaded = scr_load_font(&scr, "/f.bin");
        n = cap_read(got, sizeof(got));
        stub_vdp_mode_reply(1);

        check("a VDP with no font API loads nothing", loaded, 0);
        check("  and no font to put back", scr.fontLoaded_, 0);
        check("  and the rows are left alone", scr.rows_, 60);

        /* The whole cost of finding out: selecting font 65535, the system font,
         * which is already selected and so changes nothing on a VDP that knows
         * it. 2304 bytes of glyph data would have gone to a VDP that does not. */
        const unsigned char probe[] = {23, 0, 0x95, 0, 0xFF, 0xFF, 0};
        check("  and the probe is all that was sent", n, (int) sizeof(probe));
        check_has("  which is a system-font selection", got, n, probe, sizeof(probe));

        /* The probe is the last check, so a file that can be rejected without
         * asking the VDP anything still costs nothing at all. */
        scr_init(&scr, 32);
        install_raw(2300);
        stub_vdp_mode_reply(0);
        cap_start();
        scr_load_font(&scr, "/f.bin");
        n = cap_read(got, sizeof(got));
        stub_vdp_mode_reply(1);
        check("a bad file is rejected without probing", n, 0);
    }

    /* --- geometry that does not match the font is not trusted --- */
    {
        stub_set_screen(80, 60);
        stub_set_cell(8, 8);
        scr_init(&scr, 32);
        install_font(9, 6);

        /* The VDP has the font API and answers the probe, and takes the font.
         * What does not happen is MOS learning the new mode, so the sysvars go
         * on describing 60 rows of 8-pixel cells.
         *
         * Trusting them lays 60 rows out on a screen that now has 53: the
         * editor paints off the bottom, scrolls away what it just drew, and
         * leaves the cursor where the screen no longer reaches. A blank screen
         * and rubbish on every keypress, from numbers that looked fine. */
        stub_vdp_font_applies(0);
        cap_start();
        const bool loaded = scr_load_font(&scr, "/f.bin");
        n = cap_read(got, sizeof(got));
        stub_vdp_font_applies(1);

        check("a font whose geometry never arrives is refused", loaded, 0);
        check("  and no font to put back", scr.fontLoaded_, 0);
        check("  and the layout is left on the old font", scr.charH_, 8);
        check("  with the rows to match", scr.rows_, 60);

        const unsigned char sysfont[] = {23, 0, 0x95, 0, 0xFF, 0xFF, 0};
        /* The font was selected before this was known, so it has to be given
         * back -- otherwise the screen is in a font the editor is not laying
         * out for, which is the very thing being avoided. */
        const unsigned char create9[] = {23, 0, 0x95, 1, 0xED, 0x0A, 8, 9, 7, 0};
        const int cat = find_seq(got, n, create9, sizeof(create9));
        const int last = cat >= 0 ? find_seq(got + cat, n - cat, sysfont, sizeof(sysfont)) : -1;
        check("  and the system font is put back afterwards", last >= 0, 1);
    }

    /* --- the settings file carries the path --- */
    {
        config cfg;
        cfg_defaults(&cfg);
        check("no font by default", cfg.font[0], 0);

        cfg_defaults(&cfg);
        cfg_parse(&cfg, "[editor]\nfont = /config/aed/u8x9.bin\n", 36);
        check("the path is read", strcmp(cfg.font, "/config/aed/u8x9.bin"), 0);

        /* Under the wrong heading it is somebody else's setting, the same way
         * a [syntax] fg would be. */
        cfg_defaults(&cfg);
        cfg_parse(&cfg, "[colours]\nfont = /a.bin\n", 24);
        check("but not from the wrong section", cfg.font[0], 0);

        /* Half a path names a different file. Dropped, not truncated. */
        {
            static char big[256];
            int at = sprintf(big, "[editor]\nfont = /");
            for (int i = 0; i < CFG_FONT_MAX; i++) {
                big[at++] = 'x';
            }
            big[at++] = '\n';
            cfg_defaults(&cfg);
            cfg_parse(&cfg, big, at);
            check("a path too long to hold is dropped", cfg.font[0], 0);
        }
    }

    /* --- saving a colour must not disturb a font line AED never writes --- */
    {
        static const char before[] =
            "[editor]\r\nfont = /config/aed/u8x9.bin\r\n[colours]\r\nfg = 1\r\n";
        stub_file_reset();
        stub_file_set_content(before, (int) strlen(before));

        config cfg;
        cfg_defaults(&cfg);
        cfg.fg = 9;
        cfg_update(&cfg, "/config/aed.cfg");

        const char* out = stub_file_bytes();
        check("the font line survives a colour change",
              strstr(out, "font = /config/aed/u8x9.bin") != NULL, 1);
        check("and the colour did change", strstr(out, "fg = 9") != NULL, 1);
        int fonts = 0;
        for (const char* p = out; (p = strstr(p, "font")) != NULL; p += 4) {
            fonts++;
        }
        check("and it appears exactly once", fonts, 1);
    }

    fflush(stdout);

    return failures == 0 ? 0 : 1;
}
