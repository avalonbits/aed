/*
 * Host tests for selecting text: the state machine that decides when a
 * selection starts and ends, and the highlight that shows it.
 *
 * Selecting is deliberately modeless. Shift with a motion key starts a
 * selection or extends one; every other key ends it. So the two things worth
 * pinning are that no key can leave the editor stuck in a mode the user cannot
 * see, and that the highlight covers exactly the columns the selection covers
 * -- which is not the same as the bytes, because a tab is one byte and several
 * columns.
 *
 * The highlight is read back off the VDU stream, since that is the only place
 * it exists: set_colours emits a foreground and a background, and the
 * background is offset by 128 the way the VDP distinguishes them.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "cmd_ops.h"
#include "screen.h"
#include "text_buffer.h"
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

static FILE* cap;
static long mark;

static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

static int cap_read(char* out, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    long n = end - mark;
    if (n < 0) {
        n = 0;
    }
    if (n > max) {
        n = max;
    }
    fseek(stdout, mark, SEEK_SET);
    const size_t got = fread(out, 1, (size_t) n, stdout);
    fseek(stdout, end, SEEK_SET);

    return (int) got;
}

/* The VDU stream a row paint produces, rendered as one character per screen
 * column: '-' where the normal scheme is in force and '#' where the colours are
 * reversed. That is what a highlight looks like from outside, and it lines up
 * under the text so a wrong column is obvious.
 *
 * vdp_set_text_colour writes 17 (VDU 17) then the colour; a pair of them is one
 * set_colours call, and a foreground equal to the screen's background means the
 * scheme is reversed. */
static const char* painted(screen* scr, int cols) {
    static char raw[4096];
    const int n = cap_read(raw, sizeof(raw));
    static char out[256];
    int col = 0;
    bool inverted = false;
    for (int i = 0; i < n && col < cols && col < (int) sizeof(out) - 1; i++) {
        if (raw[i] == 17 && i + 1 < n) {
            /* One set_colours emits two of these: the foreground, then the
             * background offset by 128. Only the foreground says which way
             * round the scheme is -- reading the background byte as well would
             * undo the answer immediately. */
            const unsigned char c = (unsigned char) raw[i + 1];
            if (c < 128) {
                inverted = c == (unsigned char) scr->bg_;
            }
            i++;
            continue;
        }
        if (raw[i] == 31 && i + 2 < n) {
            i += 2;             /* cursor tab: x, y */
            continue;
        }
        if ((unsigned char) raw[i] < 32) {
            continue;           /* any other control byte */
        }
        out[col++] = inverted ? '#' : '-';
    }
    out[col] = 0;

    return out;
}

static void check_paint(const char* name, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-52s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s\n        got  %s\n        want %s\n",
                name, got, want);
        failures++;
    }
}

/* The document line currently at the top of the screen, worked out the way the
 * editor does: from the cursor's line and its screen row. */
static int top_line_of(editor* ed) {
    return tb_ypos(&ed->buf_) - (ed->scr_.currY_ - ed->scr_.topY_);
}

/* How many rows a repaint actually drew. Every row is padded out to the full
 * width, so the printable characters divide evenly by it -- enough to tell
 * "just this row" from "the whole area" without depending on the contents.
 * The colour bytes are skipped: a background is offset by 128 and would
 * otherwise count as a character. */
/* Printable characters that reached the stream, colour changes excluded. A row
 * is `cols` of them, so this measures a partial repaint that painted_rows
 * rounds away. */
static int painted_chars(void) {
    static char raw[65536];
    const int n = cap_read(raw, sizeof(raw));
    int printable = 0;
    for (int i = 0; i < n; i++) {
        if (raw[i] == 17 && i + 1 < n) {
            i++;
            continue;
        }
        if ((unsigned char) raw[i] >= 32) {
            printable++;
        }
    }

    return printable;
}

