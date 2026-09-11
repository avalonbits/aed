/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "user_input.h"

#include <agon/vdp.h>
#include <agon/mos.h>
#include <string.h>
#include <stdio.h>

#include "keys.h"
#include "version.h"
#include "vkey.h"

user_input* ui_init(user_input* ui, int size, char ypos, int cols) {
    if (!cb_init(&ui->cb_, size)) {
        return NULL;
    }
    ui->ypos_ = ypos;
    ui->cols_ = cols;
    return ui;
}

void ui_resize(user_input* ui, char ypos, int cols) {
    ui->ypos_ = ypos;
    ui->cols_ = cols;
}

void ui_destroy(user_input* ui) {
    cb_destroy(&ui->cb_);
}


static char goto_line[11] = "Goto line: ";
static int atoi(char* str, char sz) {
    int v = 0;
    int mul = 1;
    for (char i = 1; i <= sz; i++) {
        v += (str[sz-i] - '0') * mul;
        mul *= 10;
    }
    return v;
}

RESPONSE ui_goto(user_input* ui, screen* scr, int* line) {
    scr_footer_invalidate(scr);
    scr_bar_line(scr, ui->ypos_, goto_line, sizeof(goto_line));
    scr_tab_bar(scr, sizeof(goto_line), ui->ypos_);
    scr_show_cursor_ch(scr, scr->cursor_);

    char_buffer* cb = &ui->cb_;
    cb_clear(cb);

    do {
        const key_press kp = keys_wait();
        const char key = kp.ch;
        const VKey vkey = kp.vkey;

        if (key >= '0' && key <= '9') {
            if (cb_put(cb, key)) {
                putchar(key);
                scr_show_cursor_ch(scr, cb_peek(cb));
            }
            continue;
        }

        switch (vkey) {
            case VK_ESCAPE:
                return CANCEL_OPT;
            case VK_RETURN: {
                int sz = 0;
                char* buf = cb_prefix(cb, &sz);
                if (sz <= 0) {
                    return CANCEL_OPT;
                }
                *line = atoi(buf, (char) sz);
                if (*line < 0) {
                    return CANCEL_OPT;
                }
                return YES_OPT;
            }
            case VK_BACKSPACE:
                scr_hide_cursor_ch(scr, cb_peek(cb));
                if (cb_bksp(cb)) {
                    vdp_cursor_left();
                }
                scr_show_cursor_ch(scr, cb_peek(cb));
                break;
            default:
                break;

        }
    } while (true);

    return CANCEL_OPT;
}


static const char col_select[39] = "Use UP/DOWN LEFT/RIGHT to select FG/BG";

// The command list, as pairs. Kept next to nothing else on purpose: a help
// screen that disagrees with the key handling is worse than no help screen, so
// the order here follows ctrlCmds and editCmds in editor.c and any change to
// one is a change to the other.
//
// A NULL key starts a new section, with the description as its heading.
typedef struct _help_line {
    const char* keys;
    const char* what;
} help_line;

static const help_line HELP[] = {
    { NULL,               "FILE" },
    { "CTRL+O",           "open a file" },
    { "CTRL+S",           "save" },
    { "CTRL+ALT+S",       "save as" },
    { "CTRL+Q",           "quit" },

    { NULL,               "MOVING" },
    { "arrows",           "move the cursor" },
    { "CTRL+LEFT/RIGHT",  "a word at a time" },
    { "HOME / END",       "start or end of the line" },
    { "PAGE UP/DOWN",     "a screen at a time" },
    { "CTRL+G",           "go to a line number" },

    { NULL,               "EDITING" },
    { "BACKSPACE",        "delete to the left" },
    { "DELETE",           "delete to the right" },
    { "CTRL+D",           "delete the whole line" },
    { "CTRL+Z",           "undo" },
    { "CTRL+Y",           "redo" },

    { NULL,               "SELECTING" },
    { "SHIFT+motion",     "extend a selection" },
    { "CTRL+A",           "select everything" },
    { "CTRL+C",           "copy" },
    { "CTRL+X",           "cut" },
    { "CTRL+V",           "paste" },

    { NULL,               "FINDING" },
    { "CTRL+F",           "find, ignoring case" },
    { "CTRL+N",           "find the next one" },
    { "CTRL+P",           "find the previous one" },

    { NULL,               "SETTINGS" },
    { "CTRL+E",           "settings" },
    { "CTRL+H",           "this list" },
};
#define HELP_LINES ((int)(sizeof(HELP) / sizeof(HELP[0])))

