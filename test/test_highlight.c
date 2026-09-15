/*
 * Choosing a grammar and a theme for the document on screen.
 *
 * Everything below step 3 built was machinery with nothing calling it. This is
 * the part that makes it show: a file's name picks the grammar, the background
 * in force picks the theme, and a file no grammar claims puts the user's own
 * colours back.
 *
 * The last of those is the rule that matters most, and it is the one a user
 * would notice breaking: a theme is a view of a document, and what the editor
 * is set to is what aed.cfg keeps.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "cmd_ops.h"

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
    "[match]\ncomment.block = span '/*' '*/' multiline\n"
    "storage.type = words int\n";

static const char ASM_CFG[] =
    "[syntax]\nname = asm\nextensions = .s .asm\n"
    "[match]\ncomment.line = eol ';'\n";

/* Says nothing about fg and bg, so it colours tokens and moves no pair. */
static const char DARK[] =
    "[theme]\nname = dark\ncovers = 0 1 4\n"
    "[colours]\ncomment = 8\ntype = 14\n";

/* Asks for its own pair, which is the case the base/active rule is about. */
static const char BOLD[] =
    "[theme]\nname = bold\ncovers = 2\nfg = 11\nbg = 2\n"
    "[colours]\ncomment = 8\n";

/*
 * One stubbed directory serves every ffs_dopen, so the grammar walk and the
 * theme walk both see these four names. Each looks for them under its own
 * directory and the ones that are not there simply fail to load, which is the
 * same thing that happens on a card with a stray file in it.
 */
static const char* const DIR_NAMES[] = {
    "c.cfg", "asm.cfg", "dark.cfg", "bold.cfg",
};
static const unsigned DIR_SIZES[] = { 1, 1, 1, 1 };

static void files(void) {
    stub_file_reset();
    stub_set_dir(DIR_NAMES, DIR_SIZES, 4);
    stub_file_add("/config/aed/syntax/c.cfg", C_CFG, (int) sizeof(C_CFG) - 1);
    stub_file_add("/config/aed/syntax/asm.cfg", ASM_CFG,
                  (int) sizeof(ASM_CFG) - 1);
    stub_file_add("/config/aed/themes/dark.cfg", DARK, (int) sizeof(DARK) - 1);
    stub_file_add("/config/aed/themes/bold.cfg", BOLD, (int) sizeof(BOLD) - 1);
}

static void setup(editor* ed, int bg) {
    memset(ed, 0, sizeof(*ed));
    ed->scr_.rows_ = 25;
    ed->scr_.cols_ = 20;
    ed->scr_.topY_ = 1;
    ed->scr_.bottomY_ = 24;
    ed->scr_.currY_ = 1;
    ed->scr_.colors_ = 16;
    ed->scr_.baseFg_ = 15;
    ed->scr_.baseBg_ = (char) bg;
    ed->scr_.fg_ = 15;
    ed->scr_.bg_ = (char) bg;
    if (!tb_init(&ed->buf_, 4, NULL)) {
        fprintf(stderr, "tb_init failed\n");
    }
}

/* Gives the buffer a name and a document, the way opening a file does. */
static void named_text(editor* ed, const char* fname, const char* text) {
    stub_file_add(fname, text, (int) strlen(text));
    tb_load(&ed->buf_, fname);
}

static void named(editor* ed, const char* fname) {
    named_text(ed, fname, "int x;\r\n");
}

/*
 * A colour change on the wire is VDU 17 then the colour -- see set_colours in
 * screen.c -- so finding one in what a row painted is how a test sees a colour
 * without a screen to look at.
 */
static int has_colour(const char* b, int n, int c) {
    for (int i = 0; i + 1 < n; i++) {
        if ((unsigned char) b[i] == 17
                && (unsigned char) b[i + 1] == (unsigned char) c) {
            return 1;
        }
    }

    return 0;
}

static long mark = 0;

static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

static int cap_read(char* buf, int max) {
    fflush(stdout);
    const long end = ftell(stdout);
    int n = (int)(end - mark);
    if (n > max) {
        n = max;
    }
    FILE* r = fopen("/tmp/aed_highlight_capture", "rb");
    if (r == NULL) {
        return -1;
    }
    fseek(r, mark, SEEK_SET);
    n = (int) fread(buf, 1, (size_t) n, r);
    fclose(r);

    return n;
}