static int painted_rows(screen* scr, int cols) {
    (void) scr;
    static char raw[65536];
    const int n = cap_read(raw, sizeof(raw));
    int printable = 0;
    for (int i = 0; i < n; i++) {
        if (raw[i] == 17 && i + 1 < n) {
            i++;
            continue;
        }
        if ((unsigned char) raw[i] >= 32) {
            printable++;
        }
    }

    return printable / cols;
}

static key_command press(VKey vkey, char mods) {
    key_command kc;
    kc.cmd = NULL;
    kc.k.key = 0;
    kc.k.vkey = vkey;
    kc.mods = mods;

    return kc;
}

int main(void) {
    /* --- which keys move the cursor --- */
    check("LEFT moves", ed_is_motion(VK_LEFT) ? 1 : 0, 1);
    check("RIGHT moves", ed_is_motion(VK_RIGHT) ? 1 : 0, 1);
    check("UP moves", ed_is_motion(VK_UP) ? 1 : 0, 1);
    check("DOWN moves", ed_is_motion(VK_DOWN) ? 1 : 0, 1);
    check("HOME moves", ed_is_motion(VK_HOME) ? 1 : 0, 1);
    check("END moves", ed_is_motion(VK_END) ? 1 : 0, 1);
    check("PAGE UP moves", ed_is_motion(VK_PAGEUP) ? 1 : 0, 1);
    check("PAGE DOWN moves", ed_is_motion(VK_PAGEDOWN) ? 1 : 0, 1);
    check("the keypad arrows move too", ed_is_motion(VK_KP_LEFT) ? 1 : 0, 1);
    /* Anything that changes the document does not. */
    check("a letter does not move", ed_is_motion(VK_a) ? 1 : 0, 0);
    check("RETURN does not move", ed_is_motion(VK_RETURN) ? 1 : 0, 0);
    check("DELETE does not move", ed_is_motion(VK_DELETE) ? 1 : 0, 0);
    check("BACKSPACE does not move", ed_is_motion(VK_BACKSPACE) ? 1 : 0, 0);
    check("ESCAPE does not move", ed_is_motion(VK_ESCAPE) ? 1 : 0, 0);

    /* --- the modifier keys are not keystrokes --- */
    check("shift is a modifier", ed_is_modifier(VK_LSHIFT) ? 1 : 0, 1);
    check("  either one", ed_is_modifier(VK_RSHIFT) ? 1 : 0, 1);
    check("control is", ed_is_modifier(VK_LCTRL) ? 1 : 0, 1);
    check("alt is", ed_is_modifier(VK_RALT) ? 1 : 0, 1);
    check("an arrow is not", ed_is_modifier(VK_RIGHT) ? 1 : 0, 0);
    check("a letter is not", ed_is_modifier(VK_a) ? 1 : 0, 0);

    /* The bits MOS actually reports. These were wrong in the header this file
     * inherited them from -- MOD_SHFT named a bit no key sets, so shift was
     * invisible, and MOD_ALT overlapped the bit that really is shift. Measured
     * with a probe on MOS 3.0.2 and 2.2.3. */
    check("control is bit 0", MOD_CTRL, 0x01);
    check("shift is bit 1", MOD_SHFT, 0x02);
    check("alt is bit 2", MOD_ALT, 0x04);
    check("and none of them overlap", MOD_CTRL & MOD_SHFT & MOD_ALT, 0);
    check("shift is not alt", MOD_SHFT & MOD_ALT, 0);

    stub_discard_output();

    /* --- starting, extending and ending a selection --- */
    static const char DOC[] = "hello world\r\nsecond line\r\nthird\r\n";
    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    editor ed;
    check("the editor starts", ed_init(&ed, 8, "doc.txt") != NULL, 1);
    check("  with nothing selected", ed.selecting_ ? 1 : 0, 0);

    /* An unshifted motion is just a motion. */
    check("plain RIGHT selects nothing",
          ed_selection_for(&ed, press(VK_RIGHT, 0)), SEL_NONE);
    check("  and still nothing is selected", ed.selecting_ ? 1 : 0, 0);

    /* Shift with one starts it, anchored where the cursor is now. */
    tb_seek(&ed.buf_, (tb_pos){2, 3});
    check("SHIFT+RIGHT starts a selection",
          ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT)), SEL_EXTEND);
    check("  and it is selecting", ed.selecting_ ? 1 : 0, 1);
    check("  anchored on the line the cursor was on", ed.anchor_.line, 2);
    check("  at the byte it was on", ed.anchor_.x, 3);

    /* Extending must not move the anchor -- that is the whole point of one. */
    tb_seek(&ed.buf_, (tb_pos){2, 7});
    check("SHIFT+RIGHT again extends",
          ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT)), SEL_EXTEND);
    check("  the anchor has not moved", ed.anchor_.x, 3);
    check("  nor its line", ed.anchor_.line, 2);

    /* Every other motion key extends it too, including with CTRL held. */
    check("SHIFT+DOWN extends",
          ed_selection_for(&ed, press(VK_DOWN, MOD_SHFT)), SEL_EXTEND);
    check("SHIFT+END extends",
          ed_selection_for(&ed, press(VK_END, MOD_SHFT)), SEL_EXTEND);
    check("SHIFT+PAGE DOWN extends",
          ed_selection_for(&ed, press(VK_PAGEDOWN, MOD_SHFT)), SEL_EXTEND);
    check("CTRL+SHIFT+RIGHT extends",
          ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT | MOD_CTRL)), SEL_EXTEND);
    check("  and the anchor survived all of it", ed.anchor_.x, 3);

    /* Anything else ends it. There is no key for leaving select mode because
     * there is no mode to leave: that is what keeps it from getting stuck. */
    check("an unshifted motion ends it",
          ed_selection_for(&ed, press(VK_LEFT, 0)), SEL_DROP);
    check("  and nothing is selected", ed.selecting_ ? 1 : 0, 0);
    check("  ending it again is a no-op",
          ed_selection_for(&ed, press(VK_LEFT, 0)), SEL_NONE);

    /* Holding shift down reports the shift key itself, once, before the arrow.
     * That event must pass straight through: treating it as an ordinary key
     * would end the selection it is in the middle of making. */
    ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT));
    check("  a selection is under way", ed.selecting_ ? 1 : 0, 1);
    const int held_anchor = ed.anchor_.x;
    check("the shift key itself changes nothing",
          ed_selection_for(&ed, press(VK_LSHIFT, 0)), SEL_NONE);
    check("  and the selection is still under way", ed.selecting_ ? 1 : 0, 1);
    check("nor does releasing and pressing control",
          ed_selection_for(&ed, press(VK_LCTRL, 0)), SEL_NONE);
    check("  still selecting", ed.selecting_ ? 1 : 0, 1);
    check("  and the next shifted motion still extends it",
          ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT)), SEL_EXTEND);
    check("  with the anchor where it started", ed.anchor_.x, held_anchor);

    ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT));
    check("shift on a key that is not a motion ends it",
          ed_selection_for(&ed, press(VK_a, MOD_SHFT)), SEL_DROP);

    /* A key that changes the document does not merely end the selection -- it
     * takes its place, which is a different answer. */
    ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT));
    check("RETURN replaces the selection rather than ending it",
          ed_selection_for(&ed, press(VK_RETURN, 0)), SEL_REPLACE);
    ed.selecting_ = false;

    ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT));
    check("so does a key with no modifier that only moves",
          ed_selection_for(&ed, press(VK_F1, 0)), SEL_DROP);

    ed_selection_for(&ed, press(VK_RIGHT, MOD_SHFT));
    check("so does ESCAPE",
          ed_selection_for(&ed, press(VK_ESCAPE, 0)), SEL_DROP);
    check("  leaving nothing selected", ed.selecting_ ? 1 : 0, 0);

    /* --- the highlight --- */
    cap = freopen("/tmp/aed_sel_capture", "w+", stdout);
    if (cap == NULL) {
        fprintf(stderr, "could not capture stdout\n");

        return 1;
    }
    stub_emit_colours(1);
    screen* scr = &ed.scr_;
    const int cols = scr->cols_;
    /* Pinned so "reversed" is unambiguous: with the two colours equal, every
     * column would read as highlighted and the tests would pass on nothing. */
    scr_set_scheme(scr, 15, 0);
    check("the two colours differ", scr_fg(scr) != scr_bg(scr), 1);

    /* No selection: the row paints plainly from end to end. */
    ed.selecting_ = false;
    cap_start();
    scr_write_line(scr, scr->topY_, "hello", 5);
    {
        static char want[128];
        memset(want, '-', (size_t) cols);
        want[cols] = 0;
        check_paint("a row with no selection is all plain",
                    painted(scr, cols), want);
    }

    /* A span in the middle, given directly in columns. */
    cap_start();
    scr_write_line_sel(scr, scr->topY_, "hello world", 11, 2, 5);
    {
        static char want[128];
        memset(want, '-', (size_t) cols);
        want[2] = want[3] = want[4] = '#';
        want[cols] = 0;
        check_paint("a span in the middle is reversed", painted(scr, cols), want);
    }

    /* An empty span highlights nothing, which is what an unselected row asks
     * for -- and a backwards one must not paint the whole line. */
    cap_start();
    scr_write_line_sel(scr, scr->topY_, "hello world", 11, 4, 4);
    {
        static char want[128];
        memset(want, '-', (size_t) cols);
        want[cols] = 0;
        check_paint("an empty span highlights nothing", painted(scr, cols), want);
    }
    cap_start();
    scr_write_line_sel(scr, scr->topY_, "hello world", 11, 6, 2);
    {
        static char want[128];
        memset(want, '-', (size_t) cols);
        want[cols] = 0;
        check_paint("a backwards span highlights nothing",
                    painted(scr, cols), want);
    }

    /* Past the end of the text: the padding is highlighted too, which is how a
     * selected line break shows up as anything at all. */
    cap_start();
    scr_write_line_sel(scr, scr->topY_, "abc", 3, 1, 5);
    {
        static char want[128];
        memset(want, '-', (size_t) cols);
        want[1] = want[2] = want[3] = want[4] = '#';
        want[cols] = 0;
        check_paint("the highlight runs past the end of the text",
                    painted(scr, cols), want);
    }

    /* A tab is one byte and several columns, so the highlight has to be given
     * columns. scr_column_of is what turns one into the other. */
    scr_set_tab_size(scr, 4);
    check("a tab before the text is four columns wide",
          scr_column_of(scr, "\tab", 1), 4);
    cap_start();
    scr_write_line_sel(scr, scr->topY_, "\tab", 3,
                       0, scr_column_of(scr, "\tab", 3));
    {
        static char want[128];
        memset(want, '-', (size_t) cols);
        for (int i = 0; i < 6; i++) {     /* four for the tab, then 'a' and 'b' */
            want[i] = '#';
        }
        want[cols] = 0;
        check_paint("selecting a tab highlights all of its width",
                    painted(scr, cols), want);
    }

    /* And the row after a highlighted one must come back plain: the colours are
     * put back at the end of every row, not left for the next one to inherit. */
    cap_start();
    scr_write_line_sel(scr, scr->topY_, "abc", 3, 0, 3);
    scr_write_line(scr, (char)(scr->topY_ + 1), "def", 3);
    {
        static char want[256];
        memset(want, '-', (size_t) cols * 2);
        want[0] = want[1] = want[2] = '#';
        want[cols * 2] = 0;
        check_paint("the next row is not left highlighted",
                    painted(scr, cols * 2), want);
    }

    /* A highlight running to the very last column, so there is no padding after
     * it. The padding loop turns the colours back itself whenever there is any,
     * which hides a missing restore at the end of the row -- this is the case
     * where only the explicit one can save the next row. */
    static char wide[256];
    memset(wide, 'w', (size_t) cols);
    cap_start();
    scr_write_line_sel(scr, scr->topY_, wide, cols, 0, cols);
    scr_write_line(scr, (char)(scr->topY_ + 1), "plain", 5);
    {
        static char want[256];
        memset(want, '#', (size_t) cols);
        memset(want + cols, '-', (size_t) cols);
        want[cols * 2] = 0;
        check_paint("a full-width highlight still ends with the row",
                    painted(scr, cols * 2), want);
    }

    /* --- the selection mapped onto real rows --- */
    /* Everything above hands the view its columns directly. This drives the
     * whole path instead: two document positions in, painted rows out, with the
     * controller working out which bytes of which line each row shows. That is
     * where a tab stops being one byte and starts being several columns. */
    scr_set_tab_size(scr, 4);
    ed.selecting_ = true;
    ed.anchor_ = (tb_pos){1, 2};              /* "hello world", from the 'l' */
    tb_seek(&ed.buf_, (tb_pos){2, 4});        /* to "seco|nd line" */
    scr->currY_ = (char)(scr->topY_ + 1);     /* the cursor's row, so row 1 is line 1 */

    cap_start();
    cmd_repaint_rows(&ed, scr->topY_, (char)(scr->topY_ + 2));
    {
        static char want[512];
        memset(want, '-', (size_t) cols * 3);
        /* Line 1 is selected from column 2 to the end of its text, and one
         * column past it: the line break is inside the selection and that cell
         * is the only way to see it. "hello world" is 11 columns. */
        for (int i = 2; i <= 11; i++) {
            want[i] = '#';
        }
        /* Line 2 is selected from its start up to the cursor. */
        for (int i = 0; i < 4; i++) {
            want[cols + i] = '#';
        }
        /* Line 3 is outside the selection entirely. */
        want[cols * 3] = 0;
        check_paint("a selection spanning two lines paints both",
                    painted(scr, cols * 3), want);
    }

    /* Selecting upwards puts the cursor before the anchor. It is the same span
     * and must paint identically -- the user does not care which end they
     * started from. */
    ed.anchor_ = (tb_pos){2, 4};
    tb_seek(&ed.buf_, (tb_pos){1, 2});
    scr->currY_ = scr->topY_;
    cap_start();
    cmd_repaint_rows(&ed, scr->topY_, (char)(scr->topY_ + 2));
    {
        static char want[512];
        memset(want, '-', (size_t) cols * 3);
        for (int i = 2; i <= 11; i++) {
            want[i] = '#';
        }
        for (int i = 0; i < 4; i++) {
            want[cols + i] = '#';
        }
        want[cols * 3] = 0;
        check_paint("a selection made upwards paints the same",
                    painted(scr, cols * 3), want);
    }

    /* And with no selection under way the same rows come back plain. This is
     * the repaint that runs when a selection is dropped, so it has to actually
     * remove the highlight rather than leave it where it was. */
    ed.selecting_ = false;
    cap_start();
    cmd_repaint_rows(&ed, scr->topY_, (char)(scr->topY_ + 2));
    {
        static char want[512];
        memset(want, '-', (size_t) cols * 3);
        want[cols * 3] = 0;
        check_paint("dropping the selection repaints it away",
                    painted(scr, cols * 3), want);
    }
    ed.selecting_ = true;

    /* The same, with a tab in the line: the highlight has to stop at the right
     * column, not the right byte. */
    static const char TABBED[] = "\tabc\r\nsecond\r\n";
    stub_file_reset();
    stub_file_set_content(TABBED, (int) sizeof(TABBED) - 1);
    editor tabbed;
    check("an editor with a tab in it", ed_init(&tabbed, 8, "t.txt") != NULL, 1);
    screen* tscr = &tabbed.scr_;
    scr_set_scheme(tscr, 15, 0);
    scr_set_tab_size(tscr, 4);
    tabbed.selecting_ = true;
    tabbed.anchor_ = (tb_pos){1, 0};          /* the whole tab */
    tb_seek(&tabbed.buf_, (tb_pos){1, 2});    /* and one character after it */
    tscr->currY_ = tscr->topY_;

    cap_start();
    cmd_repaint_rows(&tabbed, tscr->topY_, tscr->topY_);
    {
        static char want[256];
        memset(want, '-', (size_t) cols);
        /* Two bytes selected -- the tab and 'a' -- but five columns. */
        for (int i = 0; i < 5; i++) {
            want[i] = '#';
        }
        want[cols] = 0;
        check_paint("a selected tab covers its columns, not its bytes",
                    painted(tscr, cols), want);
    }
    ed_destroy(&tabbed);

    /* --- how much gets repainted --- */
    /* Repainting only the rows the cursor moved between is what keeps a
     * keystroke cheap, but it is only right while the rest of the screen still
     * shows what it showed before. Three things break that. */
    ed.selecting_ = true;
    ed.anchor_ = (tb_pos){1, 0};
    tb_seek(&ed.buf_, (tb_pos){2, 3});
    scr->currY_ = (char)(scr->topY_ + 1);
    scr->originX_ = 0;

    /* The cursor stayed on its row and nothing scrolled, so neither the rows
     * around it nor the columns either side of the move have changed. Only the
     * columns the cursor crossed are sent -- plus the one it left, which has to
     * go back to being ordinary text. */
    scr->currX_ = 10;
    cap_start();
    ed_selection_repaint(&ed, SEL_EXTEND, scr->currY_, 4,
                         top_line_of(&ed), scr->originX_);
    const int span = painted_chars();
    check("an extend within a row paints a span, not a row", span * 4 < cols, 1);
    check("...and it does paint it", span > 0, 1);

    /* Backwards over the same columns costs the same: the span is the two
     * positions, whichever order they came in. */
    scr->currX_ = 4;
    cap_start();
    ed_selection_repaint(&ed, SEL_EXTEND, scr->currY_, 10,
                         top_line_of(&ed), scr->originX_);
    check("shrinking one paints the same columns", painted_chars(), span);

    /* The cell the cursor was in has to be repainted even when it did not move
     * -- it is showing a cursor, and the character under it has to come back.
     * So the span is the columns crossed *plus one*, and a move of nothing is
     * still a column. */
    scr->currX_ = 10;
    cap_start();
    ed_selection_repaint(&ed, SEL_EXTEND, scr->currY_, 10,
                         top_line_of(&ed), scr->originX_);
    /* One character for the span, one for the cursor drawn over it. Without
     * the span the cursor is all that is sent, and the cell keeps whatever it
     * was showing underneath. */
    check("a move of no columns still repaints the one it is on",
          painted_chars(), 2);

    /* And a longer move costs more, which is the point: the bill follows the
     * change rather than the width of the screen. */
    scr->currX_ = 30;
    cap_start();
    ed_selection_repaint(&ed, SEL_EXTEND, scr->currY_, 4,
                         top_line_of(&ed), scr->originX_);
    check("a longer move paints more", painted_chars() > span, 1);
    scr->currX_ = 10;

    /* Changing rows without scrolling still costs both rows in full: the
     * highlight on the row being left has to be taken off all of it. */
    cap_start();
    ed_selection_repaint(&ed, SEL_EXTEND, (char)(scr->currY_ - 1), 4,
                         top_line_of(&ed), scr->originX_);
    check("crossing rows repaints both", painted_rows(scr, cols), 2);

    /* A horizontal scroll moves every row at once -- originX_ is screen-wide --
     * so the rows not repainted are left showing their old columns. This is the
     * same trap as the stale rows in #60, one layer up. */
    cap_start();
    ed_selection_repaint(&ed, SEL_EXTEND, scr->currY_, scr->currX_,
                         top_line_of(&ed), scr->originX_ + 4);
    check("a horizontal scroll repaints the whole text area",
          painted_rows(scr, cols) > 1, 1);

    /* So does a vertical one. */
    cap_start();
    ed_selection_repaint(&ed, SEL_EXTEND, scr->currY_, scr->currX_,
                         top_line_of(&ed) + 1, scr->originX_);
    check("a vertical scroll does too",
          painted_rows(scr, cols) > 1, 1);

    /* And dropping a selection, since the highlight could be anywhere. */
    cap_start();
    ed_selection_repaint(&ed, SEL_DROP, scr->currY_, scr->currX_,
                         top_line_of(&ed), scr->originX_);
    check("dropping one does too", painted_rows(scr, cols) > 1, 1);

    /* With nothing selected there is nothing to repaint at all. */
    cap_start();
    ed_selection_repaint(&ed, SEL_NONE, scr->currY_, scr->currX_,
                         top_line_of(&ed), scr->originX_);
    check("and with no selection nothing is painted",
          painted_rows(scr, cols), 0);

    stub_emit_colours(0);
    ed_destroy(&ed);

    /* --- a selection belongs to the document it was made in --- */
    /* Opening another file renumbers every line, so an anchor kept across it
     * would highlight an arbitrary part of the new document. */
    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    editor op;
    check("an editor to open from", ed_init(&op, 8, "doc.txt") != NULL, 1);
    check("with a selection under way",
          ed_selection_for(&op, press(VK_RIGHT, MOD_SHFT)), SEL_EXTEND);
    check("  which is live", op.selecting_ ? 1 : 0, 1);

    stub_key keys[32];
    int n = 0;
    for (int i = 0; i < 7; i++) {          /* backspace over "doc.txt" */
        keys[n].ch = 0x7F; keys[n].vk = VK_BACKSPACE; keys[n++].mods = 0;
    }
    for (const char* q = "other.txt"; *q; q++) {
        keys[n].ch = *q; keys[n].vk = VK_NONE; keys[n++].mods = 0;
    }
    keys[n].ch = 0; keys[n].vk = VK_RETURN; keys[n++].mods = 0;

    static const char OTHER[] = "different\r\n";
    stub_file_reset();
    stub_file_set_content(OTHER, (int) sizeof(OTHER) - 1);
    stub_set_keys(keys, n);
    cmd_open(&op);
    check("opening a file dropped the selection", op.selecting_ ? 1 : 0, 0);
    check("  and the file did open", strcmp(tb_fname(&op.buf_), "other.txt"), 0);
    stub_set_keys(NULL, 0);
    ed_destroy(&op);

    /* --- the widest screen there is --- */
    /* Mode 19 is 1024x768, which MOS reports as 128 columns. Held in a signed
     * char that is -128, and every width calculation built on it goes negative:
     * the paint loop stops before it starts and the screen renders as a few
     * characters in the corner. The cursor and the document are fine, which is
     * what makes it look like a drawing bug rather than a type. */
    stub_set_screen(128, 96);
    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    editor widest;
    check("an editor on the widest mode", ed_init(&widest, 8, "wide.txt") != NULL, 1);
    /* 127, not the 128 the mode reports: the rightmost column is deliberately
     * unused. What this test is really for is that the width stays positive
     * and sane on the widest mode -- it used to be -128 in a signed char. */
    check("  reports its usable columns", widest.scr_.cols_, 127);
    check("  and all its rows", widest.scr_.rows_, 96);
    check("  with a positive width", widest.scr_.cols_ > 0, 1);

    screen* wscr = &widest.scr_;
    scr_set_scheme(wscr, 15, 0);
    stub_emit_colours(1);
    cap_start();
    scr_write_line_sel(wscr, wscr->topY_, "hello world", 11, 2, 5);
    {
        static char want[256];
        memset(want, '-', 127);
        want[2] = want[3] = want[4] = '#';
        want[127] = 0;
        check_paint("a row paints its full usable width",
                    painted(wscr, 127), want);
    }
    stub_emit_colours(0);
    ed_destroy(&widest);
    stub_set_screen(80, 25);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
