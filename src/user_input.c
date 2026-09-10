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
    { "CTRL+ALT+C",       "pick the colours" },
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

void ui_help(user_input* ui, screen* scr) {
    (void) ui;
    scr_footer_invalidate(scr);

    // The text area, less one row kept for the prompt at the bottom of it.
    const char top = scr->topY_;
    const char bottom = (char) (scr->bottomY_ - 1);
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
        while (y < bottom) {
            scr_write_line(scr, y++, NULL, 0);
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
        scr_bar_line(scr, bottom, prompt, psz);

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

