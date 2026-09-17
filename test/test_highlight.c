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
 * is set to is what aed.ini keeps.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "cmd_ops.h"
#include "undo.h"
#include "user_input.h"
#include "vkey.h"

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
    "string.quoted.double = span '\"' '\"' escape \\\n"
    "storage.type = words int\n";

static const char ASM_CFG[] =
    "[syntax]\nname = asm\nextensions = .s .asm\n"
    "[match]\ncomment.line = eol ';'\n";

/* Says nothing about fg and bg, so it colours tokens and moves no pair. */
static const char DARK[] =
    "[theme]\nname = dark\ncovers = 0 1 4\n"
    "[colours]\ncomment = 8\ntype = 14\nstring = 10\n";

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
    // ed_init does this for a real editor. Without it the screen has nobody to
    // ask and paints everything in the document's own colours.
    ed_attach_colourer(ed);
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

/* The screen column a change to colour `c` lands on, or -1 if it never does. */
static int column_of_colour(const char* b, int n, int c) {
    int col = 0;
    for (int i = 0; i < n; i++) {
        const unsigned char ch = (unsigned char) b[i];
        if (ch == 17 && i + 1 < n) {
            if ((unsigned char) b[i + 1] == (unsigned char) c) {
                return col;
            }
            i++;
            continue;
        }
        if (ch >= 32 && ch < 127) {
            col++;
        }
    }

    return -1;
}

static int asked = 0;

static int count_asks(void* ctx, char ypos, const char* pre, int presz,
                      const char* suf, int sufsz, const tok_run** runs) {
    (void) ctx; (void) ypos; (void) pre; (void) presz;
    (void) suf; (void) sufsz; (void) runs;
    asked++;

    return 0;
}

/* The colour in force when `target` is first written. */
static int colour_at_char(const char* b, int n, char target) {
    int fg = -1;
    for (int i = 0; i < n; i++) {
        const unsigned char c = (unsigned char) b[i];
        if (c == 17 && i + 1 < n) {
            const unsigned char v = (unsigned char) b[i + 1];
            if (v < 128) {
                fg = v;
            }
            i++;
            continue;
        }
        if (c == (unsigned char) target) {
            return fg;
        }
    }

    return -1;
}

/*
 * The pair left set on the wire by whatever was just captured, starting from
 * the pair that was set going in.
 */
