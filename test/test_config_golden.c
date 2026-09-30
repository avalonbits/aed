/*
 * The settings files AED writes, byte for byte.
 *
 * aed.ini is edited by hand, so what the engine writes has to be exactly what
 * it was: a line moved, a blank doubled or a space shifted is a change the
 * reader sees. The fixtures in test/fixtures/config were written by the
 * engine before it took a schema. These run the same cases through today's
 * and compare every byte: a fresh file, five merges into hand-edited files,
 * and five parses.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "aed_config.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* Whether `got` is the fixture `name`, exactly. */
static int same_as(const char* name, const char* got, int n) {
    static char want[2048];
    char path[96];
    snprintf(path, sizeof(path), "test/fixtures/config/%s", name);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    const int wn = (int) fread(want, 1, sizeof(want), f);
    fclose(f);

    return wn == n && memcmp(want, got, (size_t) n) == 0;
}

/* Five files as a reader might have left them: CRLF with a comment and a
 * blank line; bare LF with no headings; a font line and a section AED does
 * not know; a last line with no LF; and a [vdp] section on its own. */
static const char* INPUTS[] = {
    "[editor]\r\ntab = 4  # mine\r\n\r\n[colours]\r\nfg = 1\r\nbg = 2\r\n",
    "tab=8\nfg=3\n",
    "[editor]\r\nfont = /config/aed/old.bin  # keep\r\n[other]\r\nx = 1\r\n",
    "# only a comment\r\n[colours]\r\nfg = 9\r",
    "[vdp]\r\nctrl_pause_frames = 0\r\n",
};

int main(void) {
    stub_discard_output();
    static char buf[2048];

    /* --- a fresh file --- */
    {
        config c;
        cfg_defaults(&AED_CONFIG, &c);
        c.tab_size = 4;
        c.fg = 15;
        c.bg = 0;
        const int n = cfg_render(&AED_CONFIG, &c, buf, (int) sizeof(buf));
        check("a fresh aed.ini is the one AED has always written",
              same_as("render", buf, n), 1);
    }

    /* --- another program's fresh file --- */
    {
        /* The same settings named by another program: every AED in AED's
         * file, and nothing else, becomes that program's name. */
        static char want[2048];
        FILE* f = fopen("test/fixtures/config/render", "rb");
        const int wn = f != NULL ? (int) fread(want, 1, sizeof(want) - 1, f) : 0;
        if (f != NULL) {
            fclose(f);
        }
        want[wn] = 0;
        for (char* at = strstr(want, "AED"); at != NULL; at = strstr(at, "AED")) {
            memcpy(at, "ade", 3);
        }

        config c;
        cfg_defaults(&AED_CONFIG, &c);
        c.tab_size = 4;
        c.fg = 15;
        c.bg = 0;
        const int n = ed_settings_render(&c, "ade", CFG_DIR "/aed", buf, (int) sizeof(buf));
        check("another program's name goes where AED's does, and only there",
              wn > 0 && n == wn && memcmp(buf, want, (size_t) n) == 0, 1);
    }

    /* --- merges and parses --- */
    for (int i = 0; i < 5; i++) {
        char name[16];
        char what[80];

        config u;
        cfg_defaults(&AED_CONFIG, &u);
        u.fg = 7;
        u.tab_size = 2;
        if (i == 2) {
            strcpy(u.font, "/config/aed/new.bin");
        }
        if (i == 4) {
            u.ctrl_pause = 3;
            u.font_none = true;
        }
        stub_file_reset();
        stub_file_set_content(INPUTS[i], (int) strlen(INPUTS[i]));
        cfg_update(&AED_CONFIG, &u, "/config/aed.ini");
        const char* out = stub_file_bytes();
        snprintf(name, sizeof(name), "merge%d", i);
        snprintf(what, sizeof(what),
                 "merging into hand-edited file %d, byte for byte", i);
        check(what, same_as(name, out, (int) strlen(out)), 1);

        config p;
        cfg_defaults(&AED_CONFIG, &p);
        cfg_parse(&AED_CONFIG, &p, INPUTS[i], (int) strlen(INPUTS[i]));
        const int k = snprintf(buf, sizeof(buf), "%d %d %d %d [%s] %d\n",
                               p.tab_size, p.fg, p.bg, p.ctrl_pause, p.font,
                               p.font_none);
        snprintf(name, sizeof(name), "parse%d", i);
        snprintf(what, sizeof(what), "parsing hand-edited file %d", i);
        check(what, same_as(name, buf, k), 1);
    }

    /* --- a setting of 0 is a setting --- */
    {
        /* Black is colour 0. Saved into a file with no [colours] section, it
         * still needs the heading written and the value under it: a merge that
         * took 0 for "not set" would drop it without a word. */
        static const char NO_COLOURS[] = "[editor]\r\ntab = 4\r\n";
        config c;
        cfg_defaults(&AED_CONFIG, &c);
        c.bg = 0;
        stub_file_reset();
        stub_file_set_content(NO_COLOURS, (int) sizeof(NO_COLOURS) - 1);
        cfg_update(&AED_CONFIG, &c, "/config/aed.ini");
        check("bg = 0 into a file with no [colours] adds the section and it",
              strcmp(stub_file_bytes(),
                     "[editor]\r\ntab = 4\r\n\r\n[colours]\r\nbg = 0\r\n"), 0);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
