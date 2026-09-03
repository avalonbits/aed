/*
 * Host tests for the settings file.
 *
 * The parser has to be forgiving: the file is hand-edited, and a file written
 * by a later version of the editor must still load in an older one. So unknown
 * keys, blank lines, comments and malformed values are all skipped rather than
 * treated as failures, and a setting the file does not mention keeps whatever
 * default the caller already had.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "config.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static int tab_of(const char* text) {
    config cfg;
    cfg_defaults(&cfg);
    cfg_parse(&cfg, text, (int) strlen(text));

    return cfg.tab_size;
}

int main(void) {
    stub_discard_output();

    /* --- the basic shape --- */
    check("a plain setting", tab_of("tab=8"), 8);
    check("trailing newline", tab_of("tab=8\n"), 8);
    check("CRLF line endings", tab_of("tab=8\r\n"), 8);
    check("spaces around the equals", tab_of("  tab  =  8  \n"), 8);
    check("a tab character as whitespace", tab_of("\ttab\t=\t8\t\n"), 8);

    /* --- things that must not break it --- */
    check("empty file leaves it unset", tab_of(""), -1);
    check("only a comment", tab_of("# tab=8\n"), -1);
    check("comment after a real setting is ignored",
          tab_of("tab=2\n# tab=8\n"), 2);
    check("a trailing comment is stripped", tab_of("tab=8  # columns\n"), 8);
    check("a trailing comment with no space", tab_of("tab=8#columns\n"), 8);
    check("a commented-out setting stays off", tab_of("#tab=8\n"), -1);
    check("a line that is only a comment marker", tab_of("#\ntab=5\n"), 5);
    check("blank lines", tab_of("\n\n  \n tab=6 \n\n"), 6);
    check("a line with no equals", tab_of("nonsense\ntab=6\n"), 6);
    check("an unknown key", tab_of("colour=3\ntab=6\n"), 6);
    check("a key that only looks like ours", tab_of("tabs=9\n"), -1);
    check("a prefix of our key", tab_of("ta=9\n"), -1);

    /* --- malformed values leave the setting alone --- */
    check("no value at all", tab_of("tab=\n"), -1);
    check("a non-numeric value", tab_of("tab=wide\n"), -1);
    check("a partly numeric value", tab_of("tab=8x\n"), -1);
    check("a negative value is not a number here", tab_of("tab=-4\n"), -1);
    check("an absurd value is rejected, not wrapped", tab_of("tab=999999\n"), -1);

    /* --- last one wins, so a file can be appended to --- */
    check("the later setting wins", tab_of("tab=2\ntab=8\n"), 8);
    /* ...but only when the later line is actually valid: a malformed value must
     * leave the earlier one standing rather than reset it. */
    check("an empty value does not clobber an earlier one",
          tab_of("tab=8\ntab=\n"), 8);
    check("a junk value does not clobber an earlier one",
          tab_of("tab=8\ntab=wide\n"), 8);

    /* --- a realistic file --- */
    static const char real[] =
        "# AED settings\r\n"
        "\r\n"
        "# how wide a tab renders, in columns\r\n"
        "tab = 8\r\n"
        "\r\n"
        "# not understood by this version, and must not matter\r\n"
        "theme = dark\r\n"
        "\r\n"
        "tab = 8   # trailing comments are fine too\r\n";
    check("a realistic hand-written file", tab_of(real), 8);

    /* --- loading from a file --- */
    config cfg;
    cfg_defaults(&cfg);
    stub_file_reset();   /* opens, but the file is empty */
    check("an empty file loads nothing", cfg_load(&cfg, CFG_PATH) ? 1 : 0, 0);
    check("  and leaves the settings alone", cfg.tab_size, -1);

    stub_file_reset();
    static const char body[] = "tab=3\n";
    stub_file_set_content(body, (int) sizeof(body) - 1);
    cfg_defaults(&cfg);
    check("loading a real file", cfg_load(&cfg, CFG_PATH) ? 1 : 0, 1);
    check("  applies its settings", cfg.tab_size, 3);

    /* The normal case for most users: no settings file at all. It must not be
     * treated as a failure worth reporting -- the editor simply starts with its
     * own defaults. */
    stub_file_reset();
    stub_file_fail_open(1);
    cfg_defaults(&cfg);
    check("a missing file loads nothing", cfg_load(&cfg, CFG_PATH) ? 1 : 0, 0);
    check("  and changes nothing", cfg.tab_size, -1);

    check("a NULL path is refused", cfg_load(&cfg, NULL) ? 1 : 0, 0);

    /* --- colours --- */
    config col;
    cfg_defaults(&col);
    static const char scheme[] = "tab=4\r\nfg=15\r\nbg=1\r\n";
    cfg_parse(&col, scheme, (int) sizeof(scheme) - 1);
    check("fg is read", col.fg, 15);
    check("bg is read", col.bg, 1);
    config zero;
    cfg_defaults(&zero);
    cfg_parse(&zero, "bg=0\n", 5);
    check("bg=0 is a real value, not 'unset'", zero.bg, 0);

    /* --- what first run writes --- */
    config out;
    out.tab_size = 4;
    out.fg = 15;
    out.bg = 0;
    static char rendered[512];
    const int rn = cfg_render(&out, rendered, sizeof(rendered));
    check("render produces something", rn > 0, 1);

    /* It must read back as exactly what went in -- that round trip is the whole
     * point of writing a default file. */
    config back;
    cfg_defaults(&back);
    cfg_parse(&back, rendered, rn);
    check("round trip: tab", back.tab_size, 4);
    check("round trip: fg", back.fg, 15);
    check("round trip: bg", back.bg, 0);

    /* And it must be a file a person can read. */
    check("the written file explains itself",
          strstr(rendered, "# AED settings") != NULL, 1);
    check("with a comment above each setting",
          strstr(rendered, "in columns") != NULL, 1);

    /* A buffer too small must refuse rather than write a truncated file. Try
     * every size up to the full length, on an exactly-sized heap block so the
     * sanitizer catches a write past the end rather than it going unnoticed --
     * the interesting sizes are the ones where the header fits but a setting
     * does not, which a single small buffer never reaches. */
    int refused = 0, produced = 0, overlong = 0;
    for (int size = 1; size <= rn + 8; size++) {
        char* probe = malloc((size_t) size);
        if (probe == NULL) {
            break;
        }
        const int got = cfg_render(&out, probe, size);
        if (got == 0) {
            refused++;
        } else {
            produced++;
            if (got > size) {
                overlong++;
            }
        }
        free(probe);
    }
    check("small buffers are refused", refused > 0, 1);
    check("large enough buffers still render", produced > 0, 1);
    check("no render ever claims more than the buffer", overlong, 0);

    /* --- saving --- */
    stub_file_reset();
    check("save reports success", cfg_save(&out, CFG_PATH) ? 1 : 0, 1);
    check("  and wrote the rendered bytes", stub_file_size(), rn);
    check("  and closed the file", stub_file_closes(), 1);
    check("  after trying to create the directory", stub_mkdirs(), 1);
    check("  which is /config", strcmp(stub_last_mkdir(), CFG_DIR), 0);

    stub_file_reset();
    stub_file_fail_open(1);
    check("save fails quietly when the file cannot be opened",
          cfg_save(&out, CFG_PATH) ? 1 : 0, 0);
    check("  and wrote nothing", stub_file_size(), 0);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