// Where the description starts. The keys begin at column six -- four of indent
// and a bullet -- so this leaves a gap after CTRL+LEFT/RIGHT, the longest of
// them at fifteen characters.
#define HELP_GAP 24

// Renders one entry into `out` and returns its length. A heading is written on
// its own, a pair is written as key then description at a fixed column. The
// blank row that separates one section from the next is put in by the caller,
// which is the only place that knows whether there is room for it.
static int help_render(const help_line* h, char* out, int width) {
    int n = 0;
    if (h->keys == NULL) {
        while (n < 2 && n < width) {
            out[n++] = ' ';
        }
        for (const char* p = h->what; *p != 0 && n < width; p++) {
            out[n++] = *p;
        }

        return n;
    }
    while (n < 4 && n < width) {
        out[n++] = ' ';
    }
    if (n + 1 < width) {
        out[n++] = '-';
        out[n++] = ' ';
    }
    for (const char* p = h->keys; *p != 0 && n < width; p++) {
        out[n++] = *p;
    }
    while (n < HELP_GAP && n < width) {
        out[n++] = ' ';
    }
    for (const char* p = h->what; *p != 0 && n < width; p++) {
        out[n++] = *p;
    }

    return n;
}

// Defined below with the rest of the line formatting; modal_title needs it and
// the modals that use both come after it.
static int put_at(char* out, int at, int width, const char* s);

// The bottom of every modal: blank rows down to the prompt row, then the prompt.
// Three modals drew this, each with its own copy of the loop.
static void modal_fill(screen* scr, char y, char bottom,
                       const char* prompt, int psz) {
    while (y < bottom) {
        scr_write_line(scr, y++, NULL, 0);
    }
    scr_bar_line(scr, bottom, prompt, psz);
}

// The top: a title, then a blank row. Returns the row the caller's own content
// starts on.
static char modal_title(screen* scr, char top, int width, const char* title) {
    static char line[256];

    char y = top;
    const int k = put_at(line, 0, width, title);
    scr_write_line(scr, y++, line, k);
    scr_write_line(scr, y++, NULL, 0);

    return y;
}

// What a key means to a list the reader is moving through, with the motion
// already applied to `at`. Both pickers had their own copy of this, one written
// as a chain of ifs and one as a switch, agreeing on every key.
typedef enum _pick_key {
    PICK_IDLE,          // nothing to do; redraw and wait again
    PICK_TAKE,          // RETURN: act on the row `at` names
    PICK_LEAVE          // ESC
} pick_key;

static pick_key modal_move(VKey vkey, int* at, int max) {
    switch (vkey) {
        case VK_ESCAPE:
            return PICK_LEAVE;
        case VK_UP:
        case VK_KP_UP:
            if (*at > 0) {
                (*at)--;
            }
            break;
        case VK_DOWN:
        case VK_KP_DOWN:
            if (*at < max) {
                (*at)++;
            }
            break;
        case VK_RETURN:
        case VK_KP_ENTER:
            return PICK_TAKE;
        default:
            break;
    }

    return PICK_IDLE;
}

