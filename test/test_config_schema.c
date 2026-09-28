/*
 * The settings engine with a schema other than AED's.
 *
 * Which settings there are, where their values live and how a fresh file
 * reads are each program's: config.c reads and rewrites against whatever
 * schema it is handed. So these run the engine for a made-up second program
 * -- different sections, names, struct layout and file text -- and check it
 * reads, writes and merges that program's settings and nothing of AED's. The
 * end checks AED's own file round-trips through the engine byte for byte.
 */

#include <stddef.h>
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

/* Another program's settings: the fields in a different order from any of
 * AED's, a text setting that can be asked to be empty, and one that cannot. */
typedef struct {
    char project[16];
    int width;
    bool project_none;
    char tool[8];
    int depth;
} other;

static const cfg_setting OTHER_SETTINGS[] = {
    { "build", "width",   CFG_INT, offsetof(other, width), 0, -1 },
    { "build", "project", CFG_STR, offsetof(other, project), 16,
      offsetof(other, project_none) },
    { "tools", "tool",    CFG_STR, offsetof(other, tool), 8, -1 },
    { "tools", "depth",   CFG_INT, offsetof(other, depth), 0, -1 },
};

static int other_render(const void* values, char* buf, int max) {
    const other* o = (const other*) values;
    int at = cfg_put_text(buf, 0, max, "# other\r\n[build]\r\n");
    at = cfg_put_setting(buf, at, max, "width", o->width);

    return cfg_put_text(buf, at, max, "[tools]\r\n");
}

static const cfg_schema OTHER = { OTHER_SETTINGS, 4, other_render };

static int file_is(const char* want) {
    return strcmp(stub_file_bytes(), want) == 0;
}

int main(void) {
    stub_discard_output();

    /* --- defaults clear what the schema names, where it says --- */
    {
        other o;
        memset(&o, 0x55, sizeof(o));
        cfg_defaults(&OTHER, &o);
        check("an int setting starts unset", o.width, -1);
        check("  and so does the other", o.depth, -1);
        check("a text setting starts empty", o.project[0], 0);
        check("  with its none flag clear", o.project_none ? 1 : 0, 0);
        check("  and the other text one empty", o.tool[0], 0);
    }

    /* --- parsing fills the fields the schema points at --- */
    {
        static const char TEXT[] =
            "[build]\r\nwidth = 40\r\nproject = demo  # mine\r\n"
            "[tools]\r\ntool = zap\r\ndepth = 3\r\n"
            "[editor]\r\ntab = 4\r\n";
        other o;
        cfg_defaults(&OTHER, &o);
        cfg_parse(&OTHER, &o, TEXT, (int) sizeof(TEXT) - 1);
        check("its int is read", o.width, 40);
        check("  into its own field", o.depth, 3);
        check("its text is read", strcmp(o.project, "demo"), 0);
        check("  and the other text", strcmp(o.tool, "zap"), 0);

        static const char LONG[] = "[tools]\r\ntool = toolong12\r\n";
        cfg_defaults(&OTHER, &o);
        cfg_parse(&OTHER, &o, LONG, (int) sizeof(LONG) - 1);
        check("text too long for its field is dropped, by its own size",
              o.tool[0], 0);
        static const char WRONG[] = "[build]\r\ndepth = 9\r\n";
        cfg_parse(&OTHER, &o, WRONG, (int) sizeof(WRONG) - 1);
        check("a name under another section is ignored", o.depth, -1);
    }

    /* --- a fresh file is the program's own --- */
    {
        other o;
        cfg_defaults(&OTHER, &o);
        o.width = 7;
        stub_file_reset();
        check("saving writes a file",
              cfg_save(&OTHER, &o, "/config/other.ini") ? 1 : 0, 1);
        check("  from the program's render",
              file_is("# other\r\n[build]\r\nwidth = 7\r\n[tools]\r\n"), 1);
    }

    /* --- a merge changes only what the program sets --- */
    {
        static const char BEFORE[] =
            "[build]\r\nwidth = 1  # keep\r\n[tools]\r\n"
            "[editor]\r\ntab = 4\r\n";
        other o;
        cfg_defaults(&OTHER, &o);
        o.width = 2;
        o.project_none = true;
        strcpy(o.tool, "acc");
        stub_file_reset();
        stub_file_set_content(BEFORE, (int) sizeof(BEFORE) - 1);
        check("an update succeeds",
              cfg_update(&OTHER, &o, "/config/other.ini") ? 1 : 0, 1);
        check("  replacing its value, keeping the comment, adding the rest",
              file_is("[build]\r\nwidth = 2  # keep\r\nproject = \r\n"
                      "[tools]\r\ntool = acc\r\n[editor]\r\ntab = 4\r\n"), 1);
    }

    /* --- a schema too big to track writes nothing --- */
    {
        static cfg_setting many[CFG_SETTINGS_MAX + 1];
        for (int i = 0; i <= CFG_SETTINGS_MAX; i++) {
            many[i] = OTHER_SETTINGS[0];
        }
        const cfg_schema big = { many, CFG_SETTINGS_MAX + 1, other_render };
        other o;
        cfg_defaults(&OTHER, &o);
        o.width = 5;
        static const char BEFORE[] = "[build]\r\nwidth = 1\r\n";
        stub_file_reset();
        stub_file_set_content(BEFORE, (int) sizeof(BEFORE) - 1);
        check("an update with too many settings refuses",
              cfg_update(&big, &o, "/config/other.ini") ? 1 : 0, 0);
    }

    /* --- AED's own file round-trips byte for byte --- */
    {
        static char first[2048];
        static char second[2048];
        config c;
        cfg_defaults(&AED_CONFIG, &c);
        c.tab_size = 4;
        c.fg = 15;
        c.bg = 0;
        const int n = cfg_render(&AED_CONFIG, &c, first, (int) sizeof(first));
        check("AED's fresh file renders", n > 0 ? 1 : 0, 1);

        config back;
        cfg_defaults(&AED_CONFIG, &back);
        cfg_parse(&AED_CONFIG, &back, first, n);
        check("  and reads back what was written",
              back.tab_size * 10000 + back.fg * 100 + back.bg,
              4 * 10000 + 15 * 100);
        const int m = cfg_render(&AED_CONFIG, &back, second,
                                 (int) sizeof(second));
        check("  rendering what was read gives the same bytes",
              m == n && memcmp(first, second, (size_t) n) == 0 ? 1 : 0, 1);

        /* Saving the same settings over it leaves the file as it was. */
        first[n] = 0;
        stub_file_reset();
        stub_file_set_content(first, n);
        cfg_update(&AED_CONFIG, &back, "/config/aed.ini");
        check("  and updating it with them changes nothing", file_is(first), 1);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
