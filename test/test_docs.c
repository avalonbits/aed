/*
 * An editor with more than one document open.
 *
 * The editor works on whichever document is current, in its view; a program
 * with several keeps them and a view for each, and switches with ed_doc_show.
 * These open a second document beside the editor's own, edit each, switch
 * between them, and check each keeps its own text, cursor and view, that the
 * theme follows the document on screen, that a document that will not open
 * changes nothing, and that closing one takes its scratch files with it.
 *
 * Only the UI's headers: this is linked against the core and the UI alone.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "app.h"
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

static const char C_CFG[] =
    "[syntax]\nname = C\nextensions = .c .h\n"
    "[match]\nstorage.type = words int\n";
static const char DARK[] = "[theme]\nname = dark\ncovers = 0 1 4\n";
static const char* const DIR_NAMES[] = { "c.cfg", "dark.cfg" };
static const unsigned DIR_SIZES[] = { 1, 1 };

static const app_context APP = {
    .name = "docs", .syntax_dir = "/g", .theme_dir = "/t", .font_dir = "/f",
};
static const ed_program PROG = { &APP, &ED_KEYS, NULL, NULL };

static editor ed;

static void type(char ch, VKey vkey) {
    const key_press kp = { ch, vkey, 0 };
    ed_handle(&ed, ed_translate(ed.keys_, kp));
}

/* Whether line 1 of `doc` starts with `want`. */
static int starts(document* doc, const char* want) {
    const tb_pos was = tb_tell(&doc->buf_);
    tb_seek(&doc->buf_, (tb_pos) { .line = 1, .x = 0 });
    const split_line sl = tb_curr_line(&doc->buf_);
    tb_seek(&doc->buf_, was);
    const int n = (int) strlen(want);

    return sl.ssz_ >= n && memcmp(sl.suffix_, want, (size_t) n) == 0;
}

int main(void) {
    stub_discard_output();
    stub_set_screen(80, 25);
    stub_file_reset();
    stub_set_dir(DIR_NAMES, DIR_SIZES, 2);
    stub_file_add("/g/c.cfg", C_CFG, (int) sizeof(C_CFG) - 1);
    stub_file_add("/t/dark.cfg", DARK, (int) sizeof(DARK) - 1);
    stub_file_add("home.txt", "home\r\n", 6);
    stub_file_add("b.c", "int b;\r\nline two\r\nline three\r\n", 30);

    check("an editor starts on its own document",
          ed_init_for(&ed, 8, "home.txt", &PROG) != NULL, 1);
    check("  which is current", ed.doc_ == &ed.home_ ? 1 : 0, 1);
    ed.scr_.colors_ = 16;       /* enough to colour a grammar in */

    static document b;
    static view vb;

    /* --- a second document opens beside it --- */
    {
        check("a second document opens",
              ed_doc_open(&ed, &b, &vb, 8, "b.c"), TB_OK);
        check("  and is current", ed.doc_ == &b ? 1 : 0, 1);
        check("  in its own view", ed.scr_.v_ == &vb ? 1 : 0, 1);
        check("  laid over the whole text area",
              vb.cols_ * 1000 + vb.topY_ * 100 + vb.bottomY_,
              ed.scr_.whole_.cols_ * 1000 + ed.scr_.whole_.topY_ * 100
              + ed.scr_.whole_.bottomY_);
        check("  holding its file", starts(&b, "int b;"), 1);
        check("  coloured by its grammar", ed.scr_.theme_ != NULL ? 1 : 0, 1);

        type('x', VK_x);
        check("typing goes into it", starts(&b, "xint b;"), 1);
        check("  and not into the editor's own", starts(&ed.home_, "home"), 1);
        type(0, VK_DOWN);
        type(0, VK_DOWN);
        check("its cursor moves in it", tb_ypos(&b.buf_), 3);
    }

    /* --- switching back and forth --- */
    {
        const char row_b = vb.currY_;
        ed_doc_show(&ed, &ed.home_, &ed.scr_.whole_);
        check("showing the editor's own makes it current",
              ed.doc_ == &ed.home_ ? 1 : 0, 1);
        check("  in its view", ed.scr_.v_ == &ed.scr_.whole_ ? 1 : 0, 1);
        check("  with its plain colours back", ed.scr_.theme_ == NULL ? 1 : 0, 1);
        type('y', VK_y);
        check("typing goes into it now", starts(&ed.home_, "yhome"), 1);
        check("  leaving the other as it was", starts(&b, "xint b;"), 1);

        ed_doc_show(&ed, &b, &vb);
        check("showing the other again makes it current", ed.doc_ == &b ? 1 : 0, 1);
        check("  its cursor where it was left", tb_ypos(&b.buf_), 3);
        check("  on the row it was on", vb.currY_, row_b);
        check("  and coloured again", ed.scr_.theme_ != NULL ? 1 : 0, 1);
    }

    /* --- a document that will not open changes nothing --- */
    {
        static document c;
        static view vc;
        /* There and unreadable: a name that is not there at all is a new,
         * empty document, which opens. */
        stub_file_add("c.txt", "c\r\n", 3);
        stub_file_fail_open(1);
        const tb_result got = ed_doc_open(&ed, &c, &vc, 8, "c.txt");
        stub_file_fail_open(0);
        check("a document that cannot open says so", got != TB_OK ? 1 : 0, 1);
        check("  and the current one still is", ed.doc_ == &b ? 1 : 0, 1);
        check("  in its view", ed.scr_.v_ == &vb ? 1 : 0, 1);
    }

    /* --- closing takes a paged document's scratch files --- */
    {
        static char big[20000];
        int n = 0;
        while (n + 40 < (int) sizeof(big)) {
            n += sprintf(big + n, "a line of a file too big for 4 KiB\r\n");
        }
        stub_file_add("big.txt", big, n);
        static document d;
        static view vd;
        check("a big file opens as another document",
              ed_doc_open(&ed, &d, &vd, 4, "big.txt"), TB_OK);
        check("  paged, with its scratch files",
              stub_file_exists("big.txt.aedh") && stub_file_exists("big.txt.aedt"), 1);
        ed_doc_show(&ed, &b, &vb);
        ed_doc_close(&d);
        check("closing it takes them away",
              stub_file_exists("big.txt.aedh") || stub_file_exists("big.txt.aedt"), 0);
        check("  and leaves the others open", starts(&b, "xint b;"), 1);
    }

    /* Taken down with another document current: the editor takes its own
     * with it, and the program closes the one it opened -- once each. */
    check("the other document is current as the editor goes",
          ed.doc_ == &b ? 1 : 0, 1);
    ed_destroy(&ed);
    ed_doc_close(&b);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