void ui_help(user_input* ui, screen* scr) {
    scr_footer_invalidate(scr);

    // The prompt goes on ui->ypos_, the row every other modal uses. Putting it
    // one row higher left that row showing whatever the last modal had drawn
    // there -- the colour picker's own prompt sat under the settings list,
    // still offering its arrow keys, long after it had been answered.
    const char top = scr->topY_;
    const char bottom = ui->ypos_;
    const int rows = bottom - top - 1;
    if (rows < 1) {
        return;
    }
    const int width = scr->cols_ < 255 ? scr->cols_ : 255;

    static char line[256];

    // Where each page started, so a page can be gone back to. Paging forward is
    // deterministic from a starting entry, but the number of entries on a page
    // is not fixed -- the blank rows between sections vary it -- so going back
    // means remembering rather than recomputing. Deep enough for the list at
    // any font size; if it ever were not, the worst is that paging back stops
    // at the oldest page still remembered.
    int starts[24];
    int depth = 0;
    starts[0] = 0;

    for (;;) {
        const int at = starts[depth];
        char y = top;
        static const help_line title = { NULL, "AED " AED_VERSION " -- commands" };
        int n = help_render(&title, line, width);
        scr_write_line(scr, y++, line, n);
        scr_write_line(scr, y++, NULL, 0);

        int i = at;
        for (; i < HELP_LINES && y < top + 1 + rows; i++) {
            // A blank row before each section, except at the top of a page --
            // a page that opens with a gap just looks like a misprint. If the
            // blank takes the last row, stop and let the heading start the next
            // page rather than stranding it from its entries.
            if (HELP[i].keys == NULL && i > at && y > top + 1) {
                scr_write_line(scr, y++, NULL, 0);
                if (y >= top + 1 + rows) {
                    break;
                }
            }
            n = help_render(&HELP[i], line, width);
            scr_write_line(scr, y++, line, n);
        }
        const bool more = i < HELP_LINES;
        const bool back = depth > 0;
        char* prompt = "  any key to close";
        int psz = 18;
        if (more && back) {
            prompt = "  SPACE or UP/DOWN to page, any other key to close";
            psz = 49;
        } else if (more) {
            prompt = "  SPACE or DOWN for more, any other key to close";
            psz = 47;
        } else if (back) {
            prompt = "  UP for the previous page, any other key to close";
            psz = 49;
        }
        modal_fill(scr, y, bottom, prompt, psz);

        const key_press kp = keys_wait();
        const bool fwd = kp.vkey == VK_SPACE || kp.vkey == VK_PAGEDOWN
                         || kp.vkey == VK_DOWN || kp.vkey == VK_KP_DOWN;
        const bool rev = kp.vkey == VK_PAGEUP
                         || kp.vkey == VK_UP || kp.vkey == VK_KP_UP;

        if (fwd && more) {
            if (depth + 1 < (int)(sizeof(starts) / sizeof(starts[0]))) {
                starts[++depth] = i;
            }
            continue;
        }
        if (rev && back) {
            depth--;
            continue;
        }

        // A paging key with nowhere to go stays put rather than closing: being
        // dropped out of the help for pressing DOWN on the last page would be
        // a surprise, and the prompt says which keys are live.
        if (fwd || rev) {
            continue;
        }

        return;
    }
}

void ui_banner(user_input* ui, screen* scr) {
    (void) ui;

    static const char* BANNER[] = {
        "AED " AED_VERSION,
        "",
        "Press CTRL+H for the list of commands",
    };
    const int n = (int)(sizeof(BANNER) / sizeof(BANNER[0]));

    // Framed with + - | rather than any of the box-drawing characters. A loaded
    // font is 256 glyphs and the box-drawing ones live well above that, so what
    // they land on differs from font to font; these three are the same
    // everywhere.
    const int pad = 3;
    int inner = 0;
    for (int i = 0; i < n; i++) {
        const int len = (int) strlen(BANNER[i]);
        if (len > inner) {
            inner = len;
        }
    }
    inner += pad * 2;

    const int boxw = inner + 2;                 // plus the two verticals
    const int boxh = n + 4;                     // two rules, and a blank inside each
    const int width = scr->cols_ < 255 ? scr->cols_ : 255;
    const int rows = scr->bottomY_ - scr->topY_;
    if (boxw > width || boxh > rows) {
        return;                                 // no room to say it nicely
    }

    // Centred in the text area both ways.
    const int left = (width - boxw) / 2;
    const char first = (char) (scr->topY_ + (rows - boxh) / 2);

    static char line[256];

    for (int r = 0; r < boxh; r++) {
        const bool rule = (r == 0) || (r == boxh - 1);
        const int body = r - 2;                 // past the rule, then the blank
        const char* mid = (!rule && body >= 0 && body < n) ? BANNER[body] : NULL;

        // Written from column zero: the painter pads to the full width, and a
        // line starting further in would leave whatever was underneath it.
        int at = 0;
        while (at < left && at < width) {
            line[at++] = ' ';
        }
        if (at < width) {
            line[at++] = rule ? '+' : '|';
        }
        if (rule) {
            for (int i = 0; i < inner && at < width; i++) {
                line[at++] = '-';
            }
        } else {
            // Centred inside the frame, so a short line like the version does
            // not hang off to one side of a wide box.
            const int len = mid != NULL ? (int) strlen(mid) : 0;
            const int lpad = (inner - len) / 2;
            for (int i = 0; i < lpad && at < width; i++) {
                line[at++] = ' ';
            }
            for (int i = 0; i < len && at < width; i++) {
                line[at++] = mid[i];
            }
            for (int i = lpad + len; i < inner && at < width; i++) {
                line[at++] = ' ';
            }
        }
        if (at < width) {
            line[at++] = rule ? '+' : '|';
        }

        scr_write_line(scr, (char) (first + r), line, at);
    }
}