int main(void) {
    if (freopen("/tmp/aed_highlight_capture", "w+", stdout) == NULL) {
        fprintf(stderr, "capture failed\n");

        return 2;
    }
    static editor ed;
    static char cap[8192];

    /* --- a C file gets the C grammar --- */
    {
        files();
        setup(&ed, 0);
        named(&ed, "/main.c");
        ed_pick_syntax(&ed);
        check("a .c file finds a grammar", ed.syn_.loaded ? 1 : 0, 1);
        check("  and it is the C one", strcmp(ed.syn_.name, "C") == 0 ? 1 : 0, 1);
        check("  with a theme to colour it", ed.scr_.theme_ != NULL ? 1 : 0, 1);
        check("    the one for this background",
              strcmp(ed.theme_.name, "dark") == 0 ? 1 : 0, 1);
        check("  and C is a language that crosses lines",
              syn_crosses_lines(&ed.syn_) ? 1 : 0, 1);
        tb_destroy(&ed.buf_);
    }

    /* --- an assembly file gets the other one --- */
    {
        files();
        setup(&ed, 0);
        named(&ed, "/boot.asm");
        ed_pick_syntax(&ed);
        check("an .asm file finds its own grammar",
              strcmp(ed.syn_.name, "asm") == 0 ? 1 : 0, 1);
        check("  and assembly crosses no lines",
              syn_crosses_lines(&ed.syn_) ? 1 : 0, 0);
        tb_destroy(&ed.buf_);
    }

    /* --- a file no grammar claims is painted plainly --- */
    {
        files();
        setup(&ed, 0);
        named(&ed, "/notes.txt");
        ed_pick_syntax(&ed);
        check("a .txt file finds no grammar", ed.syn_.loaded ? 1 : 0, 0);
        check("  so the screen is given no theme",
              ed.scr_.theme_ == NULL ? 1 : 0, 1);
        tb_destroy(&ed.buf_);
    }

    /* --- a grammar with no theme for this background colours nothing --- */
    {
        /*
         * Dividing a line into tokens and painting every one of them the same
         * colour is the work without the result, so the grammar goes too.
         */
        files();
        setup(&ed, 7);          /* no theme here covers 7 */
        named(&ed, "/main.c");
        ed_pick_syntax(&ed);
        check("a background no theme covers", ed.syn_.loaded ? 1 : 0, 0);
        check("  leaves the screen unthemed", ed.scr_.theme_ == NULL ? 1 : 0, 1);
        tb_destroy(&ed.buf_);
    }

    /* --- the pair a theme moves is the active one --- */
    {
        /*
         * The rule the whole feature was asked to respect: a theme may change
         * what the document is drawn in, and what the user chose survives it.
         * Opening a file with no grammar is what puts it back, so both halves
         * are checked here rather than only the first.
         */
        files();
        setup(&ed, 2);          /* bold.cfg covers 2, and wants 11 on 2 */
        named(&ed, "/main.c");
        ed_pick_syntax(&ed);
        check("a theme that asks for its own pair",
              strcmp(ed.theme_.name, "bold") == 0 ? 1 : 0, 1);
        check("  moves the active foreground", ed.scr_.fg_, 11);
        check("  and leaves the base alone", scr_base_fg(&ed.scr_), 15);
        check("    and the base background too", scr_base_bg(&ed.scr_), 2);
        tb_destroy(&ed.buf_);

        setup(&ed, 2);
        ed.scr_.fg_ = 11;       /* as the theme left it */
        named(&ed, "/notes.txt");
        ed_pick_syntax(&ed);
        check("opening a file with no grammar restores the user's own",
              ed.scr_.fg_, 15);
        tb_destroy(&ed.buf_);
    }

    /* --- a theme that says nothing about the pair moves nothing --- */
    {
        files();
        setup(&ed, 0);
        named(&ed, "/main.c");
        ed_pick_syntax(&ed);
        check("dark.cfg names no pair", ed.theme_.fg, -1);
        check("  so the document keeps its own colours", ed.scr_.fg_, 15);
        check("    and still colours its tokens",
              theme_colour(&ed.theme_, TOK_COMMENT), 8);
        tb_destroy(&ed.buf_);
    }

    /* --- what a painted row actually puts on the wire --- */
    {
        /*
         * The centre of the feature, and the one thing the rest of this file
         * cannot see: that the runs the lexer produced reach the paint and
         * come out as colour changes. Everything else here could pass with the
         * screen never being told anything.
         *
         * `int` is a type and gets 14; the block comment gets 8. Both are in
         * the theme above, and both have to appear in the bytes the row wrote.
         */
        files();
        setup(&ed, 0);
        // The stub keeps colour bytes out of the stream by default, because
        // colour 0 is a NUL and ends it for every test that reads it as text.
        // This is the one test that wants to see them.
        stub_emit_colours(1);
        named_text(&ed, "/main.c", "int x; /* hi\r\nplain\r\n");
        ed_pick_syntax(&ed);
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;

        cap_start();
        cmd_repaint_rows(&ed, 1, 1);
        int n = cap_read(cap, (int) sizeof(cap));
        check("painting a C row writes colour changes", n > 0 ? 1 : 0, 1);
        fprintf(stderr, "DEBUG loaded=%d theme=%p n=%d bytes:", ed.syn_.loaded,
                (void*) ed.scr_.theme_, n);
        for (int i = 0; i < n && i < 40; i++) {
            fprintf(stderr, " %d", (unsigned char) cap[i]);
        }
        fprintf(stderr, "\n");
        check("  the theme's type colour, for int",
              has_colour(cap, n, 14), 1);
        check("    and its comment colour", has_colour(cap, n, 8), 1);

        /*
         * The line above ended inside a block comment, so the row below it is
         * comment from its first column -- which is the multiline state
         * arriving through the paint rather than through a unit test.
         */
        cap_start();
        cmd_repaint_rows(&ed, 2, 2);
        n = cap_read(cap, (int) sizeof(cap));
        check("  the row below is still inside the comment",
              has_colour(cap, n, 8), 1);
        check("    and is not coloured as a type",
              has_colour(cap, n, 14), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- a full repaint works out what its top row is inside --- */
    {
        /*
         * cmd_repaint_rows can walk down from the top of the screen, because
         * the rows above it are on screen. A full repaint has no such thing:
         * the top row is wherever the view landed, and the only way to know
         * what it is inside is to read back. That is the lookback, and this is
         * the test that it is actually reached.
         *
         * The view starts on line 2, inside a comment opened on line 1 that is
         * off the top of the screen.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        named_text(&ed, "/main.c", "/* open\r\nstill\r\nmore\r\n");
        ed_pick_syntax(&ed);
        tb_home(&ed.buf_);
        tb_down(&ed.buf_);
        ed.scr_.currY_ = 1;             /* the cursor row is the top row */
        check("the view is on the second line", tb_ypos(&ed.buf_), 2);

        cap_start();
        cmd_show(&ed);
        const int n = cap_read(cap, (int) sizeof(cap));
        check("  a full repaint colours its top row as comment",
              has_colour(cap, n, 8), 1);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- editing and scrolling keep the colouring --- */
    {
        /*
         * Reported from a real session: opening a file coloured it, and then
         * typing left the characters just typed plain, scrolling lost the
         * colouring altogether, and pressing return brought it back.
         *
         * One cause for all three. Painting a row is not one function: a full
         * repaint goes through fill_screen, a range through cmd_repaint_rows,
         * and an edit or a scroll through the screen's own narrow paths --
         * scr_putc, scr_scroll_up_split, scr_down. Only the first two set the
         * row's colouring, and since a paint that sets none now gets none, the
         * rest painted plainly.
         *
         * Each of these drives a real command and looks for a token colour in
         * what reached the wire. `int` is a type and gets 14 from the theme
         * above; nothing else on these rows is 14.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static char doc[1024];
        int k = 0;
        for (int i = 0; i < 20; i++) {
            k += sprintf(doc + k, "int a%d;\r\n", i);
        }
        stub_file_add("/edit.c", doc, k);
        tb_load(&ed.buf_, "/edit.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 6;           /* a short screen, so the edge is near */
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;

        cap_start();
        cmd_show(&ed);
        int n = cap_read(cap, (int) sizeof(cap));
        check("opening colours the document", has_colour(cap, n, 14), 1);

        tb_end(&ed.buf_);
        const key ch = { 'x', VK_X };
        cap_start();
        cmd_putc(&ed, ch);
        n = cap_read(cap, (int) sizeof(cap));
        check("  typing keeps it", has_colour(cap, n, 14), 1);

        cap_start();
        cmd_bksp(&ed);
        n = cap_read(cap, (int) sizeof(cap));
        check("  backspace keeps it", has_colour(cap, n, 14), 1);

        cap_start();
        cmd_newl(&ed);
        n = cap_read(cap, (int) sizeof(cap));
        check("  return keeps it", has_colour(cap, n, 14), 1);

        tb_home(&ed.buf_);
        cap_start();
        cmd_del(&ed);
        n = cap_read(cap, (int) sizeof(cap));
        check("  delete keeps it", has_colour(cap, n, 14), 1);

        /* Down to the bottom row, then one more, which scrolls the view. */
        while (ed.scr_.currY_ < ed.scr_.bottomY_ - 1) {
            cmd_down(&ed);
        }
        cap_start();
        cmd_down(&ed);
        n = cap_read(cap, (int) sizeof(cap));
        check("  scrolling off the bottom keeps it",
              has_colour(cap, n, 14), 1);

        /*
         * Moving the cursor up repaints no row -- scr_up moves the cursor and
         * nothing else -- so there is nothing to colour and nothing to check.
         * What matters is that the next row painted is still right, which the
         * repaint below asks for directly.
         */
        cmd_up(&ed);
        cap_start();
        cmd_repaint_rows(&ed, ed.scr_.currY_, ed.scr_.currY_);
        n = cap_read(cap, (int) sizeof(cap));
        check("  and the row is still coloured afterwards",
              has_colour(cap, n, 14), 1);

        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- opening a comment recolours what is below it --- */
    {
        /*
         * The state a row leaves is what the row below begins in, so typing
         * the second character of a comment opener changes every row under it.
         * The edit paths repaint only their own row, so they compare what the
         * row now leaves against what the row below was painted with and carry
         * on down when they differ.
         *
         * Without that the screen shows a comment that stops at the end of the
         * line it was opened on, until something else forces a full repaint.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static char doc2[512];
        int k2 = 0;
        for (int i = 0; i < 8; i++) {
            k2 += sprintf(doc2 + k2, "int b%d;\r\n", i);
        }
        stub_file_add("/open.c", doc2, k2);
        tb_load(&ed.buf_, "/open.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 6;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);

        tb_end(&ed.buf_);
        const key slash = { '/', VK_SLASH };
        const key star = { '*', VK_8 };
        cmd_putc(&ed, slash);
        cap_start();
        cmd_putc(&ed, star);
        const int n2 = cap_read(cap, (int) sizeof(cap));
        check("opening a block comment paints the comment colour",
              has_colour(cap, n2, 8), 1);
        /* The edited row still starts with `int`, so the type colour is on it
         * legitimately. The rows under it are the ones that must have gone
         * over to comment entirely. */
        cap_start();
        cmd_repaint_rows(&ed, 3, 3);
        const int n3 = cap_read(cap, (int) sizeof(cap));
        check("  a row below it is comment", has_colour(cap, n3, 8), 1);
        check("    and is no longer a type", has_colour(cap, n3, 14), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- a row's colouring does not outlive the row --- */
    {
        /*
         * Every paint that wants colour sets its own runs first. The ones that
         * do not -- a cell repainted after a sideways scroll, a row blanked
         * past the end of the document -- must get none rather than whatever
         * the row before them was painted with, which would put one line's
         * comment colour on another line's text.
         *
         * Dropping them in scr_paint_span is what makes that true by
         * construction instead of by every caller remembering.
         */
        files();
        setup(&ed, 0);
        static tok_run runs[2];
        runs[0].end = 3;
        runs[0].cls = TOK_COMMENT;
        scr_set_row_tokens(&ed.scr_, runs, 1);
        check("a row is given its colouring", ed.scr_.nruns_, 1);
        scr_paint_row(&ed.scr_, 1, "abc", 3, NULL, 0);
        check("  and it is gone once the row is painted",
              ed.scr_.runs_ == NULL ? 1 : 0, 1);
        check("    so the next row inherits none", ed.scr_.nruns_, 0);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
