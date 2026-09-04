/*
 * Host tests for the settings file.
 *
 * The file is an INI file: [section] headings, then name = value lines. The
 * parser has to be forgiving, because the file is hand-edited and a file written
 * by a later version of the editor must still load in an older one. So unknown
 * sections and names, blank lines, comments and malformed values are all skipped
 * rather than treated as failures, and a setting the file does not mention keeps
 * whatever default the caller already had.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "config.h"
#include "editor.h"
#include "cmd_ops.h"
#include "vkey.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static int tab_of_raw(const char* text) {
    config cfg;
    cfg_defaults(&cfg);
    cfg_parse(&cfg, text, (int) strlen(text));

    return cfg.tab_size;
}

// `tab` lives in [editor], so the section heading is part of every case below
// that is not itself about sections.
static int tab_of(const char* body) {
    static char text[512];
    snprintf(text, sizeof(text), "[editor]\n%s", body);

    return tab_of_raw(text);
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
    check("empty file leaves it unset", tab_of_raw(""), -1);
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
        "[editor]\r\n"
        "; how wide a tab renders, in columns\r\n"
        "tab = 8\r\n"
        "\r\n"
        "# not understood by this version, and must not matter\r\n"
        "theme = dark\r\n"
        "\r\n"
        "tab = 8   # trailing comments are fine too\r\n"
        "\r\n"
        "[colours]\r\n"
        "fg = 15\r\n";
    check("a realistic hand-written file", tab_of_raw(real), 8);

    /* --- sections --- */
    /* A name only means something inside the section that owns it. Without this
     * a future [syntax] section could not reuse a name like `fg` for something
     * else, which is most of the reason for having sections at all. */
    /* ...except before the first heading, where a name is taken at face value.
     * The first settings file AED wrote had no headings, and a hand-edited file
     * missing one should still do what it plainly says. */
    check("a name before any heading is taken at face value",
          tab_of_raw("tab = 8\n"), 8);
    check("a name in the wrong section is ignored",
          tab_of_raw("[colours]\ntab = 8\n"), -1);
    check("the heading scopes every name under it",
          tab_of_raw("[colours]\nfg = 1\n[editor]\ntab = 8\n"), 8);
    check("a later heading ends the previous section",
          tab_of_raw("[editor]\n[colours]\ntab = 8\n"), -1);
    check("headings are matched without regard to case",
          tab_of_raw("[EdiTor]\nTAB = 8\n"), 8);
    check("spaces inside the brackets are trimmed",
          tab_of_raw("[  editor  ]\ntab = 8\n"), 8);
    check("an unknown section is skipped whole",
          tab_of_raw("[syntax]\ntab = 2\n[editor]\ntab = 8\n"), 8);
    /* Checked from inside another section, so a bracket wrongly taken as a
     * heading would show up as the setting being applied rather than skipped. */
    check("an unclosed bracket is not a heading",
          tab_of_raw("[colours]\n[editor\ntab = 8\n"), -1);
    check("a comment may follow a heading",
          tab_of_raw("[editor]  # editing\ntab = 8\n"), 8);
    check("a semicolon starts a comment too", tab_of("; tab = 2\ntab = 8\n"), 8);
    check("a trailing semicolon comment", tab_of("tab = 8 ; columns\n"), 8);

    /* The whole of the first file AED ever wrote, headings and all absent. It
     * has to keep working: an installation that has one would otherwise read as
     * an empty file and silently lose every setting in it. */
    static const char legacy[] =
        "# AED settings.\r\n"
        "\r\n"
        "# How wide a tab renders, in columns. 1 to 16.\r\n"
        "tab = 8\r\n"
        "\r\n"
        "fg = 15\r\n"
        "bg = 2\r\n";
    config old_file;
    cfg_defaults(&old_file);
    cfg_parse(&old_file, legacy, (int) sizeof(legacy) - 1);
    check("a pre-INI file still loads: tab", old_file.tab_size, 8);
    check("  and its foreground", old_file.fg, 15);
    check("  and its background", old_file.bg, 2);

    /* --- loading from a file --- */
    config cfg;
    cfg_defaults(&cfg);
    stub_file_reset();   /* opens, but the file is empty */
    check("an empty file loads nothing", cfg_load(&cfg, CFG_PATH) ? 1 : 0, 0);
    check("  and leaves the settings alone", cfg.tab_size, -1);

    stub_file_reset();
    static const char body[] = "[editor]\ntab=3\n";
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
    static const char scheme[] =
        "[editor]\r\ntab=4\r\n[colours]\r\nfg=15\r\nbg=1\r\n";
    cfg_parse(&col, scheme, (int) sizeof(scheme) - 1);
    check("fg is read", col.fg, 15);
    check("bg is read", col.bg, 1);
    config zero;
    cfg_defaults(&zero);
    cfg_parse(&zero, "[colours]\nbg=0\n", 15);
    check("bg=0 is a real value, not 'unset'", zero.bg, 0);

    /* --- what first run writes --- */
    config out;
    out.tab_size = 4;
    out.fg = 15;
    out.bg = 0;
    static char rendered[1024];
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

    /* --- a short write must not leave a truncated file behind --- */
    /* A truncated settings file still parses, so the next startup would load it
     * and never rewrite the settings that never made it to disk. */
    stub_file_reset();
    stub_file_short_write(20);
    check("a short write reports failure", cfg_save(&out, CFG_PATH) ? 1 : 0, 0);
    check("  and the partial file is removed", stub_deletes(), 1);

    stub_file_reset();
    check("a complete write keeps the file", cfg_save(&out, CFG_PATH) ? 1 : 0, 1);
    check("  and deletes nothing", stub_deletes(), 0);

    /* --- what ed_init does with a settings file --- */
    /* Each setting applies on its own: one named in the file takes effect, one
     * left out keeps whatever the editor already had. */
    static const char only_fg[] = "[colours]\r\nfg=3\r\n";
    stub_file_reset();
    stub_file_set_content(only_fg, (int) sizeof(only_fg) - 1);
    editor ed;
    check("editor starts with a settings file", ed_init(&ed, 8, NULL) != NULL, 1);
    check("fg alone is applied", scr_fg(&ed.scr_), 3);
    check("  and bg keeps the measured value", scr_bg(&ed.scr_), 0);
    check("  and tab keeps its default", scr_tab_size(&ed.scr_), SCR_DEFAULT_TAB_SIZE);
    ed_destroy(&ed);

    static const char only_bg[] =
        "[colours]\r\nbg=5\r\n[editor]\r\ntab=8\r\n";
    stub_file_reset();
    stub_file_set_content(only_bg, (int) sizeof(only_bg) - 1);
    check("editor starts again", ed_init(&ed, 8, NULL) != NULL, 1);
    check("bg alone is applied", scr_bg(&ed.scr_), 5);
    check("  and tab alongside it", scr_tab_size(&ed.scr_), 8);
    ed_destroy(&ed);

    /* No file at all: the editor writes one holding what it is starting with. */
    stub_file_reset();
    stub_file_fail_open(1);
    check("editor starts with no settings file", ed_init(&ed, 8, NULL) != NULL, 1);
    check("  and tried to create /config", stub_mkdirs() >= 1, 1);
    ed_destroy(&ed);

    /* --- updating an existing file must not reformat it --- */
    /* The file is hand-edited, so saving a colour change has to leave comments,
     * spacing, blank lines and unknown settings exactly where they were. */
    static const char handwritten[] =
        "# my settings, do not lose this line\r\n"
        "\r\n"
        "[editor]\r\n"
        "tab   =   8      # I like wide tabs\r\n"
        "theme = midnight\r\n"
        "\r\n"
        "[colours]\r\n"
        "fg = 15\r\n"
        "bg = 0\r\n";
    static char merged[1024];

    config change;
    cfg_defaults(&change);
    change.fg = 3;
    change.bg = 4;
    change.tab_size = 8;

    stub_file_reset();
    stub_file_set_content(handwritten, (int) sizeof(handwritten) - 1);
    check("update succeeds", cfg_update(&change, CFG_PATH) ? 1 : 0, 1);
    const int mn = stub_file_size();
    memcpy(merged, stub_file_bytes(), (size_t) mn);
    merged[mn] = 0;

    check("the user's comment survives",
          strstr(merged, "# my settings, do not lose this line") != NULL, 1);
    check("an unknown setting survives", strstr(merged, "theme = midnight") != NULL, 1);
    check("a trailing comment survives", strstr(merged, "# I like wide tabs") != NULL, 1);
    check("an unchanged setting is left completely alone",
          strstr(merged, "tab   =   8      # I like wide tabs") != NULL, 1);
    check("the headings survive", strstr(merged, "[colours]") != NULL, 1);
    check("  both of them", strstr(merged, "[editor]") != NULL, 1);
    check("the new fg is written", strstr(merged, "fg = 3") != NULL, 1);
    check("the new bg is written", strstr(merged, "bg = 4") != NULL, 1);
    check("the old fg is gone", strstr(merged, "fg = 15") == NULL, 1);

    /* And it must still parse back to what we asked for. */
    config after;
    cfg_defaults(&after);
    cfg_parse(&after, merged, mn);
    check("round trip: fg", after.fg, 3);
    check("round trip: bg", after.bg, 4);
    check("round trip: tab untouched in value", after.tab_size, 8);

    /* A setting the file never mentioned gets appended rather than dropped. */
    static const char no_colours[] = "[editor]\r\ntab = 4\r\n";
    stub_file_reset();
    stub_file_set_content(no_colours, (int) sizeof(no_colours) - 1);
    check("update with a missing key succeeds", cfg_update(&change, CFG_PATH) ? 1 : 0, 1);
    const int an = stub_file_size();
    memcpy(merged, stub_file_bytes(), (size_t) an);
    merged[an] = 0;
    cfg_defaults(&after);
    cfg_parse(&after, merged, an);
    check("the absent fg was appended", after.fg, 3);
    check("the absent bg was appended", after.bg, 4);
    /* ...under a heading of their own, because a bare name after [editor] would
     * be read back as an editor setting and lost. */
    check("  under a heading of their own",
          strstr(merged, "[colours]") != NULL, 1);
    /* ...and a setting that was already correct is not appended twice. */
    int tabs_seen = 0;
    for (const char* q = merged; (q = strstr(q, "tab")) != NULL; q++) {
        tabs_seen++;
    }
    check("the already-correct tab appears once, not twice", tabs_seen, 1);

    /* A file too large to hold whole is left alone rather than truncated. The
     * merge writes back what it read, so writing back a partial read would eat
     * everything past the buffer -- much worse than not saving a colour.
     *
     * Every setting here is one the merge would either leave alone or *shorten*
     * (fg 15 -> 3, bg 10 -> 4), so the rewritten text fits in the buffer and the
     * write would go through. A file that merely overflows proves nothing: the
     * merge would refuse it anyway for running out of room. */
    static char huge[4096];
    int hn = 0;
    hn += snprintf(huge + hn, sizeof(huge) - hn,
                   "[editor]\r\ntab = 8\r\n[colours]\r\nfg = 15\r\nbg = 10\r\n");
    while (hn < 3000) {
        hn += snprintf(huge + hn, sizeof(huge) - hn, "# padding padding\r\n");
    }
    stub_file_reset();
    stub_file_set_content(huge, hn);
    check("an oversized file is not merged", cfg_update(&change, CFG_PATH) ? 1 : 0, 0);
    check("  and is left untouched on disk", stub_file_opens_for_write(), 0);

    /* A file whose last line has no newline: an appended setting must start on
     * a line of its own, not glued onto the end of the one already there. */
    static const char unterminated[] = "[colours]\r\nfg = 15";
    stub_file_reset();
    stub_file_set_content(unterminated, (int) sizeof(unterminated) - 1);
    check("update a file with no trailing newline",
          cfg_update(&change, CFG_PATH) ? 1 : 0, 1);
    const int un = stub_file_size();
    memcpy(merged, stub_file_bytes(), (size_t) un);
    merged[un] = 0;
    cfg_defaults(&after);
    cfg_parse(&after, merged, un);
    check("  the last line still reads back", after.fg, 3);
    check("  and the appended one is on its own line", after.bg, 4);

    /* With no file at all it falls back to writing a fresh one. */
    stub_file_reset();
    stub_file_fail_open(1);
    check("update with no file to merge into still fails cleanly",
          cfg_update(&change, CFG_PATH) ? 1 : 0, 0);

    /* A trailing comment on a line whose value *does* change must survive too.
     * The tab line above keeps its comment only because its value was already
     * correct and the line is copied through untouched. */
    static const char commented[] =
        "[colours]\r\nfg = 15  # my foreground\r\n";
    stub_file_reset();
    stub_file_set_content(commented, (int) sizeof(commented) - 1);
    check("update a commented line", cfg_update(&change, CFG_PATH) ? 1 : 0, 1);
    const int cn = stub_file_size();
    memcpy(merged, stub_file_bytes(), (size_t) cn);
    merged[cn] = 0;
    check("the changed line keeps its comment",
          strstr(merged, "# my foreground") != NULL, 1);
    check("  and carries the new value", strstr(merged, "fg = 3") != NULL, 1);

    /* --- exit puts the machine back the way it was found --- */
    /* AED measures the colours it starts with; whatever scheme the editor ends
     * up using, those are the ones the user gets back. */
    screen sc;
    memset(&sc, 0, sizeof(sc));
    sc.colors_ = 16;
    sc.entryFg_ = 7;
    sc.entryBg_ = 1;
    scr_set_scheme(&sc, 3, 4);
    check("the editor is using its own scheme", stub_last_fg(), 3);
    check("  and its own background", stub_last_bg(), 4);
    stub_colours_reset();
    scr_destroy(&sc);
    check("exit restores the entry foreground", stub_last_fg(), 7);
    check("exit restores the entry background", stub_last_bg(), 1);

    /* --- picking a colour writes it down --- */
    /* Drive the picker: one UP (foreground up by one) then RETURN to accept. */
    static const stub_key pick[] = {
        { 0, VK_UP, 0 },
        { 0, VK_RETURN, 0 },
    };
    static const char before_pick[] =
        "# keep me\r\n[editor]\r\ntab = 4\r\n"
        "[colours]\r\nfg = 1\r\nbg = 0\r\n";
    stub_file_reset();
    stub_file_set_content(before_pick, (int) sizeof(before_pick) - 1);
    editor pe;
    check("editor starts for the picker", ed_init(&pe, 8, NULL) != NULL, 1);
    const char fg_before = scr_fg(&pe.scr_);
    stub_file_reset();
    stub_file_set_content(before_pick, (int) sizeof(before_pick) - 1);
    stub_set_keys(pick, 2);
    cmd_color_picker(&pe);
    check("the picker changed the foreground",
          scr_fg(&pe.scr_) != fg_before, 1);
    check("  and wrote the file", stub_file_size() > 0, 1);
    memcpy(merged, stub_file_bytes(), (size_t) stub_file_size());
    merged[stub_file_size()] = 0;
    check("  keeping the user's comment", strstr(merged, "# keep me") != NULL, 1);
    config picked;
    cfg_defaults(&picked);
    cfg_parse(&picked, merged, stub_file_size());
    check("  and the saved fg matches the screen", picked.fg, scr_fg(&pe.scr_));

    /* Picking a colour must write *only* the colours. A tab value the editor
     * clamped on the way in, or one the file never had, would otherwise be
     * rewritten or invented behind the user's back. */
    static const char odd_tab[] = "[editor]\r\ntab = 99\r\n[colours]\r\nfg = 1\r\n";
    stub_file_reset();
    stub_file_set_content(odd_tab, (int) sizeof(odd_tab) - 1);
    stub_set_keys(pick, 2);
    cmd_color_picker(&pe);
    memcpy(merged, stub_file_bytes(), (size_t) stub_file_size());
    merged[stub_file_size()] = 0;
    check("the picker leaves an out-of-range tab exactly as written",
          strstr(merged, "tab = 99") != NULL, 1);

    static const char no_tab[] = "[colours]\r\nfg = 1\r\nbg = 0\r\n";
    stub_file_reset();
    stub_file_set_content(no_tab, (int) sizeof(no_tab) - 1);
    stub_set_keys(pick, 2);
    cmd_color_picker(&pe);
    memcpy(merged, stub_file_bytes(), (size_t) stub_file_size());
    merged[stub_file_size()] = 0;
    check("  and does not invent a tab setting that was never there",
          strstr(merged, "tab") == NULL, 1);

    ed_destroy(&pe);
    stub_set_keys(NULL, 0);

    /* The VDP pauses for a few frames whenever a line wraps while CTRL is
     * held -- its own default is 3, which is what makes CTRL with an arrow key
     * drag once a line reaches the right edge. The setting exists so that can
     * be turned off, and it is off unless the file asks, because the VDU that
     * sets it is a later addition and an older VDP reads the bytes after it as
     * commands. */
    {
        config c;
        cfg_defaults(&c);
        check("unset unless the file says so", c.ctrl_pause, -1);

        static const char text[] = "[vdp]\r\nctrl_pause_frames = 0\r\n";
        cfg_parse(&c, text, (int) sizeof(text) - 1);
        check("the file can turn it off", c.ctrl_pause, 0);

        cfg_defaults(&c);
        static const char other[] = "[editor]\r\nctrl_pause_frames = 5\r\n";
        cfg_parse(&c, other, (int) sizeof(other) - 1);
        check("...and only under its own heading", c.ctrl_pause, -1);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