// ---- settings ----------------------------------------------------------

// Fonts live beside the settings file, in a directory of their own.
#define FONT_DIR   CFG_DIR "/aed"
#define FONT_MAX   16
#define GLYPHS     256

typedef struct _font_entry {
    char name[24];
    int  rows;                  // rows per glyph, from the file size
} font_entry;

// Lists the fonts on the card. A font has no header -- its height is the file
// size divided by 256 -- so the geometry shown here is worked out rather than
// declared, and cannot disagree with what AED will make of the file.
//
// Anything whose size is not a whole number of 256-byte rows is not a font and
// is left out, which is the same rule scr_load_font applies.
static int font_list(font_entry* out, int max) {
    // Static: a FILINFO carries a 256-byte name, and this function is inlined
    // into the settings modal -- so on the stack those bytes join *its* frame,
    // take it past the 128 an ix displacement reaches, and charge an address
    // computation to every local the modal has. One directory walk at a time.
    static DIR dir;
    static FILINFO info;
    int n = 0;

    if (ffs_dopen(&dir, FONT_DIR) != 0) {
        return 0;
    }
    while (n < max) {
        if (ffs_dread(&dir, &info) != 0 || info.fname[0] == 0) {
            break;
        }
        if ((info.fattrib & AM_DIR) != 0) {
            continue;
        }
        if (info.fsize == 0 || (info.fsize % GLYPHS) != 0) {
            continue;
        }
        const int rows = (int)(info.fsize / GLYPHS);
        if (rows < 4 || rows > 255) {
            continue;
        }
        int len = (int) strlen(info.fname);
        if (len >= (int) sizeof(out[n].name)) {
            continue;               // no room to show it, so no room to pick it
        }
        memcpy(out[n].name, info.fname, (size_t) len);
        out[n].name[len] = 0;
        out[n].rows = rows;
        n++;
    }
    ffs_dclose(&dir);

    return n;
}

// Writes `s` into `out` at `at`, stopping at `width`. Returns the new position.
static int put_at(char* out, int at, int width, const char* s) {
    for (const char* p = s; *p != 0 && at < width; p++) {
        out[at++] = *p;
    }

    return at;
}

static int pad_to(char* out, int at, int width, int col) {
    while (at < col && at < width) {
        out[at++] = ' ';
    }

    return at;
}

static int put_num(char* out, int at, int width, int v) {
    char digits[8];
    int d = 0;
    if (v == 0) {
        digits[d++] = '0';
    }
    for (int x = v; x > 0; x /= 10) {
        digits[d++] = (char)('0' + (x % 10));
    }
    while (d > 0 && at < width) {
        out[at++] = digits[--d];
    }

    return at;
}

// Picks a font, or none. Returns YES_OPT with `out` holding a path, or holding
// an empty string for "the machine's own font".
static RESPONSE ui_font_picker(user_input* ui, screen* scr, char* out, int max) {

    static font_entry fonts[FONT_MAX];
    const int n = font_list(fonts, FONT_MAX);

    const char top = scr->topY_;
    const char bottom = ui->ypos_;
    const int width = scr->cols_ < 255 ? scr->cols_ : 255;
    const int px = getsysvar_scrheight();
    const int cols = getsysvar_scrCols();

    static char line[256];
    int at = 0;                     // 0 is "none", 1..n are the fonts

    for (;;) {
        char y = modal_title(scr, top, width, "  FONTS");
        int k = 0;

        for (int i = 0; i <= n && y < bottom; i++) {
            k = put_at(line, 0, width, i == at ? "  > " : "    ");
            if (i == 0) {
                k = put_at(line, k, width, "(none -- the font the machine starts in)");
            } else {
                const font_entry* f = &fonts[i - 1];
                k = put_at(line, k, width, f->name);
                k = pad_to(line, k, width, 28);
                k = put_num(line, k, width, 8);
                k = put_at(line, k, width, "x");
                k = put_num(line, k, width, f->rows);
                k = pad_to(line, k, width, 38);
                k = put_num(line, k, width, cols);
                k = put_at(line, k, width, "x");
                k = put_num(line, k, width, f->rows > 0 ? px / f->rows : 0);
            }
            scr_write_line(scr, y++, line, k);
        }
        modal_fill(scr, y, bottom,
                   "  UP/DOWN to choose, RETURN to take it, ESC to leave it alone", 60);

        const key_press kp = keys_wait();
        const pick_key act = modal_move(kp.vkey, &at, n);
        if (act == PICK_LEAVE) {
            return CANCEL_OPT;
        }
        if (act == PICK_TAKE) {
            if (at == 0) {
                out[0] = 0;

                return NO_OPT;          // chosen, and the choice is "none"
            }
            const int dlen = (int) strlen(FONT_DIR);
            const int flen = (int) strlen(fonts[at - 1].name);
            if (dlen + 1 + flen >= max) {
                return CANCEL_OPT;
            }
            memcpy(out, FONT_DIR, (size_t) dlen);
            out[dlen] = '/';
            memcpy(out + dlen + 1, fonts[at - 1].name, (size_t) flen);
            out[dlen + 1 + flen] = 0;

            return YES_OPT;
        }
    }
}

