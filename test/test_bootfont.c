/*
 * Host tests for reading the boot script's font selection.
 *
 * AED cannot ask the VDP which font is in force -- no font command reports it
 * -- so the only name it has for "put the old font back" is the buffer id the
 * boot script selected. This finds it, or says there is none, in which case
 * AED goes on selecting the stock font as it always did.
 *
 * The awkward part is that MOS's own VDU command can write the selection out
 * by hand, and it is generous about how: spaces or commas, a ';' for a 16-bit
 * value, an 'H' suffix for hex. All of those have to read as the same line.
 */

#include <stdio.h>
#include <string.h>

#include "bootfont.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-56s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-56s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static int scan(const char* text) {
    return bootfont_scan(text, (int) strlen(text));
}

int main(void) {
    /* --- the ordinary case --- */
    check("no boot script at all", bootfont_scan(NULL, 0), -1);
    check("a script that says nothing about fonts",
          scan("SET KEYBOARD 1\r\nbbcbasic\r\n"), -1);
    check("loading a font but never selecting one",
          scan("loadfont 100 /config/aed/unscii16.bin\r\n"), -1);

    /* --- what AED is looking for --- */
    check("fontctl selects a buffer",
          scan("loadfont 100 /f.bin\r\nfontctl 100\r\n"), 100);
    check("  and the case it is typed in does not matter",
          scan("FontCtl 100\r\n"), 100);
    check("  nor a leading star, which MOS treats as whitespace",
          scan("*fontctl 100\r\n"), 100);
    check("  nor leading space", scan("   fontctl 7\r\n"), 7);
    check("a bare LF script", scan("fontctl 42\n"), 42);

    /* --- the same thing written out by hand --- */
    check("the VDU form",        scan("VDU 23 0 149 0 100; 0\r\n"), 100);
    check("  with commas",       scan("VDU 23,0,149,0,100;,0\r\n"), 100);
    check("  with hex",          scan("VDU 23 0 95H 0 100; 0\r\n"), 100);
    check("  all of it in hex",  scan("VDU 17H 0 95H 0 64H; 0\r\n"), 100);
    check("  and lower case vdu", scan("vdu 23 0 149 0 256; 0\r\n"), 256);

    /* --- VDU lines that are not a font selection --- */
    check("a mode change is not one", scan("VDU 22 3\r\n"), -1);
    check("a font command that is not a select",
          scan("VDU 23 0 149 1 100; 8 16 15 0\r\n"), -1);
    check("something else entirely", scan("VDU 23 16 1 0\r\n"), -1);

    /* --- selections that cancel --- */
    check("fontctl sys is the stock font, so nothing to restore",
          scan("fontctl 100\r\nfontctl sys\r\n"), -1);
    /* 65535 is the system font whichever way it is spelled -- `sys` takes a
     * different path through this than the number does. */
    check("  as does fontctl with the number for it",
          scan("fontctl 100\r\nfontctl 65535\r\n"), -1);
    check("  and 65535 written out means the same",
          scan("fontctl 100\r\nVDU 23 0 149 0 65535; 0\r\n"), -1);

    /* --- the last one wins --- */
    check("the last selection is the one in force",
          scan("fontctl 100\r\nfontctl 200\r\n"), 200);
    check("  whichever form it was written in",
          scan("VDU 23 0 149 0 100; 0\r\nfontctl 300\r\n"), 300);
    check("  and a later sys undoes an earlier select",
          scan("fontctl 100\r\nfontctl sys\r\nloadfont 5 /f.bin\r\n"), -1);

    /* --- a real one --- */
    check("the script this was written for",
          scan("SET KEYBOARD 1\r\n"
               "loadfont 100 /config/aed/unscii16.bin\r\n"
               "fontctl 100\r\n"), 100);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
