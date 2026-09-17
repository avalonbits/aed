/*
 * Host tests for real tab support.
 *
 * Tabs used to be expanded to spaces on load and while typing, because
 * tb->x_ (a byte offset) doubled as the screen column and one byte had to be
 * one column. A tab is now stored as a single byte and expanded only when
 * painting.
 *
 * These cover the three halves that has to get right: the model keeps the byte,
 * the view expands it at the correct stops, and the column<->byte mapping that
 * vertical motion uses to hold a column across lines of different shape.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "screen.h"
#include "text_buffer.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void check_str(const char* name, const char* got, int gotsz, const char* want) {
    const int wantsz = (int) strlen(want);
    if (gotsz == wantsz && memcmp(got, want, (size_t) wantsz) == 0) {
        fprintf(stderr, "PASS  %-54s %d bytes\n", name, gotsz);
    } else {
        fprintf(stderr, "FAIL  %-54s got '%.*s' (%d), want '%s'\n",
                name, gotsz, got, gotsz, want);
        failures++;
    }
}

static screen mkscreen(char cols) {
    screen scr;
    memset(&scr, 0, sizeof(scr));
    scr.rows_ = 25; scr.cols_ = cols; scr.cursor_ = 32;
    scr.topY_ = 1; scr.bottomY_ = 24;
    scr.tab_size_ = SCR_DEFAULT_TAB_SIZE;
    scr.currY_ = 3; scr.originX_ = 0;

    return scr;
}

/* Captures what a paint emits, so tab expansion can be read back. */
static long mark;
static void cap_start(void) { fflush(stdout); mark = ftell(stdout); }
static int cap_read(char* buf, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    int n = (int)(end - mark);
    if (n > max) { n = max; }
    FILE* r = fopen("/tmp/aed_tab_capture", "rb");
    if (r == NULL) { return -1; }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

int main(void) {
    char buf[256];

    /* ---------- the model keeps the byte ---------- */
    stub_discard_output();
    text_buffer tb;
    if (!tb_init(&tb, 8, NULL)) {
        fprintf(stderr, "tb_init failed\n");

        return 2;
    }
    tb_put(&tb, 'a');
    tb_put(&tb, '\t');
    tb_put(&tb, 'b');
    check("a tab occupies one byte in the buffer", tb_used(&tb), 3);
    tb_set_fname(&tb, "t.txt", 5);
    stub_file_reset();
    tb_save(&tb);
    check_str("and is written out verbatim", stub_file_bytes(), stub_file_size(), "a\tb");
    tb_destroy(&tb);

    /* Loading no longer rewrites tabs. */
    stub_file_reset();
    static const char withtabs[] = "\tx\ty\r\n\tz\r\n";
    stub_file_set_content(withtabs, (int) sizeof(withtabs) - 1);
    check("file with tabs loads", tb_init(&tb, 1, "t.txt") != NULL, 1);
    check("no expansion: byte count is unchanged", tb_used(&tb),
          (int) sizeof(withtabs) - 1);
    check("loading tabs alone leaves the buffer clean", tb_changed(&tb) ? 1 : 0, 0);
    stub_file_reset();
    tb_save(&tb);
    check_str("and they survive a save", stub_file_bytes(), stub_file_size(),
              "\tx\ty\r\n\tz\r\n");
    tb_destroy(&tb);

    /* ---------- the view expands at the right stops ---------- */
    if (freopen("/tmp/aed_tab_capture", "w+", stdout) == NULL) {
        fprintf(stderr, "capture failed\n");

        return 2;
    }
    screen scr = mkscreen(20);

    /*
     * Each case below asserts the whole of what its paint put on the wire, and
     * a paint blanks a row only as far as that row previously reached -- so
     * without this each case would read the length of the one before it.
     */
    #define ROW_BLANK() memset(scr.rowFill_, 0, sizeof(scr.rowFill_))

    char l1[] = "\tx";                       /* tab at column 0 -> 4 wide */
    ROW_BLANK();
    cap_start();
    scr_paint_row(&scr, 3, NULL, 0, l1, 2);
    int n = cap_read(buf, sizeof(buf));
    check_str("tab at column 0 renders 4 spaces", buf, n, "    x");

    /*
     * The rule the row-blanking rests on: what a shorter line leaves behind is
     * erased. A row is padded only as far as it previously reached rather than
     * to the window's edge, which is most of what a repaint used to cost -- and
     * the way for that to be wrong is to blank too little and leave the tail of
     * the line before still showing.
     */
    ROW_BLANK();
    cap_start();
    scr_paint_row(&scr, 3, NULL, 0, "abcdefghij", 10);
    n = cap_read(buf, sizeof(buf));
    check_str("a long line paints itself and no more", buf, n, "abcdefghij");

    cap_start();
    scr_paint_row(&scr, 3, NULL, 0, "xy", 2);
    n = cap_read(buf, sizeof(buf));
    check_str("  and a shorter one after it blanks the rest",
              buf, n, "xy        ");

    cap_start();
    scr_paint_row(&scr, 3, NULL, 0, "xy", 2);
    n = cap_read(buf, sizeof(buf));
    check_str("    with nothing left to blank the second time", buf, n, "xy");

    char l2[] = "ab\tc";                     /* tab at column 2 -> 2 wide */
    ROW_BLANK();
    cap_start();
    scr_paint_row(&scr, 3, NULL, 0, l2, 4);
    n = cap_read(buf, sizeof(buf));
    check_str("tab at column 2 renders 2 spaces", buf, n, "ab  c");

    char l3[] = "abcd\te";                   /* tab exactly on a stop -> full width */
    ROW_BLANK();
    cap_start();
    scr_paint_row(&scr, 3, NULL, 0, l3, 6);
    n = cap_read(buf, sizeof(buf));
    check_str("tab on a stop renders a full width", buf, n, "abcd    e");

    /* The line is held either side of the gap; painting must cross the split. */
    ROW_BLANK();
    cap_start();
    scr_paint_row(&scr, 3, "ab", 2, "\tc", 2);
    n = cap_read(buf, sizeof(buf));
    check_str("expansion works across the gap split", buf, n, "ab  c");

    /* Scrolled right: the window starts mid-way through an expanded tab. */
    scr.originX_ = 2;
    ROW_BLANK();
    cap_start();
    scr_paint_row(&scr, 3, NULL, 0, l1, 2);
    n = cap_read(buf, sizeof(buf));
    check_str("origin 2 shows the tail of the tab", buf, n, "  x");
    scr.originX_ = 0;

    /* A wider tab changes the rendering, not the stored bytes. */
    scr_set_tab_size(&scr, 8);
    ROW_BLANK();
    cap_start();
    scr_paint_row(&scr, 3, NULL, 0, l2, 4);
    n = cap_read(buf, sizeof(buf));
    check_str("width 8 renders the same bytes differently", buf, n,
              "ab      c");
    scr_set_tab_size(&scr, SCR_DEFAULT_TAB_SIZE);

    /* An insertion must repaint from the inserted character, not from the
     * cursor: the cursor sits after it, and a tab starts several columns back.
     * Painting from the cursor left the inserted columns showing stale text. */
    scr = mkscreen(20);
    cap_start();
    scr_putc(&scr, '\t', "\t", 1, "col0", 4);
    n = cap_read(buf, sizeof(buf));
    /* Hiding the cursor writes the character back and steps over it -- two
     * bytes, the second being VDU 8 -- and the repaint follows those. */
    check("insert repaints from the inserted tab's column",
          n > 10 && memcmp(buf + 2, "    col0", 8) == 0, 1);

    /* An insert paints the tail of the line through the buffer, and the cursor
     * is put back afterwards. The buffer has to be emptied before that move or
     * the text lands wherever the cursor went next -- and, worse, sits there
     * until something else happens to flush it. */
    stub_writes_reset();
    scr_paint_tail(&scr, "tail", 4);
    check("the tail is written at all", stub_writes() > 0, 1);
    check("...and before the cursor is put back",
          stub_tab_at(), stub_writes());

    /* Same for an ordinary character, which the cursor also sits past. */
    cap_start();
    scr_putc(&scr, 'X', "X", 1, "yz", 2);
    n = cap_read(buf, sizeof(buf));
    check("insert repaints the character itself",
          n > 5 && memcmp(buf + 2, "Xyz", 3) == 0, 1);

    /* The cursor cell cannot contain a control byte. Sending a tab to the VDP
     * moves the cursor instead of painting it, which left the cursor invisible
     * whenever it sat on a tab -- and corrupted the following output. */
    cap_start();
    scr_show_cursor_ch(&scr, '\t');
    n = cap_read(buf, sizeof(buf));
    check("cursor on a tab emits a space, not 0x09", n > 0 && buf[0] == ' ', 1);

    cap_start();
    scr_hide_cursor_ch(&scr, '\t');
    n = cap_read(buf, sizeof(buf));
    check("hiding it does the same", n > 0 && buf[0] == ' ', 1);

    cap_start();
    scr_show_cursor_ch(&scr, 'q');
    n = cap_read(buf, sizeof(buf));
    check("a printable character is still shown as itself", n > 0 && buf[0] == 'q', 1);

    /* Painting a single exposed column after a region scroll needs to know what
     * renders at one document column -- including "inside a tab", which is a
     * space rather than the tab byte. */
    char gl[] = "ab\tcd";      /* cols: a=0 b=1 tab=2,3 c=4 d=5 */
    check("glyph at column 0", scr_glyph_at(&scr, gl, 5, 0), 'a');
    check("glyph at column 1", scr_glyph_at(&scr, gl, 5, 1), 'b');
    check("glyph inside a tab is a space", scr_glyph_at(&scr, gl, 5, 2), ' ');
    check("glyph later inside the same tab", scr_glyph_at(&scr, gl, 5, 3), ' ');
    check("glyph after the tab", scr_glyph_at(&scr, gl, 5, 4), 'c');
    check("glyph at the last column", scr_glyph_at(&scr, gl, 5, 5), 'd');
    check("glyph past the end is a space", scr_glyph_at(&scr, gl, 5, 6), ' ');
    check("glyph on an empty line is a space", scr_glyph_at(&scr, NULL, 0, 3), ' ');

    /* ---------- the same line, arriving in two pieces ---------- */
    /* A line is two runs whenever the gap falls inside it. The cursor's own
     * line always is; a walker reading without moving the gap meets one more.
     * Splitting it must not change a single answer, and the place that breaks
     * is a tab: its width depends on the column it lands in, so a second piece
     * measured from zero and added on gives the wrong number. */
    for (int cut = 0; cut <= 5; cut++) {
        int wrong = 0;
        for (int col = 0; col <= 7; col++) {
            const char whole = scr_glyph_at(&scr, gl, 5, col);
            const char split = scr_glyph_at_split(&scr, gl, cut,
                                                  gl + cut, 5 - cut, col);
            if (whole != split) {
                wrong = 100 + col;
                break;
            }
        }
        for (int n = 0; n <= 5 && wrong == 0; n++) {
            const int whole = scr_column_of(&scr, gl, n);
            const int lead = n < cut ? n : cut;
            const int rest = n - lead;
            const int split = scr_column_of_split(&scr, gl, lead,
                                                  gl + cut, rest);
            if (whole != split) {
                wrong = 200 + n;
            }
        }
        check("a line cut in two reads the same as one piece", wrong, 0);
    }

    /* The case a wrong implementation gets right by accident: cut the line so
     * the tab is in the first piece and the text after it in the second, then
     * ask for the column of the whole thing. Measuring the pieces apart and
     * adding gives 2 + 2 = 4; carrying the column through gives 4 + 2 = 6. */
    check("a tab in the first piece still pushes the second along",
          scr_column_of_split(&scr, gl, 3, gl + 3, 2), 6);
    check("  and a glyph after it is found in the second piece",
          scr_glyph_at_split(&scr, gl, 3, gl + 3, 2, 4), 'c');
    check("  with the tab's own columns still spaces",
          scr_glyph_at_split(&scr, gl, 3, gl + 3, 2, 3), ' ');

    /* A selection column is the width of everything before the byte it starts
     * at, so the same carry has to survive clipping the line short. Tested
     * against every cut of the line and every byte count, because the two
     * clips interact: the one the caller asks for and the one the split
     * imposes. */
    for (int cut = 0; cut <= 5; cut++) {
        int wrong = 0;
        for (int n = 0; n <= 7 && wrong == 0; n++) {
            const int want = scr_column_of(&scr, gl, n < 5 ? n : 5);
            const int got = scr_column_of_n(&scr, gl, cut, gl + cut,
                                            5 - cut, n);
            if (want != got) {
                wrong = cut * 10 + n;
            }
        }
        check("  and clipping it short measures the same either way", wrong, 0);
    }
    check("a byte count past the end measures the whole line",
          scr_column_of_n(&scr, gl, 3, gl + 3, 2, 99),
          scr_column_of(&scr, gl, 5));
    check("a negative one measures none of it",
          scr_column_of_n(&scr, gl, 3, gl + 3, 2, -1), 0);

    /* An empty piece on either side is the contiguous case, which is how every
     * single-run caller reaches these. */
    check("an empty prefix is the whole line",
          scr_column_of_split(&scr, NULL, 0, gl, 5),
          scr_column_of(&scr, gl, 5));
    check("an empty suffix is too",
          scr_column_of_split(&scr, gl, 5, NULL, 0),
          scr_column_of(&scr, gl, 5));

    /* ---------- holding a column across differently shaped lines ---------- */
    /* This is what cmd_up/cmd_down do: column on the old line -> byte on the new. */
    char from[] = "\tx";        /* 'x' is byte 1, column 4 */
    char to[]   = "abcdef";     /* column 4 is byte 4 */
    const int col = scr_column_of(&scr, from, 1);
    check("column of byte 1 on a tabbed line", col, 4);
    check("same column is byte 4 on a plain line", scr_byte_at(&scr, to, 6, col), 4);

    char to2[] = "\t\tz";       /* column 4 is byte 1 (start of the second tab) */
    check("and byte 1 on a doubly-tabbed line", scr_byte_at(&scr, to2, 3, col), 1);

    /* tb_goto_offset must land exactly, in both directions. */
    stub_discard_output();
    text_buffer nav;
    if (!tb_init(&nav, 1, NULL)) {
        fprintf(stderr, "nav init failed\n");

        return 2;
    }
    for (const char* p = "\tabc\tdef"; *p; p++) {
        tb_put(&nav, *p);
    }
    tb_home(&nav);
    check("goto_offset forward", tb_goto_offset(&nav, 4) == '\t' ? 1 : 0, 1);
    check("  reports the right column", tb_xpos(&nav), 5);
    check("goto_offset backward", tb_goto_offset(&nav, 1) == 'a' ? 1 : 0, 1);
    check("  and back to the start", tb_goto_offset(&nav, 0) == '\t' ? 1 : 0, 1);
    tb_destroy(&nav);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