RESPONSE ui_color_picker(user_input* ui, screen* scr) {
    scr_footer_invalidate(scr);
    char fg = scr->fg_;
    char bg = scr->bg_;

    // Centred across the bar, not the text area -- this row stands in for the
    // footer. The remainder is split rather than halved twice, so an odd number
    // of spare columns still fills the bar instead of leaving one uncovered.
    const int spare = scr->barW_ - (int) sizeof(col_select);
    const int lpad = spare / 2;
    const int rpad = spare - lpad;
    do {
        scr_tab_bar(scr, 0, ui->ypos_);
        set_colours(fg, bg);
        for (int i = 0; i < lpad; i++) {
            putchar(' ');
        }
        VDP_PUTS(col_select);
        for (int i = 0; i < rpad; i++) {
            putchar(' ');
        }

        const VKey vkey = keys_wait().vkey;

        switch (vkey) {
            case VK_ESCAPE:
                return CANCEL_OPT;
            case VK_UP:
            case VK_KP_UP:
                fg = (fg + 1) % scr->colors_;
                break;
            case VK_DOWN:
            case VK_KP_DOWN:
                if (fg == 0) {
                    fg = scr->colors_-1;
                } else {
                    fg = (fg - 1) % scr->colors_;
                }
                break;
            case VK_LEFT:
            case VK_KP_LEFT:
                if (bg == 0) {
                    bg = scr->colors_-1;
                } else {
                    bg = (bg - 1) % scr->colors_;
                }
                break;
            case VK_RIGHT:
            case VK_KP_RIGHT:
                bg = (bg + 1) % scr->colors_;
                break;
            case VK_RETURN:
            case VK_KP_ENTER:
                scr->fg_ = fg;
                scr->bg_ = bg;
                return YES_OPT;
            default:
                break;
        }

    } while (true);

    return CANCEL_OPT;
}

static const char options[13] = " [Y/N/ESC]: ";

static const char dismiss[17] = " (press any key)";

// The settings, in the order they are shown. Each is edited in the way that
// suits it: a number is typed, the colours go to the picker that already
// exists, and a font is chosen from what is on the card.
// ctrl_pause_frames is deliberately not here. It is the one setting that can
// do harm to get wrong -- the sequence carrying it means something else on a
// VDP that does not know it, and one of the bytes after it clears the screen --
// and explaining that in a row of a list is more confusing than useful. It
// stays in the settings file for anyone who wants it; the README says what it
// is for.
typedef enum _setting_row {
    ROW_TAB = 0,
    ROW_COLOURS,
    ROW_FONT,
    ROW_COUNT,
} setting_row;

// Reads a number from the reader, between lo and hi. Returns false when they
// gave up or typed something that is not one, in which case nothing changes --
// a bad answer must not silently become zero.
static bool ask_number(user_input* ui, screen* scr, char* title, int cur,
                       int lo, int hi, int* out) {
    char prefill[8];
    int p = put_num(prefill, 0, (int) sizeof(prefill) - 1, cur < 0 ? 0 : cur);
    prefill[p] = 0;

    char* buf = NULL;
    int sz = 0;
    if (ui_text(ui, scr, title, prefill, &buf, &sz) != YES_OPT || sz <= 0) {
        return false;
    }

    int v = 0;
    for (int i = 0; i < sz; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            return false;
        }
        v = v * 10 + (buf[i] - '0');
        if (v > hi) {
            return false;
        }
    }
    if (v < lo) {
        return false;
    }
    *out = v;

    return true;
}

