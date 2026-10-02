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

#include "aed.h"

#include <string.h>

#include "version.h"
#include "vkey.h"

// The command list, as pairs. Kept next to nothing else on purpose: a help
// screen that disagrees with the key handling is worse than no help screen, so
// the order here follows the keys -- AED_KEYS in aed.c, and ED_KEYS in editor.c
// behind it -- and any change to one is a change to the other.
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
    { "CTRL+HOME/END",    "start or end of the file" },
    { "PAGE UP/DOWN",     "a screen at a time" },
    { "CTRL+G",           "go to a line number" },

    { NULL,               "EDITING" },
    { "BACKSPACE",        "delete to the left" },
    { "DELETE",           "delete to the right" },
    { "CTRL+D / CTRL+DEL", "delete the whole line" },
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

    { NULL,               "RUNNING" },
    { "CTRL+R",           "build and run .c .s .asm .bas" },

    { NULL,               "SETTINGS" },
    { "CTRL+E",           "settings" },
    { "CTRL+H",           "this list" },
};
#define HELP_LINES ((int)(sizeof(HELP) / sizeof(HELP[0])))

// Where the description starts. The keys begin at column six -- four of indent
// and a bullet -- so this leaves a gap after CTRL+D / CTRL+DEL, the longest of
// them at seventeen characters.
#define HELP_GAP 26

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

static void help_modal(user_input* ui, screen* scr) {
    scr_footer_invalidate(scr);

    // The prompt goes on ui->ypos_, the row every other modal uses. Putting it
    // one row higher left that row showing whatever the last modal had drawn
    // there -- the colour picker's own prompt sat under the settings list,
    // still offering its arrow keys, long after it had been answered.
    const char top = scr->v_->topY_;
    const char bottom = ui->ypos_;
    const int rows = bottom - top - 1;
    if (rows < 1) {
        return;
    }
    const int width = scr->v_->cols_ < 255 ? scr->v_->cols_ : 255;

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
        ui_modal_fill(scr, y, bottom, prompt, psz);

        const key_press kp = ks_wait(ui->keys_);
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

static void banner_modal(user_input* ui, screen* scr) {
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
    const int width = scr->v_->cols_ < 255 ? scr->v_->cols_ : 255;
    const int rows = scr->v_->bottomY_ - scr->v_->topY_;
    if (boxw > width || boxh > rows) {
        return;                                 // no room to say it nicely
    }

    // Centred in the text area both ways.
    const int left = (width - boxw) / 2;
    const char first = (char) (scr->v_->topY_ + (rows - boxh) / 2);

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


/*
 * Help, the banner and the settings are laid out on the whole screen, like the
 * header and footer, whichever view is current: the screen is pointed at
 * whole_ while they are up and back at the caller's view afterwards. A
 * program showing more than one view gets them across the whole text area
 * rather than squeezed into one pane.
 */
void aed_help(user_input* ui, screen* scr) {
    view* was = scr->v_;
    scr_set_view(scr, NULL);
    help_modal(ui, scr);
    scr_set_view(scr, was);
}

void aed_banner(user_input* ui, screen* scr) {
    view* was = scr->v_;
    scr_set_view(scr, NULL);
    banner_modal(ui, scr);
    scr_set_view(scr, was);
}

RESPONSE aed_settings(user_input* ui, screen* scr, config* cfg) {
    return ed_settings_modal(ui, scr, &AED_CONFIG, cfg);
}

