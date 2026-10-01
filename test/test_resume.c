/*
 * Putting the screen back after the program it was lent to has finished.
 *
 * KISS saves the file, runs it, and the program it runs ends by putting the
 * screen mode back. A mode change resets the VDP's colours, font and scroll
 * protection even when it is the same mode, and the editor carried on drawing
 * as though they were its own: half the title bar in white on black, a black
 * border round the text, and -- with a font loaded -- a screen laid out for
 * rows the system font does not have. ed_resume sends them again and repaints.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "app.h"
#include "cmd_ops.h"
#include "editor.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static const app_context APP = {
    .name = "resume", .syntax_dir = "/g", .theme_dir = "/t", .font_dir = "/f",
};
static const ed_program PROG = { &APP, &ED_KEYS, NULL, NULL, NULL };

static editor ed;
static char text[2048];
static char font[256 * 16];

/* What the program the editor ran leaves behind: the system font's eight-row
 * cell on the same 480 pixels, and the mode's own colours. */
static void mode_change(void) {
    stub_set_cell(8, 8);
    stub_set_screen(80, 60);
    stub_colours_reset();
}

int main(void) {
    if (freopen("/tmp/aed_resume_capture", "w+", stdout) == NULL) {
        return 2;
    }
    stub_set_cell(8, 8);
    stub_set_screen(80, 60);
    stub_file_reset();

    int n = 0;
    for (int i = 1; i <= 80; i++) {
        n += sprintf(text + n, "line %d\r\n", i);
    }
    stub_file_add("prog.kiss", text, n);
    /* Capitals in rows 0 to 13 of a sixteen-row cell, as unscii-16 is. */
    for (int g = 0; g < 256; g++) {
        for (int r = 0; r <= 13; r++) {
            font[g * 16 + r] = (char) 0x18;
        }
    }
    stub_file_add("/f/u16.bin", font, (int) sizeof(font));

    check("an editor starts", ed_init_for(&ed, 8, "prog.kiss", &PROG) != NULL, 1);
    check("  with a sixteen row font", scr_load_font(&ed.scr_, "/f/u16.bin"), 1);
    ui_resize(&ed.ui_, ed.scr_.v_->bottomY_, ed.scr_.v_->cols_);
    scr_set_scheme(&ed.scr_, 0, 3);
    cmd_restore_after_modal(&ed, false);
    check("  thirty rows of it", ed.scr_.rows_, 30);

    /* --- the program ran and put the mode back --- */
    mode_change();
    ed_resume(&ed);
    check("the font is back, and its thirty rows", ed.scr_.rows_, 30);
    check("  the prompt is still on the row above the footer",
          ed.ui_.ypos_, ed.scr_.whole_.bottomY_);
    check("the screen is cleared in the editor's background",
          stub_last_bg(), 3);

    /* --- and one whose font did not come back --- */
    /* A VDP that takes the select but whose mode packet never reaches MOS
     * leaves the system font's sixty rows. The editor has to lay out for
     * those rather than the thirty it had. */
    stub_vdp_font_applies(0);
    mode_change();
    ed_resume(&ed);
    stub_vdp_font_applies(1);
    check("without the font the rows are the system font's", ed.scr_.rows_, 60);
    check("  the prompt moves to the new last row",
          ed.ui_.ypos_, ed.scr_.whole_.bottomY_);
    check("  and the cursor is inside the text area",
          ed.scr_.v_->currY_ >= ed.scr_.v_->topY_
              && ed.scr_.v_->currY_ < ed.scr_.v_->bottomY_, 1);

    /* --- a program that left a mode with fewer rows behind --- */
    /* The cursor can then sit below the new last row; it has to be placed
     * again rather than kept where the old screen had it. */
    tb_seek(&ed.doc_->buf_, (tb_pos) { .line = 70, .x = 0 });
    tb_settle(&ed.doc_->buf_);
    cmd_restore_after_modal(&ed, true);
    check("before: the cursor is low on the sixty row screen",
          ed.scr_.v_->currY_ >= 29, 1);
    stub_vdp_font_applies(0);
    stub_set_cell(8, 8);
    stub_set_screen(80, 30);
    stub_colours_reset();
    ed_resume(&ed);
    stub_vdp_font_applies(1);
    check("thirty rows now", ed.scr_.rows_, 30);
    check("  and the cursor is inside the smaller text area",
          ed.scr_.v_->currY_ >= ed.scr_.v_->topY_
              && ed.scr_.v_->currY_ < ed.scr_.v_->bottomY_, 1);
    check("  on the line it was on", tb_ypos(&ed.doc_->buf_), 70);

    ed_destroy(&ed);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