RESPONSE ui_settings(user_input* ui, screen* scr, config* cfg) {
    // What AED is using now, which is what the rows show until something is
    // changed. `cfg` itself is left holding only the changes: it is written
    // back with cfg_update, which copies through every setting it is not told
    // about, and telling it about one the reader never touched would rewrite a
    // line they had left alone.
    const int tab_now = scr_tab_size(scr);
    const int fg_now = scr_fg(scr);
    const int bg_now = scr_bg(scr);

    // The two CFG_FONT_MAX buffers here are static for the same reason the
    // directory walk's are: together they are 128 bytes, which is the whole ix
    // displacement, and everything past it in the frame then costs five
    // instructions an access. A modal is entered once at a time.
    static char font_now[CFG_FONT_MAX];
    const int fl = (int) strlen(cfg->font);
    memcpy(font_now, cfg->font, (size_t)(fl < CFG_FONT_MAX ? fl : CFG_FONT_MAX - 1));
    font_now[fl < CFG_FONT_MAX ? fl : CFG_FONT_MAX - 1] = 0;
    cfg_defaults(cfg);

    const char top = scr->topY_;
    const char bottom = ui->ypos_;
    const int width = scr->cols_ < 255 ? scr->cols_ : 255;
    static char line[256];
    int at = 0;
    bool changed = false;

    for (;;) {
        int tab = cfg->tab_size >= 0 ? cfg->tab_size : tab_now;
        int fg = cfg->fg >= 0 ? cfg->fg : fg_now;
        int bg = cfg->bg >= 0 ? cfg->bg : bg_now;
        // Three states, not two: a path that was chosen, no font asked for,
        // and nothing said either way -- in which case the row shows what the
        // file already had. Reading only the path conflated the middle one
        // with the last, so choosing "none" left the old font on the row and
        // looked as though nothing had happened.
        const char* font = cfg->font_none
                           ? ""
                           : (cfg->font[0] != 0 ? cfg->font : font_now);

        char y = modal_title(scr, top, width, "  SETTINGS");
        int k = 0;

        for (int i = 0; i < ROW_COUNT && y < bottom; i++) {
            k = put_at(line, 0, width, i == at ? "  > " : "    ");
            switch (i) {
                case ROW_TAB:
                    k = put_at(line, k, width, "tab width");
                    k = pad_to(line, k, width, 24);
                    k = put_num(line, k, width, tab);
                    break;
                case ROW_COLOURS:
                    k = put_at(line, k, width, "colours");
                    k = pad_to(line, k, width, 24);
                    k = put_at(line, k, width, "text ");
                    k = put_num(line, k, width, fg);
                    k = put_at(line, k, width, " on ");
                    k = put_num(line, k, width, bg);
                    break;
                case ROW_FONT:
                    k = put_at(line, k, width, "font");
                    k = pad_to(line, k, width, 24);
                    k = put_at(line, k, width,
                               font[0] != 0 ? font : "(the machine's own)");
                    break;
                default:
                    break;
            }
            scr_write_line(scr, y++, line, k);
        }
        modal_fill(scr, y, bottom,
                   "  UP/DOWN to choose, RETURN to change, ESC to close", 51);

        const key_press kp = keys_wait();
        const pick_key act = modal_move(kp.vkey, &at, ROW_COUNT - 1);
        if (act == PICK_LEAVE) {
            return changed ? YES_OPT : CANCEL_OPT;
        }
        if (act != PICK_TAKE) {
            continue;
        }

        switch (at) {
            case ROW_TAB: {
                int v = 0;
                if (ask_number(ui, scr, "Tab width, 1 to 16:", tab, 1,
                               SCR_MAX_TAB_SIZE, &v)) {
                    cfg->tab_size = v;
                    scr_set_tab_size(scr, (char) v);
                    changed = true;
                }
            } break;
            case ROW_COLOURS:
                // The picker sets the screen's colours as it goes, so what it
                // leaves behind is the answer.
                if (ui_color_picker(ui, scr) == YES_OPT) {
                    cfg->fg = scr_fg(scr);
                    cfg->bg = scr_bg(scr);
                    changed = true;

                    // Repaint the whole screen, not just the rows this modal
                    // draws on. The title bar is not one of them, so it kept
                    // the colours it was drawn in and the new scheme appeared
                    // to have reached only the middle of the screen.
                    //
                    // scr_clear moves the cursor's row to the top of the text
                    // area as a side effect, and what put the document back
                    // reads that row to work out where the view was -- so it
                    // is saved across the clear here, the same as there.
                    const char currX = scr->currX_;
                    const char currY = scr->currY_;
                    scr_clear(scr);
                    scr->currX_ = currX;
                    scr->currY_ = currY;
                }
                break;
            case ROW_FONT: {
                static char picked[CFG_FONT_MAX];
                const RESPONSE got = ui_font_picker(ui, scr, picked, CFG_FONT_MAX);
                if (got == YES_OPT) {
                    const int n = (int) strlen(picked);
                    memcpy(cfg->font, picked, (size_t) n);
                    cfg->font[n] = 0;
                    cfg->font_none = false;
                    changed = true;
                } else if (got == NO_OPT) {
                    // Asked for no font at all, which is a different answer
                    // from saying nothing about it: the line is written out
                    // empty rather than left alone.
                    cfg->font[0] = 0;
                    cfg->font_none = true;
                    changed = true;
                }
            } break;
            default:
                break;
        }
    }
}