static void pair_after(const char* b, int n, int* fg, int* bg) {
    for (int i = 0; i + 1 < n; i++) {
        if ((unsigned char) b[i] != 17) {
            continue;
        }
        const int v = (unsigned char) b[i + 1];
        if (v >= 128) {
            *bg = v - 128;
        } else {
            *fg = v;
        }
        i++;
    }
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
        check("  the theme's type colour, for int",
              has_colour(cap, n, 14), 1);
        check("    and its comment colour", has_colour(cap, n, 8), 1);

        /*
         * And it changes the foreground alone at a token boundary.
         *
         * A theme moves the foreground and leaves the pair the document is
         * drawn on where it is, so the background either side of a boundary is
         * the same and sending it again spends two bytes to say nothing. VDU
         * 17 takes one parameter, so a background change is a second 17 with
         * the index plus 128 -- and the bytes are the cost of a repaint, at
         * 11.1 us each on the wire with 8.6 us to enter MOS for the write
         * (test/probes/vducost.c).
         *
         * Counted rather than sampled: one 17 per change and no high byte
         * behind any of them is the whole claim, and a test that looked for
         * one change would pass while the rest carried both halves.
         */
        {
            int changes = 0;
            int with_bg = 0;
            for (int i = 0; i + 1 < n; i++) {
                if ((unsigned char) cap[i] != 17) {
                    continue;
                }
                changes++;
                if ((unsigned char) cap[i + 1] >= 128) {
                    with_bg++;
                }
                i++;            // the parameter is not itself a command
            }
            check("  a coloured row crosses several boundaries",
                  changes >= 3 ? 1 : 0, 1);
            check("    and none of them resends the background", with_bg, 0);
        }

        /*
         * A selection does swap the pair, and that one still has to send
         * both -- the saving is only sound because a theme never touches the
         * background.
         */
        {
            static char sel[] = "int x; /* hi";
            cap_start();
            scr_write_line_sel(&ed.scr_, 1, sel, (int) sizeof(sel) - 1, 0, 3);
            const int m = cap_read(cap, (int) sizeof(cap));
            int with_bg = 0;
            for (int i = 0; i + 1 < m; i++) {
                if ((unsigned char) cap[i] == 17) {
                    if ((unsigned char) cap[i + 1] >= 128) {
                        with_bg++;
                    }
                    i++;
                }
            }
            check("  a row with a selection on it still sends a background",
                  with_bg > 0 ? 1 : 0, 1);
        }

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

    /* --- a tab does not push the colouring off the token --- */
    {
        /*
         * Reported from a real session: on a reopened, correctly coloured file
         * the last character of `return` was left plain.
         *
         * A run ends at a byte offset into the line, which is what the lexer
         * counts in. The painting counts screen columns, and a tab is one byte
         * and several columns. Looking runs up by column therefore drifted by
         * one column per tab: every token after the first tab was coloured a
         * column early, so its last character lost its colour and the
         * character before it gained one.
         *
         * With tab_size 2 the tab is columns 0 and 1, so `int` starts at
         * column 2. Before the fix the type colour was set at column 1.
         */
        files();
        setup(&ed, 0);
        ed.scr_.tab_size_ = 2;
        stub_emit_colours(1);
        static const char TABBED[] = "\tint x;\r\nint y;\r\n";
        stub_file_add("/tab.c", TABBED, (int) sizeof(TABBED) - 1);
        tb_load(&ed.buf_, "/tab.c");
        ed_pick_syntax(&ed);
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;

        cap_start();
        cmd_repaint_rows(&ed, 1, 1);
        const int n4 = cap_read(cap, (int) sizeof(cap));
        check("a tab-indented token is coloured where it is",
              column_of_colour(cap, n4, 14), 2);

        /* And with no tab the answer is the same one, at column 0. */
        cap_start();
        cmd_repaint_rows(&ed, 2, 2);
        const int n5 = cap_read(cap, (int) sizeof(cap));
        check("  and an unindented one at column 0",
              column_of_colour(cap, n5, 14), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- the cursor puts back the colour it stood on --- */
    {
        /*
         * Reported twice, and the second time was the interesting one: moving
         * the cursor along a line left letters discoloured and then coloured
         * again as it went.
         *
         * The cell being put back is the one the cursor is leaving, and by the
         * time the screen puts it back the document's cursor has already moved
         * -- tb_prev runs first. Answering for "the cell under the cursor"
         * therefore answered for the cell being arrived at, which is the same
         * colour inside a token and the wrong one at either end of it.
         *
         * `int x;`, with the cursor stepping right off the `t` and onto the
         * space. The cell left behind is a type; the one arrived at is not.
         */
        files();
        setup(&ed, 0);
        named(&ed, "/cursor.c");        /* "int x;" */
        ed_pick_syntax(&ed);
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);                  /* which is what fills the model in */

        cmd_right(&ed);
        cmd_right(&ed);                 /* on the `t`, the last of the type */

        stub_emit_colours(1);
        cap_start();
        cmd_right(&ed);                 /* off it, onto the space */
        int nc = cap_read(cap, (int) sizeof(cap));
        check("stepping off a type puts the type's colour back",
              has_colour(cap, nc, 14), 1);

        /* And stepping off the space does not claim it was one. */
        cap_start();
        cmd_right(&ed);
        nc = cap_read(cap, (int) sizeof(cap));
        check("  and stepping off plain text does not",
              has_colour(cap, nc, 14), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);

        files();
        setup(&ed, 0);
        named(&ed, "/cursor.txt");
        ed_pick_syntax(&ed);
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);
        stub_emit_colours(1);
        cap_start();
        cmd_right(&ed);
        nc = cap_read(cap, (int) sizeof(cap));
        check("  a document with no grammar asks for no colour",
              has_colour(cap, nc, 14), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- return in front of a line --- */
    {
        /*
         * The awkward one, and the one that was reported: with the cursor at
         * the start of a line, nothing is left on the row it was on and the
         * whole line moves down to a row cmd_newl paints itself, through the
         * region scroll. That row was painted with no colouring, so the line
         * appeared to lose its colours and got them back the next time
         * anything else repainted it.
         *
         * A fresh document and a deliberate position, because pressing return
         * on a blank line moves nothing and would pass either way.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char NL[] = "int a;\r\nint b;\r\nint c;\r\n";
        stub_file_add("/nl.c", NL, (int) sizeof(NL) - 1);
        tb_load(&ed.buf_, "/nl.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 6;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);

        check("the cursor is at the start of a line with a token on it",
              tb_xpos(&ed.buf_), 1);
        cap_start();
        cmd_newl(&ed);
        const int n6 = cap_read(cap, (int) sizeof(cap));
        check("  return in front of it keeps the line coloured",
              has_colour(cap, n6, 14), 1);

        /*
         * And what is known about the rows survives it. Inserting a line moves
         * every row below, and the states are shifted to follow rather than
         * worked out again -- which on a screenful of C costs a lex a row and
         * measured 141 milliseconds a keystroke before the shift existed.
         *
         * The check is the invariant an edit maintains: the answers stop at
         * the line that changed, and everything above it is still held. They
         * are never thrown away wholesale, which is what left the cursor with
         * nothing to consult.
         */
        check("  what was known above the split is still held",
              ed.synFirst_, 1);
        check("    and none of it was thrown away",
              ed.synKnown_ > 0 ? 1 : 0, 1);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- inserting a line moves every row below it --- */
    {
        /*
         * What each screen row begins inside is worked out once for a view and
         * read back per row. Inserting or removing a line shifts every row
         * under it, so what was worked out describes the wrong rows -- and for
         * C that is the difference between a row being inside a block comment
         * and not.
         *
         * Row 2 here opens a comment that row 3 closes, so row 3 begins inside
         * one and its `*` and `/` are comment rather than text. Insert a line
         * above them and, with the old answer still believed, row 3 is lexed
         * as though it began outside and the closing marker paints plain.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char SHIFT[] = "/* open\r\n*/ int a;\r\nint b;\r\n";
        stub_file_add("/shift.c", SHIFT, (int) sizeof(SHIFT) - 1);
        tb_load(&ed.buf_, "/shift.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 6;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);                  /* which is what fills the answers in */

        cap_start();
        cmd_repaint_rows(&ed, 2, 2);
        int n7 = cap_read(cap, (int) sizeof(cap));
        check("the row inside the comment is comment",
              column_of_colour(cap, n7, 8), 0);

        /* A line in front of everything, which moves all three rows down. */
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_newl(&ed);

        cap_start();
        cmd_repaint_rows(&ed, 3, 3);
        n7 = cap_read(cap, (int) sizeof(cap));
        /*
         * At column 0 specifically. The cursor's own writes carry colour bytes
         * too -- it is sitting in the comment -- so asking only whether the
         * colour appears anywhere passes whether the row was painted right or
         * not.
         */
        check("  and it still is once a line is inserted above it",
              column_of_colour(cap, n7, 8), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- joining two lines moves every row below them --- */
    {
        /*
         * The case the top line cannot catch. Deleting the break between the
         * first two lines leaves the cursor on the same document line and the
         * same screen row, so the view's top line is what it was -- and every
         * row below has still moved up by one.
         *
         * Row 2 was outside the comment and is inside it afterwards. Believing
         * the old answer paints its text plain.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char JOIN[] =
            "int a;\r\n/* open\r\nstill\r\n*/ int b;\r\nint c;\r\n";
        stub_file_add("/join.c", JOIN, (int) sizeof(JOIN) - 1);
        tb_load(&ed.buf_, "/join.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 7;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);

        const int top_was = tb_ypos(&ed.buf_) - (ed.scr_.currY_ - ed.scr_.topY_);
        cmd_end(&ed);                   /* end of line 1 */
        cmd_del(&ed);                   /* which joins it to line 2 */
        check("joining leaves the view's top line where it was",
              tb_ypos(&ed.buf_) - (ed.scr_.currY_ - ed.scr_.topY_), top_was);

        cap_start();
        cmd_repaint_rows(&ed, 2, 2);
        const int n8 = cap_read(cap, (int) sizeof(cap));
        check("  and the row that moved up is coloured for where it now is",
              column_of_colour(cap, n8, 8), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- the row a scroll brings in from below --- */
    {
        /*
         * Scrolling moves every row up and brings in a line that was off
         * screen. Two things have to be right: what each row that moved begins
         * inside, which is what the row under it began inside, and what the
         * new bottom row begins inside, which nothing on screen knew.
         *
         * The document alternates on purpose. Neighbouring rows in the same
         * state hide a shift that never happened, because the wrong answer and
         * the right one are the same value.
         *
         *   1  int a;          begins outside, leaves outside
         *   2  /_* y           begins outside, leaves INSIDE
         *   3  *_/ int c;      begins INSIDE, leaves outside
         *   4  int d;          begins outside
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char SCROLL[] =
            "int z;\r\nint a;\r\n/* y\r\n*/ int c;\r\nint d;\r\n";
        stub_file_add("/scroll.c", SCROLL, (int) sizeof(SCROLL) - 1);
        tb_load(&ed.buf_, "/scroll.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 5;           /* four rows: lines 1 to 4 */
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);

        while (ed.scr_.currY_ < ed.scr_.bottomY_ - 1) {
            cmd_down(&ed);
        }
        check("the cursor is on the bottom row", ed.scr_.currY_,
              ed.scr_.bottomY_ - 1);

        check("  and the painted screen left its answers behind",
              ed.synTop_, 1);
        cmd_down(&ed);                  /* line 5 scrolls into view */
        check("  the view has moved down one", tb_ypos(&ed.buf_), 5);
        check("    and the top row draws the line below", ed.synTop_, 2);
        check("      while the answers themselves did not move",
              ed.synFirst_, 1);

        /*
         * The row that came in from below, first. Its state is the one nothing
         * on screen knew, and taking it from the row above paints this line as
         * comment instead of code.
         *
         * Before row 3, on purpose: painting a row writes what it leaves into
         * the row under it, so checking row 3 first would repair row 4's
         * answer and hide a wrong one.
         */
        cap_start();
        cmd_repaint_rows(&ed, 4, 4);
        int ns = cap_read(cap, (int) sizeof(cap));
        check("    the row from below begins where its line does",
              column_of_colour(cap, ns, 14), 0);
        check("      rather than inside the comment",
              has_colour(cap, ns, 8), 0);

        /*
         * And row 3 shows the line that closes the comment. It begins inside
         * one, so its first two characters are comment. If the rows did not
         * move, row 3 still answers for the line above and they paint plain.
         */
        cap_start();
        cmd_repaint_rows(&ed, 3, 3);
        ns = cap_read(cap, (int) sizeof(cap));
        check("    a row that moved up begins where its line does",
              column_of_colour(cap, ns, 8), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- joining two lines can open something neither of them did --- */
    {
        /*
         * When two lines become one, what the merged line leaves is usually
         * what the second of them left -- which is what the row under it
         * already answered, so shifting alone would do.
         *
         * Not always. Taking the break out puts the two halves together, and
         * characters that meant nothing apart can mean something joined:
         *
         *   int a; /      leaves nothing open
         *   * still       leaves nothing open
         *   int b;
         *
         * joined, the two halves form a slash-star and open a comment that
         * runs on. The row under it has to be told what the merged line
         * leaves rather than what the line it replaced left.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char MERGE[] = "int a; /\r\n* still\r\nint b;\r\nint c;\r\n"
            "int d;\r\nint e;\r\nint f;\r\nint g;\r\n";
        stub_file_add("/merge.c", MERGE, (int) sizeof(MERGE) - 1);
        tb_load(&ed.buf_, "/merge.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 5;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);
        check("the screen left its answers behind", ed.synTop_, 1);

        cap_start();
        cmd_repaint_rows(&ed, 3, 3);
        int nm = cap_read(cap, (int) sizeof(cap));
        check("  the third line is code to start with",
              column_of_colour(cap, nm, 14), 0);

        /* Join the first two, with the cursor at the end of the first. */
        cmd_end(&ed);
        cmd_del(&ed);
        check("  joining takes a line out", tb_ymax(&ed.buf_), 8);
        check("    and the answers survived it", ed.synFirst_ != 0 ? 1 : 0, 1);

        cap_start();
        cmd_repaint_rows(&ed, 2, 2);
        nm = cap_read(cap, (int) sizeof(cap));
        check("    and the row under it is inside the comment now",
              column_of_colour(cap, nm, 8), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- the cell the cursor starts on --- */
    {
        /*
         * Reported from a real session: opening a file put the cursor on the
         * first character of the line, which was coloured correctly until the
         * cursor moved off it -- and then it came back plain. Moving back on
         * to it and off again put it right.
         *
         * The colour of the cell under the cursor was worked out after each
         * command, so before the first command there was none, and the first
         * cell the cursor ever sat on was restored in the document's colour.
         * It is worked out at startup too now, which is what this checks: an
         * editor that has run no commands already knows.
         *
         * Through ed_init rather than by hand, because what failed was the
         * state an editor is in before anything has happened to it.
         */
        files();
        static const char INC[] = "int x;\r\n#include <stdio.h>\r\n";
        stub_file_add("/inc.c", INC, (int) sizeof(INC) - 1);
        static editor e3;
        check("an editor opens the file", ed_init(&e3, 8, "/inc.c") != NULL, 1);
        check("  with a grammar", e3.syn_.loaded ? 1 : 0, 1);
        stub_emit_colours(1);
        cap_start();
        scr_hide_cursor_ch(&e3.scr_, 'i');
        const int ni = cap_read(cap, (int) sizeof(cap));
        check("  and the cursor puts the cell's own colour back",
              has_colour(cap, ni, theme_colour(&e3.theme_, TOK_TYPE)), 1);
        stub_emit_colours(0);
        ed_destroy(&e3);
    }

    /* --- backspacing up to a line leaves it coloured --- */
    {
        /*
         * Reported from a real session: holding backspace until the cursor
         * reached `int main(void) {` left the `i` of int uncoloured.
         *
         * The sequence matters. Backspace inside a line repaints that row;
         * backspace at the start of one joins it to the line above and scrolls
         * the rows under it; and in between, the cursor sits on cells and
         * moves off them again. Each of those used to be a separate place that
         * had to remember about colour, and this walks through all of them.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char BK[] =
            "int main(void) {\r\nxy\r\nint b;\r\nint c;\r\nint d;\r\n";
        stub_file_add("/bk.c", BK, (int) sizeof(BK) - 1);
        tb_load(&ed.buf_, "/bk.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 6;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);

        /* Down to the short line, to its end, and backspace off the end of it
         * and onto the line above. */
        cmd_down(&ed);
        cmd_end(&ed);
        for (int i = 0; i < 3; i++) {
            cmd_bksp(&ed);
        }
        check("backspacing lands on the line above", tb_ypos(&ed.buf_), 1);

        cap_start();
        cmd_repaint_rows(&ed, 1, 1);
        const int nb = cap_read(cap, (int) sizeof(cap));
        check("  and its first character is still a type",
              column_of_colour(cap, nb, 14), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- an edit renumbers the answers below it --- */
    {
        /*
         * Adding or removing a line does not change what the lines below it
         * begin inside -- the same text still runs into them -- but it does
         * change what they are called. The answers are renumbered to follow.
         *
         * Checked well below the edit, on a row the edit did not repaint. The
         * rows it does repaint write their own answers as they go, so a
         * renumbering that went the wrong way is invisible there.
         *
         *   1 int a;      2 /_* x      3 still      4 *_/ int b;     5 int c;
         *
         * Row 4 begins inside the comment. Put a line in at the top and that
         * becomes row 5, still inside it.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char RN[] =
            "int a;\r\n/* x\r\nstill\r\n*/ int b;\r\nint c;\r\nint d;\r\n";
        stub_file_add("/rn.c", RN, (int) sizeof(RN) - 1);
        tb_load(&ed.buf_, "/rn.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 7;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);

        cap_start();
        cmd_repaint_rows(&ed, 4, 4);
        int nr = cap_read(cap, (int) sizeof(cap));
        check("the line that closes the comment is inside it",
              column_of_colour(cap, nr, 8), 0);

        /* A line in at the very top, which moves everything down one. */
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_newl(&ed);
        check("  the document gained a line", tb_ymax(&ed.buf_), 8);

        cap_start();
        cmd_repaint_rows(&ed, 5, 5);
        nr = cap_read(cap, (int) sizeof(cap));
        check("    and it is still inside it, a row further down",
              column_of_colour(cap, nr, 8), 0);

        /* And taking the line back out puts everything where it was. The
         * split left the cursor on the second half, so this steps back onto
         * the blank line it made. */
        cmd_up(&ed);
        cmd_del(&ed);
        check("  the document lost it again", tb_ymax(&ed.buf_), 7);

        /*
         * The line under it first, which is outside the comment while its
         * neighbours are inside one -- so it is the row that shows a
         * renumbering off by one in either direction. Checking a row whose
         * neighbours share its state proves nothing, and checking it after the
         * row above proves nothing either: painting a row writes the answer
         * for the row below, which repairs exactly what is being looked for.
         */
        cap_start();
        cmd_repaint_rows(&ed, 5, 5);
        nr = cap_read(cap, (int) sizeof(cap));
        check("    the line after it is outside the comment",
              column_of_colour(cap, nr, 14), 0);
        check("      rather than inside it", has_colour(cap, nr, 8), 0);

        cap_start();
        cmd_repaint_rows(&ed, 4, 4);
        nr = cap_read(cap, (int) sizeof(cap));
        check("    and the comment's last line is back where it started",
              column_of_colour(cap, nr, 8), 0);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- typing beside a string, then taking it back out --- */
    {
        /*
         * Reported from a real session: text typed between `(` and the quote
         * coloured correctly, and a backspace then turned everything before
         * the quote into string colour, which stayed until the cursor was
         * walked over it.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char PR[] = "printf(\"hi\");\r\nint b;\r\nint c;\r\n";
        stub_file_add("/pr.c", PR, (int) sizeof(PR) - 1);
        tb_load(&ed.buf_, "/pr.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 6;
        ed.scr_.cols_ = 40;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);

        /* To just after the `(`, which is byte 7. */
        for (int i = 0; i < 7; i++) {
            cmd_right(&ed);
        }
        check("the cursor is after the bracket", tb_xpos(&ed.buf_), 8);

        const key kx = { 'x', VK_X };
        cap_start();
        cmd_putc(&ed, kx);
        int np = cap_read(cap, (int) sizeof(cap));
        check("  typing beside the string leaves the name plain",
              colour_at_char(cap, np, 'p'), 15);
        check("    and the string still a string",
              colour_at_char(cap, np, 'h'), 10);

        /*
         * The colour the text is written in, rather than where a colour change
         * lands. The cursor writes its own cell before the row is painted, so
         * counting columns from the start of the capture counts that too --
         * and the cell's colour is exactly what went wrong here.
         */
        cap_start();
        cmd_bksp(&ed);
        np = cap_read(cap, (int) sizeof(cap));
        check("  and taking it back out leaves it plain too",
              colour_at_char(cap, np, 'p'), 15);
        check("    with the string still a string",
              colour_at_char(cap, np, 'h'), 10);
        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- the screen knows what colour it left set --- */
    {
        /*
         * A standing check, and the one that would have caught the last bug
         * rather than leaving it to be reported.
         *
         * curFg_ and curBg_ are what the screen believes the VDP holds, and a
         * row that wants the colour it already has writes nothing. A belief
         * that is wrong therefore does not show where it is wrong -- it shows
         * as the *next* thing painted coming out in whatever was really set.
         * The cursor drew a cell in a string's colour, recorded nothing, and
         * the name in front of the string was painted green.
         *
         * Checked one writer at a time rather than one command at a time. A
         * command ends by drawing the cursor, which sets the pair and records
         * it, so a lie told in the middle is true again by the end -- which is
         * exactly why this went unnoticed until somebody used the editor.
         *
         * Anything added to screen.c that emits a colour belongs in this list.
         */
        files();
        setup(&ed, 0);
        stub_emit_colours(1);
        static const char INV[] =
            "printf(\"hi\");\r\nint b;\r\n/* c\r\nstill\r\n*/ int d;\r\nint e;\r\n";
        stub_file_add("/inv.c", INV, (int) sizeof(INV) - 1);
        tb_load(&ed.buf_, "/inv.c");
        ed_pick_syntax(&ed);
        ed.scr_.bottomY_ = 6;
        ed.scr_.cols_ = 40;
        tb_home(&ed.buf_);
        ed.scr_.currY_ = 1;
        cmd_show(&ed);

        /* On the quote, so the cursor cell is a colour the document is not. */
        for (int i = 0; i < 7; i++) {
            cmd_right(&ed);
        }

        int fg = ed.scr_.curFg_;
        int bg = ed.scr_.curBg_;
        cap_start();
        scr_hide_cursor_ch(&ed.scr_, '"');
        int n = cap_read(cap, (int) sizeof(cap));
        pair_after(cap, n, &fg, &bg);
        check("hiding the cursor records what it left set",
              (fg == ed.scr_.curFg_ && bg == ed.scr_.curBg_) ? 1 : 0, 1);
        check("  and it left the cell's own colour", fg, 10);

        fg = ed.scr_.curFg_;
        bg = ed.scr_.curBg_;
        cap_start();
        scr_show_cursor_ch(&ed.scr_, '"');
        n = cap_read(cap, (int) sizeof(cap));
        pair_after(cap, n, &fg, &bg);
        check("showing the cursor records what it left set",
              (fg == ed.scr_.curFg_ && bg == ed.scr_.curBg_) ? 1 : 0, 1);

        fg = ed.scr_.curFg_;
        bg = ed.scr_.curBg_;
        cap_start();
        scr_paint_row(&ed.scr_, 2, "int b;", 6, NULL, 0);
        n = cap_read(cap, (int) sizeof(cap));
        pair_after(cap, n, &fg, &bg);
        check("painting a row records what it left set",
              (fg == ed.scr_.curFg_ && bg == ed.scr_.curBg_) ? 1 : 0, 1);

        fg = ed.scr_.curFg_;
        bg = ed.scr_.curBg_;
        cap_start();
        scr_write_line(&ed.scr_, 2, "int b;", 6);
        n = cap_read(cap, (int) sizeof(cap));
        pair_after(cap, n, &fg, &bg);
        check("writing a line records what it left set",
              (fg == ed.scr_.curFg_ && bg == ed.scr_.curBg_) ? 1 : 0, 1);

        fg = ed.scr_.curFg_;
        bg = ed.scr_.curBg_;
        cap_start();
        scr_write_line_sel(&ed.scr_, 2, "int b;", 6, 1, 3);
        n = cap_read(cap, (int) sizeof(cap));
        pair_after(cap, n, &fg, &bg);
        check("writing one with a selection records what it left set",
              (fg == ed.scr_.curFg_ && bg == ed.scr_.curBg_) ? 1 : 0, 1);

        /*
         * And the one that matters in practice: a row painted straight after
         * the cursor drew in a token's colour comes out in the document's.
         */
        for (int i = 0; i < 7; i++) {
            cmd_left(&ed);
        }
        for (int i = 0; i < 7; i++) {
            cmd_right(&ed);
        }
        scr_hide_cursor_ch(&ed.scr_, '"');
        cap_start();
        scr_paint_row(&ed.scr_, 2, "int b;", 6, NULL, 0);
        n = cap_read(cap, (int) sizeof(cap));
        check("  and a row painted after it is the document's colour",
              colour_at_char(cap, n, 'b'), 15);

        stub_emit_colours(0);
        tb_destroy(&ed.buf_);
    }

    /* --- the screen asks, rather than being told --- */
    {
        /*
         * The property the whole arrangement rests on, and the one worth a
         * test of its own: a path that paints a row gets that row's colouring
         * without knowing there is such a thing.
         *
         * It was the other way round, and every bug reported against syntax
         * highlighting was a paint that had not been told -- typing, scrolling
         * off the bottom, return in front of a line, the cell the cursor left.
         * Each was found by a user rather than by the suite, because a paint
         * that forgets looks exactly like a document with no grammar.
         *
         * A counting answerer here, so the check is that the question is asked
         * at all rather than what the answer was.
         */
        files();
        setup(&ed, 0);
        asked = 0;
        scr_set_colourer(&ed.scr_, count_asks, NULL, NULL);
        scr_set_theme(&ed.scr_, &ed.theme_);

        scr_paint_row(&ed.scr_, 1, "abc", 3, NULL, 0);
        check("painting a row asks what colours it", asked, 1);

        scr_write_line(&ed.scr_, 2, "abc", 3);
        check("  and so does writing one", asked, 2);

        scr_write_line_sel(&ed.scr_, 3, "abc", 3, 0, 0);
        check("    and writing one with a selection", asked, 3);

        scr_write_line(&ed.scr_, 4, NULL, 0);
        check("      and blanking one", asked, 4);

        /* And with nobody to ask, a row paints plainly rather than wrongly. */
        scr_set_colourer(&ed.scr_, NULL, NULL, NULL);
        scr_paint_row(&ed.scr_, 1, "abc", 3, NULL, 0);
        check("  with nobody to ask it paints plainly",
              ed.scr_.runs_ == NULL ? 1 : 0, 1);
        tb_destroy(&ed.buf_);
    }

    /* --- changing the background changes the theme with it --- */
    {
        /*
         * A theme is picked for the background it was written against, and
         * CTRL+E can leave a different one behind. Picking black on white and
         * then inverting it used to keep the theme chosen for black: the
         * colours were only right again after quitting and coming back, which
         * is the startup path doing what this one was not.
         *
         * Driven through cmd_settings rather than by setting the background
         * directly, because the modal is where the reader changes it and the
         * order it does things in -- pick, then repaint -- is half the fix.
         */
        files();
        setup(&ed, 0);
        named(&ed, "/main.c");
        ed_pick_syntax(&ed);
        check("a C file on a dark background",
              strcmp(ed.theme_.name, "dark") == 0 ? 1 : 0, 1);

        ui_init(&ed.ui_, 256, ed.scr_.bottomY_, ed.scr_.cols_);

        /* Down to the colours, into the picker, background 0 -> 2, accept,
         * close. bold.cfg is the one that covers 2. */
        const stub_key to_bold[] = {
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(to_bold, 6);
        cmd_settings(&ed);

        check("  and a background the reader moves to 2",
              scr_base_bg(&ed.scr_), 2);
        check("    takes the theme written for 2",
              strcmp(ed.theme_.name, "bold") == 0 ? 1 : 0, 1);
        check("      which the screen is painting with",
              ed.scr_.theme_ != NULL ? 1 : 0, 1);
        check("      and whose own pair is in force", ed.scr_.fg_, 11);
        check("    while the base pair is still the reader's",
              scr_base_fg(&ed.scr_), 15);

        ui_destroy(&ed.ui_);
        tb_destroy(&ed.buf_);
    }

    /* --- and to a background no theme covers, which colours nothing --- */
    {
        /*
         * The same rule as opening a file no grammar claims: tokens painted
         * all one colour is the work without the result, so the grammar goes
         * too and the reader gets their own colours back.
         */
        files();
        setup(&ed, 0);
        named(&ed, "/main.c");
        ed_pick_syntax(&ed);
        check("a grammar and theme in force", ed.syn_.loaded ? 1 : 0, 1);

        ui_init(&ed.ui_, 256, ed.scr_.bottomY_, ed.scr_.cols_);

        /* Background 0 -> 7. Nothing here covers 7. */
        const stub_key to_bare[] = {
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 0,  .vk = VK_RIGHT },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(to_bare, 11);
        cmd_settings(&ed);

        check("  moving to a background no theme covers",
              scr_base_bg(&ed.scr_), 7);
        check("    leaves the screen unthemed",
              ed.scr_.theme_ == NULL ? 1 : 0, 1);
        check("    and drops the grammar with it", ed.syn_.loaded ? 1 : 0, 0);
        check("    so the reader's own pair is what paints", ed.scr_.bg_, 7);

        ui_destroy(&ed.ui_);
        tb_destroy(&ed.buf_);
    }

    /* --- the foreground alone leaves the theme where it was --- */
    {
        /* Which theme fits is a question about the background: a theme names
         * the backgrounds it was designed against and says nothing about what
         * the text is drawn in. */
        files();
        setup(&ed, 0);
        named(&ed, "/main.c");
        ed_pick_syntax(&ed);

        ui_init(&ed.ui_, 256, ed.scr_.bottomY_, ed.scr_.cols_);

        /* UP is the foreground, and 15 wraps to 0. */
        const stub_key fg_only[] = {
            { .ch = 0,  .vk = VK_DOWN },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 0,  .vk = VK_UP },
            { .ch = 13, .vk = VK_RETURN },
            { .ch = 27, .vk = VK_ESCAPE },
        };
        stub_set_keys(fg_only, 5);
        cmd_settings(&ed);

        check("a foreground change moves the base foreground",
              scr_base_fg(&ed.scr_), 0);
        check("  and leaves the background alone", scr_base_bg(&ed.scr_), 0);
        check("    so the theme is the one it was",
              strcmp(ed.theme_.name, "dark") == 0 ? 1 : 0, 1);
        check("      and the grammar is still loaded", ed.syn_.loaded ? 1 : 0, 1);

        ui_destroy(&ed.ui_);
        tb_destroy(&ed.buf_);
    }

    /* --- a new line inherits the indent, for a document with a grammar --- */
    {
        /*
         * Pressing RETURN at the end of an indented line should leave the
         * cursor under the text rather than at the left margin: a scope is
         * indented once and every line in it repeats that, so typing it again
         * per line is the editor making the reader do its work.
         *
         * Gated on a grammar, which is what the request asked for -- indenting
         * is a habit of code rather than of prose, and a document AED cannot
         * recognise is as likely to be a letter as a program.
         */

        /* What is on the new line, before the cursor. After a RETURN that is
         * exactly the indent, because nothing else has been typed. */
        #define NEW_INDENT(e) (tb_curr_line(&(e)->buf_).psz_)

        files();
        setup(&ed, 0);
        /* setup() leaves the undo log unwired, which ed_init does for a real
         * editor. Without it cmd_undo is a no-op and an undo check passes
         * against anything -- which is how the first version of the check
         * below passed while undoing nothing. */
        undo_init(&ed.undo_, UNDO_TEXT_BYTES, UNDO_MAX_RECS);
        tb_set_undo(&ed.buf_, &ed.undo_);
        named_text(&ed, "/main.c", "    int x;\r\nplain\r\n");
        ed_pick_syntax(&ed);
        cmd_show(&ed);
        check("a C file gets a grammar", ed.syn_.loaded ? 1 : 0, 1);

        /* To the end of the first line, which is where RETURN is pressed. */
        cmd_home(&ed);
        cmd_end(&ed);
        cmd_newl(&ed);
        check("  RETURN at the end of an indented line", tb_ypos(&ed.buf_), 2);
        check("    starts the new line under the text", NEW_INDENT(&ed), 4);
        check("      and the cursor sits there", ed.scr_.currX_, 4);
        {
            const split_line ln = tb_curr_line(&ed.buf_);
            check("      as spaces, which is what the line had",
                  ln.prefix_[0] == ' ' && ln.prefix_[3] == ' ' ? 1 : 0, 1);
        }

        /* One CTRL+Z, not two: the break and the indent it brought are one
         * edit, or undo leaves whitespace the reader never typed. */
        /*
         * Two undos, and the pair is the assertion rather than either half.
         * A record is one run on one line: the break is recorded on the line
         * it split and the indent on the line below, so they cannot be one
         * record however they are grouped.
         *
         * What matters is that the halfway house is somewhere sensible -- the
         * first undo leaves exactly the empty line a RETURN without this
         * feature would have left, rather than a line holding whitespace
         * nobody typed.
         */
        cmd_undo(&ed);
        check("    one undo takes the indent", NEW_INDENT(&ed), 0);
        check("      leaving the line a plain RETURN would have",
              tb_ymax(&ed.buf_), 4);
        cmd_undo(&ed);
        check("    and the second takes the break", tb_ymax(&ed.buf_), 3);
        undo_destroy(&ed.undo_);
        tb_destroy(&ed.buf_);
    }

    /* --- a tab indent stays tabs --- */
    {
        /* Copied rather than converted: a file indented with tabs stays
         * indented with tabs. Deciding which the reader meant is a different
         * feature and a worse one to get wrong. */
        files();
        setup(&ed, 0);
        named_text(&ed, "/main.c", "\tint x;\r\n");
        ed_pick_syntax(&ed);
        cmd_show(&ed);
        cmd_home(&ed);
        cmd_end(&ed);
        cmd_newl(&ed);
        {
            const split_line ln = tb_curr_line(&ed.buf_);
            check("a tab indent is inherited as one byte", ln.psz_, 1);
            check("  and it is a tab", ln.prefix_[0] == '\t' ? 1 : 0, 1);
        }
        tb_destroy(&ed.buf_);
    }

    /* --- only as far as the cursor --- */
    {
        /*
         * Splitting `    foo` at the second column gives `  ` above and
         * `  foo` below, and the text is where it was. Taking the whole indent
         * instead would push it two columns right for having been split.
         */
        files();
        setup(&ed, 0);
        named_text(&ed, "/main.c", "    foo\r\n");
        ed_pick_syntax(&ed);
        cmd_show(&ed);
        cmd_home(&ed);
        cmd_right(&ed);
        cmd_right(&ed);
        cmd_newl(&ed);
        check("splitting inside the indent takes only what is behind the cursor",
              NEW_INDENT(&ed), 2);
        tb_destroy(&ed.buf_);

        /* And at the very start there is nothing behind it, so the new line
         * begins where the old one did rather than gaining an indent the
         * reader never asked for. */
        setup(&ed, 0);
        named_text(&ed, "/main.c", "    foo\r\n");
        ed_pick_syntax(&ed);
        cmd_show(&ed);
        cmd_home(&ed);
        cmd_newl(&ed);
        check("  and RETURN at the start of a line adds none",
              NEW_INDENT(&ed), 0);
        tb_destroy(&ed.buf_);
    }

    /* --- an indent deeper than the bound is taken as far as the bound --- */
    {
        /*
         * The copy is into a fixed buffer, so the bound is what stops a line
         * that begins with a hundred columns of whitespace writing past it.
         * Without the clamp this is a memcpy of 100 bytes into 64, which the
         * sanitiser stops the suite dead for rather than reporting as a
         * failing check -- so this is one to read the exit status for.
         */
        static char deep[160];
        int at = 0;
        while (at < 100) {
            deep[at++] = ' ';
        }
        deep[at++] = 'x';
        deep[at++] = '\r';
        deep[at++] = '\n';
        deep[at] = 0;

        files();
        setup(&ed, 0);
        named_text(&ed, "/main.c", deep);
        ed_pick_syntax(&ed);
        cmd_show(&ed);
        cmd_home(&ed);
        cmd_end(&ed);
        cmd_newl(&ed);
        check("an indent past the bound is taken as far as the bound",
              NEW_INDENT(&ed), 64);
        tb_destroy(&ed.buf_);
    }

    /* --- and a document with no grammar gets none of it --- */
    {
        files();
        setup(&ed, 0);
        named_text(&ed, "/notes.txt", "    a list item\r\n");
        ed_pick_syntax(&ed);
        cmd_show(&ed);
        check("a .txt file finds no grammar", ed.syn_.loaded ? 1 : 0, 0);
        cmd_home(&ed);
        cmd_end(&ed);
        cmd_newl(&ed);
        check("  so RETURN leaves the new line at the margin",
              NEW_INDENT(&ed), 0);
        check("    and the cursor with it", ed.scr_.currX_, 0);
        tb_destroy(&ed.buf_);
    }

    /* --- the answers window slides rather than starting again --- */
    {
        /*
         * Holding an arrow key used to scroll smoothly and then stop dead,
         * over and over: down stalled once every SCR_MAX_ROWS rows, and up
         * stalled on nearly every row once the reader passed the top of the
         * answers. Each stall was a read-back -- two hundred lines sought and
         * lexed to find what one line begins inside.
         *
         * Both came from the window starting *at* the line asked about. Going
         * down that left no room to grow, so the next row started again; going
         * up it left nothing above, so the next row -- which is above, because
         * that is what going up means -- started again too.
         *
         * Counting read-backs would need a counter in the lexer, which is
         * production code paying for a test. The window itself is the fix, so
         * these check its shape: an answer window that reaches above the line
         * it was asked about, and one that keeps its answers when it fills.
         */
        /*
         * Longer than the answer window, or nothing below exercises the slide:
         * a document that fits in the window never has to drop anything, and
         * every check about sliding passes without one happening.
         */
        static char many[24000];
        int at = 0;
        for (int i = 0; i < SYN_WINDOW * 2; i++) {
            at += sprintf(many + at, "int v%d; /* line %d */\r\n", i, i);
        }

        files();
        setup(&ed, 0);
        /*
         * A bigger buffer than setup() hands out. Its 4 KiB holds 128 index
         * slots, so a document of 300 lines pages -- and a walker may not
         * slide, so the read-back cannot reach past the window and the whole
         * thing measures paging rather than the answers. The file that
         * prompted this is 16 KB in a 72 KiB buffer and is not paged at all.
         */
        tb_destroy(&ed.buf_);
        tb_init(&ed.buf_, 32, NULL);
        named_text(&ed, "/main.c", many);
        ed_pick_syntax(&ed);
        cmd_show(&ed);
        check("a long C file, held whole",
              tb_ymax(&ed.buf_) > SYN_WINDOW ? 1 : 0, 1);
        check("  and not paged", ed.buf_.paged_ ? 1 : 0, 0);

        /* Down past the end of the window, which is where it used to stall. */
        for (int i = 0; i < SYN_WINDOW + 40; i++) {
            cmd_down(&ed);
        }
        check("  scrolled well past a windowful", ed.synTop_ > SYN_WINDOW, 1);
        check("    the window slid with it", ed.synFirst_ > 1, 1);
        check("      and kept its answers rather than starting again",
              ed.synKnown_ >= SYN_WINDOW / 2, 1);

        /*
         * And back up, far enough to leave the window behind -- which is the
         * case that used to stall on every row. Coming up less than a
         * windowful stays inside the answers the walk down already built and
         * would pass whatever a refill does, so it proves nothing.
         */
        for (int i = 0; i < SYN_WINDOW + 20; i++) {
            cmd_up(&ed);
        }
        check("  and scrolling up out of the window again", ed.synTop_ > 1, 1);
        /*
         * How wide the window is, rather than how far above the top line it
         * reaches: the reach is legitimately spent down to nothing just before
         * the next refill, so it says more about where the walk stopped than
         * about what a refill does. The width does not move between refills,
         * so it is the same answer wherever the walk stops -- a windowful when
         * a refill keeps what its read-back passed through, and a screenful
         * when every row starts again.
         */
        check("    leaves a windowful of answers rather than a screenful",
              ed.synKnown_ >= SYN_WINDOW / 2, 1);

        /*
         * A page down reuses the answers rather than working them out again.
         *
         * The view moved forward by one screen from a place already painted,
         * and the paint recorded what every line it drew leaves open -- so the
         * new top line's answer is already held. Refilling instead read back
         * up to SYN_LOOKBACK lines to re-derive it, on every repaint: 174
         * lexes a page against a screenful, and 3.1 seconds a page against
         * 2.0, paging through a 406-line C file on the emulator.
         *
         * What says so here is that the window is not restarted -- synFirst_
         * stays where it was -- and that it still answers for where the view
         * landed.
         */
        {
            const int was_first = ed.synFirst_;
            cmd_page_down(&ed);
            check("  a page down reuses the answers", ed.synFirst_, was_first);
            check("    and they still reach where it landed",
                  ed.synTop_ - ed.synFirst_ < ed.synKnown_, 1);
        }

        /*
         * A page up keeps the answers above the line it landed on.
         *
         * Going up is the direction that cannot chain: the line above is not
         * one any paint has been asked about, so the view reads back to find
         * what its new top begins inside. That walk settles the same question
         * for every line it passes, and the view used to keep only the answer
         * for the top row -- so the next page up was outside the window again
         * and read back over lines it had just read. Some page ups were free
         * and the rest cost a full lookback, which is what made them uneven.
         *
         * What says so here is that after a page up the window reaches above
         * the top row, and that the page up after it finds its own landing
         * line already answered for. Over a 406 line C file that took a page
         * up from 125 lexes to 52, and the worst one from 223 to one refill in
         * four.
         */
        {
            /*
             * Far enough down that a page up has to leave the window behind:
             * coming up less than a windowful stays inside the answers the
             * walk down already built and would pass whatever a refill does.
             *
             * What a refill leaves is checked when one happens -- synFirst_
             * moving is what says one did -- because between refills the reach
             * above the top row is legitimately spent down to nothing, and
             * asking after an arbitrary page up measures where the walk
             * stopped rather than what a refill does.
             */
            for (int i = 0; i < 8; i++) {
                cmd_page_down(&ed);
            }
            const int screenful = ed.scr_.bottomY_ - ed.scr_.topY_;
            int refills = 0;
            int kept = 1;
            for (int i = 0; i < 6; i++) {
                const int was = ed.synFirst_;
                cmd_page_up(&ed);
                if (ed.synFirst_ == was) {
                    continue;               // answered from the window
                }
                refills++;
                if (ed.synTop_ > 1 && ed.synTop_ - ed.synFirst_ < screenful) {
                    kept = 0;               // read back and kept the screen
                }
            }
            check("  a page up that reads back keeps what the walk passed",
                  kept, 1);
            check("    so one read-back serves several page ups",
                  refills < 6, 1);
        }

        /*
         * And the answers it keeps are the right ones.
         *
         * The read-back settles what every line it passes begins inside, and
         * the window is now handed those rather than walking the same lines
         * again. A second editor reaches the same top line by scrolling down
         * from the first line, which chains every answer from a line that
         * certainly starts clean and so cannot be wrong; the two have to agree
         * wherever their windows overlap.
         *
         * The document above cannot show this: every comment on it closes on
         * its own line, so every answer is NONE and a wrong one looks right.
         * This one has comments that run for twenty lines at a time.
         */
        {
            static char spans[12000];
            int sat = 0;
            for (int i = 0; i < 280; i++) {
                const int k = i % 40;
                if (k == 20) {
                    sat += sprintf(spans + sat, "/* open at %d\r\n", i);
                } else if (k == 39) {
                    sat += sprintf(spans + sat, "closed here */\r\n");
                } else if (k > 20) {
                    sat += sprintf(spans + sat, "still inside %d\r\n", i);
                } else {
                    sat += sprintf(spans + sat, "int v%d;\r\n", i);
                }
            }

            static editor paged;
            static editor walked;
            for (int which = 0; which < 2; which++) {
                editor* e = which == 0 ? &paged : &walked;
                files();
                setup(e, 0);
                tb_destroy(&e->buf_);
                tb_init(&e->buf_, 32, NULL);
                named_text(e, "/spans.c", spans);
                ed_pick_syntax(e);
                cmd_show(e);
            }
            check("a C file whose comments run over many lines",
                  paged.syn_.loaded ? 1 : 0, 1);

            /*
             * To the top and then to the end, so the refill at the end finds
             * a window holding answers about the first screen of the document
             * rather than about the lines it is being asked for. A refill that
             * widens the window without filling it leaves those in place, and
             * they are answers about the wrong lines.
             */
            cmd_doc_top(&paged);
            cmd_doc_end(&paged);
            for (int i = 0; i < 2; i++) {
                cmd_page_up(&paged);
            }

            /* Scrolled to the same top line, a row at a time. */
            int guard = 0;
            while (walked.synTop_ < paged.synTop_ && guard++ < 4000) {
                cmd_down(&walked);
            }
            check("  both views on the same line", walked.synTop_,
                  paged.synTop_);

            int agree = 1;
            int compared = 0;
            for (int l = paged.synFirst_;
                 l < paged.synFirst_ + paged.synKnown_; l++) {
                const int b = l - walked.synFirst_;
                if (b < 0 || b >= walked.synKnown_) {
                    continue;
                }
                compared++;
                if (paged.lineSyn_[l - paged.synFirst_] != walked.lineSyn_[b]) {
                    agree = 0;
                }
            }
            check("  the windows overlap enough to say anything",
                  compared > 8 ? 1 : 0, 1);
            check("    and every answer they share agrees", agree, 1);

            tb_destroy(&paged.buf_);
            tb_destroy(&walked.buf_);
        }

        tb_destroy(&ed.buf_);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