void ui_message(user_input* ui, screen* scr, char* msg) {
    scr_footer_invalidate(scr);
    const int msz = strlen(msg);
    scr_bar_line(scr, ui->ypos_, msg, msz);
    scr_tab_bar(scr, msz, ui->ypos_);
    VDP_PUTS(dismiss);
    scr_show_cursor_ch(scr, scr->cursor_);

    keys_wait();
}

RESPONSE ui_dialog(user_input* ui, screen* scr, char* msg) {
    scr_footer_invalidate(scr);
    const int msz = strlen(msg);
    scr_bar_line(scr, ui->ypos_, msg, msz);
    scr_tab_bar(scr, msz, ui->ypos_);
    VDP_PUTS(options);
    scr_show_cursor_ch(scr, scr->cursor_);

    do {
        const key_press kp = keys_wait();
        const char key = kp.ch;
        if (kp.vkey == VK_ESCAPE) {
            break;
        }
        if (key == 'Y' || key == 'y') {
            return YES_OPT;
        }
        if (key == 'N' || key == 'n') {
            return NO_OPT;
        }
    } while (true);

    return CANCEL_OPT;
}

RESPONSE ui_text(
    user_input* ui,
    screen* scr,
    char* title,
    char* prefill,
    char** buf,
    int* sz
) {
    scr_footer_invalidate(scr);
    const int msz = strlen(title);
    scr_bar_line(scr, ui->ypos_, title, msz);
    scr_tab_bar(scr, msz, ui->ypos_);

    *buf = NULL;
    *sz = 0;
    char_buffer* cb = &ui->cb_;
    cb_clear(cb);

    if (prefill != NULL) {
        for (unsigned int i = 0; i < strlen(prefill); i++) {
            const char ch = prefill[i];
            if (!cb_put(cb, ch)) {
                break;
            }
            putchar(ch);
        }
    }
    scr_show_cursor_ch(scr, scr->cursor_);

    do {
        const key_press kp = keys_wait();
        const char key = kp.ch;
        const VKey vkey = kp.vkey;

        if (key != 0x7F && key > 0x20) {
            if (cb_put(cb, key)) {
                putchar(key);
                scr_show_cursor_ch(scr, cb_peek(cb));
            }
            continue;
        }

        switch (vkey) {
            case VK_ESCAPE:
                return CANCEL_OPT;
            case VK_RETURN:
            case VK_KP_ENTER:
                if (cb_used(cb) == 0) {
                    return CANCEL_OPT;
                } else {
                    *buf = cb_prefix(cb, sz);
                    return YES_OPT;
                }
                break;
            case VK_BACKSPACE:
                scr_hide_cursor_ch(scr, cb_peek(cb));
                if (cb_bksp(cb)) {
                    vdp_cursor_left();
                }
                scr_show_cursor_ch(scr, cb_peek(cb));
                break;
            default:
                break;
        }
    } while (true);

    return CANCEL_OPT;
}

