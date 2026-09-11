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

#include "screen.h"

#include <agon/vdp.h>
#include <string.h>
#include <agon/mos.h>
#include <stdio.h>

#include "bootfont.h"
#include "conv.h"

#define MAX_COLS 255

// Characters on their way to the VDP, held back so they go in one call.
//
// putchar is `rst.lil $10` -- one entry into MOS per byte -- so painting a row
// a character at a time is eighty of them, and the editor does that on a
// keystroke. Everything that draws runs of text writes here instead, and the
// run is sent when the colour changes or the row ends.
static char out_buf[MAX_COLS];
static int out_len;

static void out_flush(void) {
    if (out_len > 0) {
        mos_puts(out_buf, out_len, 0);
        out_len = 0;
    }
}

static void out_ch(char ch) {
    if (out_len >= (int) sizeof(out_buf)) {
        out_flush();
    }
    out_buf[out_len++] = ch;
}

static void out_str(const char* str, int n) {
    for (int i = 0; i < n; i++) {
        out_ch(str[i]);
    }
}

static void out_run(char ch, int n) {
    for (int i = 0; i < n; i++) {
        out_ch(ch);
    }
}

void set_colours(char fg, char bg) {
    // Anything buffered belongs to the colours that are still current.
    out_flush();

    // Both colours in one write. VDU 17 takes one parameter, so this is two
    // commands, but they are four bytes and there is no reason to enter MOS
    // twice for them -- and the editor changes colours several times a
    // keystroke, around the cursor cell and at each end of a highlight.
    char vdu[4];
    vdu[0] = 17;
    vdu[1] = fg;
    vdu[2] = 17;
    vdu[3] = (char) (bg + 128);
    mos_puts(vdu, sizeof(vdu), 0);
}

// The cursor cell shows the character under it, but a control byte cannot be
// drawn -- sending a tab to the VDP moves the cursor instead of painting it,
// which left the cursor invisible whenever it sat on one.
static char cursor_glyph(screen* scr, char ch) {
    if (ch == 0 || ch == '\r' || ch == '\n' || ch == '\t') {
        return scr->cursor_;
    }

    return ch;
}

// Drawing the cursor is four things -- reverse the colours, put the character
// down, step back over it, put the colours back -- and ten bytes. It happens on
// every keystroke, so it goes in one write rather than four.
void scr_show_cursor_ch(screen* scr, char ch) {
    ch = cursor_glyph(scr, ch);
    out_flush();

    char vdu[10];
    vdu[0] = 17;
    vdu[1] = scr->bg_;
    vdu[2] = 17;
    vdu[3] = (char) (scr->fg_ + 128);
    vdu[4] = ch;
    vdu[5] = 8;                         // VDU 8: back over the cell just drawn
    vdu[6] = 17;
    vdu[7] = scr->fg_;
    vdu[8] = 17;
    vdu[9] = (char) (scr->bg_ + 128);
    mos_puts(vdu, sizeof(vdu), 0);
}

static void scr_show_cursor(screen* scr) {
    scr_show_cursor_ch(scr, scr->cursor_);
}

static void vdp_puts(char* str, char sz) {
    volatile uint8_t* sysvar = mos_sysvars();
    sysvar[sysvar_vdp_pflags] = 0;
    mos_puts(str, sz, 0);

    for (;;) {
        waitvblank();
        sysvar = mos_sysvars();
        if ((sysvar[sysvar_vdp_pflags] & 0x04) != 0) {
            break;
        }
    }
}

// The column of the probe cell that is looked at. Four is the middle of an
// eight pixel wide glyph, and every font the VDP can hold is eight wide.
#define PROBE_X 4

// One pixel of the cell in the top left corner, as a colour index: VDU 23,0,&84
// asks and the VDP puts the answer in a sysvar.
static char pixel_colour(int x, int y) {
    char ask[7] = {23, 0, (char) 0x84,
                   (char) (x & 0xFF), (char) ((x >> 8) & 0xFF),
                   (char) (y & 0xFF), (char) ((y >> 8) & 0xFF)};

    vdp_puts(ask, sizeof(ask));

    volatile char idx = 0;
    for (int i = 0; i < 1; i++) {
        waitvblank();
        volatile char* sysvar = (volatile char*) mos_sysvars();
        idx = sysvar[sysvar_scrpixelIndex];
    }

    return idx;
}

// Puts one character in that cell, and waits for it to be on the screen before
// anything reads the pixels back.
static void put_probe_ch(char ch) {
    vdp_cursor_tab(0, 0);
    mos_puts(&ch, 1, 0);
    waitvblank();
}

// The colours the machine was using, read back off its own screen so that AED
// can put them back on the way out. A space paints the whole cell in the
// background; a character with ink in it shows the foreground somewhere.
//
// Where that ink is depends on the font, which is why this searches the cell
// rather than sampling the middle of it. The middle of an 8x8 cell is inside
// almost every glyph, but row 4 of a sixteen-row font is above most of them:
// unscii-16 draws '*' from row 4 with nothing in the middle column, so the one
// read this used to do -- '*' at (4,4) -- came back blank, and the background
// was recorded as the foreground.
//
// Restoring fg == bg is worse than it sounds. The screen is handed back drawn
// in its own background colour, and the VDP builds the text cursor by XOR-ing
// the two colours together, so an equal pair leaves a cursor with no colour in
// it at all: a blank screen with no cursor on it, which is what a machine
// booted into a sixteen-row font got back when AED exited.
static void get_active_colours(screen* scr) {
    static char logic[4] = {23, 0, 0xC0, 0};
    VDP_PUTS(logic);

    put_probe_ch(' ');
    const char bg = pixel_colour(PROBE_X, 0);

    // '#' rather than '*': its bars run the full width of the cell, so a column
    // down the middle meets one wherever in the cell the font draws them.
    put_probe_ch('#');
    char fg = bg;
    const int h = scr->charH_ > 0 ? scr->charH_ : 8;
    for (int y = 0; y < h && fg == bg; y++) {
        fg = pixel_colour(PROBE_X, y);
    }
    if (fg == bg) {
        // Nothing in the cell differed from the blank one. Rather than hand the
        // screen back drawn in a single colour, assume what the Agon boots into.
        fg = (bg == 15) ? 0 : 15;
    }

    scr->fg_ = fg;
    scr->bg_ = bg;
    scr->entryFg_ = fg;
    scr->entryBg_ = bg;
    set_colours(scr->fg_, scr->bg_);
}

// Everything the screen layout takes from the current font and mode, in one
// place so that a font change re-runs it rather than patching the fields it
// moved. They are not independent: the row count, the bottom row and the cell
// height all shift together when the font height does, and a set of them that
// half agrees is worse than either state.
static void derive_geometry(screen* scr) {
    const int cols = getsysvar_scrCols();
    const int rows = getsysvar_scrRows();

    scr->rows_ = rows;
    scr->barW_ = cols - 1;
    scr->textX_ = 1;
    scr->cols_ = scr->barW_ - 1;
    scr->bottomY_ = rows - 1;

    // Cell size for the VDU 23,7 movement byte, derived rather than assumed.
    // Every stock Agon mode uses the 8x8 system font, so this is 8 until a font
    // is loaded, and a scroll of the wrong distance tears the text area.
    // Guarded because emitting 0 here would mean "no movement" on an old VDP --
    // the one value that must never reach the wire.
    const int w = cols > 0 ? getsysvar_scrwidth() / cols : 0;
    const int h = rows > 0 ? getsysvar_scrheight() / rows : 0;
    scr->charW_ = (char) (w > 0 && w < 256 ? w : 8);
    scr->charH_ = (char) (h > 0 && h < 256 ? h : 8);
}

screen *scr_init(screen* scr, char cursor) {
    // VDU 23,16,setting,mask -- new = (current AND mask) EOR setting. With
    // mask 0 this sets the whole byte to 1: bit 0, scroll protection. It has
    // never had anything to do with cursor wrap, which is bit 4.
    static char enable_scroll_protect[4] = {23, 16, 1, 0};
    VDP_PUTS(enable_scroll_protect);

    vdp_cursor_enable(false);

    // The screen is laid out as two full-width bars with an inset text area
    // between them. The header and footer span barW_ columns from column 0; the
    // text area is cols_ wide starting at textX_, one column in from each side.
    //
    // The right-hand margin is not cosmetic and must not be reclaimed. The
    // rightmost column of the screen is never written by anything.
    //
    // Writing the last column of a row stops the next keystroke arriving.
    // Measured on hardware with a probe that does nothing else: eleven
    // characters written at column 0 are harmless, and the same eleven written
    // against the right edge stop the following key until something breaks the
    // sequence. Not the byte count, not the number of writes, not the colours
    // around it, not moving the cursor back afterwards -- each ruled out by a
    // probe differing from a working one in one line.
    //
    // The cause is in the VDP, in Context::plotString (video/context/graphics.h):
    //
    //     if (!cursorBehaviour.xHold) {
    //         cursorRight();
    //         if (cursorIsOffRight()) {
    //             checkPagedMode();
    //         }
    //     }
    //
    // and checkPagedMode, with CTRL and SHIFT both down, sets the processor to
    // CtrlShiftPaused -- the BBC "pause output" chord. This editor's selection
    // chord is that chord. Writing the last column is what moves the cursor off
    // the right edge, so it is what arms the pause; a row that stops one column
    // short never makes the call.
    //
    // The pending newline that scroll protection leaves is *not* the cause: it
    // is consumed by cursorAutoNewline -> cursorCR + cursorDown, and none of
    // those reach checkPagedMode. That is why every probe that tried to cancel
    // it with VDU 8 failed -- there was nothing to cancel. The pause is
    // synchronous with the off-right transition, not deferred to the wrap.
    //
    // Measured, not just read: test/probes/lastcol.c writes one character in a
    // chosen column and counts key events beside it. On VDP 2.16.0, six taps
    // with the chord held leave the counter blank until the keys come up when
    // it writes this column, and reading 8 while they are still down when it
    // writes the one before it. Two builds, one column apart.
    //
    // It cannot be switched off. Paged mode does not gate it, VDP variable
    // 0x1022 covers only the CTRL-alone branch, kbEnabled is never cleared, and
    // scrollProtect guards only the post-string newline.
    //
    // One route does exist and is deliberately not taken. checkPagedMode opens
    // with `if (!textCursorActive()) return;`, so text plotted at the graphics
    // cursor -- VDU 5 -- never reaches the pause and could hold this column.
    // It would cost graphics-coordinate positioning per write, in OS units that
    // vary with the screen mode, for text that does not scroll with the rest.
    // Declined 2026-09-05; the column is cheaper.
    //
    // cursorBehaviour.xHold (bit 5) would skip the block outright, and it is
    // old enough to rely on where the pause exists at all: the pause arrived in
    // VDP 2.14.0, xHold has worked since Console8 VDP 2.7.0. Note "worked" --
    // the Quark 1.04 documentation claims bits 4 and 5, but that firmware never
    // implemented them and the cursor always moved right, so on the floor this
    // editor supports, setting bit 5 is a silent no-op.
    //
    // That is what rules it out. There is no way to send it only to the VDPs
    // that honour it: MOS cannot report the VDP version, an unsupported VDP
    // variable "will not be stored, and cannot be read", and the read path
    // needs 2.12.0 itself, so probing means a serial round trip with a timeout.
    // Sent unconditionally to an older VDP the bracket does nothing and the
    // last-column write wraps. Not writing the column works on every VDP.
    //
    // barW_ stops one short of the screen for that reason. The text area then
    // stops one short of barW_ and starts one column in, so the blank column on
    // the right is matched by one on the left and the text sits centred between
    // them rather than pushed against one edge.
    derive_geometry(scr);
    scr->colors_ = getsysvar_scrColours();
    scr->fontLoaded_ = false;

    // Read once, at startup. The boot script is not going to change underneath
    // a running editor, and reading it on the way out -- when the answer is
    // wanted -- would put a file read on the exit path for no gain.
    scr->bootFont_ = bootfont_read(BOOTFONT_PATH);
    scr->cursor_ = cursor;
    scr->lastFname_[0] = 0;
    scr->lastPosW_ = 0;
    scr->footerDrawn_ = false;
    scr->topY_ = 1;
    scr->originX_ = 0;
    scr->selFrom_ = 0;
    scr->selTo_ = 0;
    scr->selOn_ = 0;
    get_active_colours(scr);
    scr_clear(scr);
    scr_show_cursor(scr);
    vdp_cursor_home();
    scr->tab_size_ = SCR_DEFAULT_TAB_SIZE;

    return scr;
}

// The buffer the font is uploaded into. Any 16-bit id would do; this one is
// unlikely to collide with whatever else the machine has put in a buffer.
#define FONT_BUFFER 0x0AED

// Where AED's font goes if the boot script picked the same buffer. Loading over
// it would destroy the font AED is trying to be able to put back -- restoring
// would then select AED's own font and call it the machine's.
#define FONT_BUFFER_ALT 0x0AEE

// Glyphs in a font file. Fixed by the VDP, not by us.
#define FONT_GLYPHS 256

// How much of the file is held at a time. The VDP's buffer write reads a byte
// count off the stream and does not care how the eZ80 divides it up, so the
// font never has to be in memory whole -- which is what keeps a 16-row font
// from costing 4 KiB of the little RAM there is.
#define FONT_CHUNK 256

// The fewest text rows worth starting with: a header, a footer, and something
// between them. A font tall enough to leave less than this would leave the
// editor with no document on screen at all.
#define FONT_MIN_ROWS 4

// Waiting for the VDP to report the new mode. Bounded on purpose: a VDP with no
// font API never answers, and a startup that hangs is worse than one that
// carries on with the geometry it already had. About a second at 60 Hz.
#define FONT_MODE_FRAMES 60

// Waiting for the VDP to answer the probe below. Measured: a VDP that has the
// font API replies within one frame, so this is generous already, and every
// frame of it is a frame added to startup on a VDP that has not.
#define FONT_PROBE_FRAMES 20

// VDU 23, 0, &95, 0, 65535; 0 -- select font 65535, the system font. It is the
// whole of the undo for a font change, which is why the font API was taken over
// reprogramming the system font with VDU 23,n: that has no way back at all.
static const char SYSTEM_FONT[7] = {23, 0, (char) 0x95, 0,
                                    (char) 0xFF, (char) 0xFF, 0};

static void font_put(const char* vdu, int n) {
    mos_puts((char*) vdu, (unsigned) n, 0);
}

// Waits for the VDP to say the mode is settled, and returns whether it did.
// Bounded: a VDP with no font API never answers.
static bool wait_mode_packet(int frames) {
    volatile uint8_t* sysvar = mos_sysvars();

    for (int i = 0; i < frames; i++) {
        waitvblank();
        sysvar = mos_sysvars();
        if ((sysvar[sysvar_vdp_pflags] & vdp_pflag_mode) != 0) {
            return true;
        }
    }

    return false;
}

// Does this VDP have the font API at all?
//
// It was taken as read that this could not be asked: MOS cannot report the VDP
// version, so the settings file was the declaration and getting it wrong meant
// kilobytes of glyph data read as commands. That reasoning was about a general
// version query. Asking about *one feature* is a different question and it does
// have an answer.
//
// Selecting font 65535 is the system font, which is already selected at
// startup, so on a VDP that understands it this changes nothing -- and it
// answers with mode information, which raises vdp_pflag_mode. On a VDP that
// does not, seven bytes are read as something else and no mode packet comes.
//
// Seven bytes is a far smaller thing to get wrong than a 2304-byte upload.
// Measured on MOS 3.0.2 with VDP 1.04: no flag, and the VDP still reports
// 80x60 and 640x480 afterwards -- the probe leaves it healthy. On VDP 2.16.0
// and Console8 the flag arrives within a frame.
static bool font_api_present(const screen* scr) {
    // Selecting the font that is already selected, so that the probe changes
    // nothing whatever the answer is. Which font that is matters: sending 65535
    // on a machine booted into a font of its own would put the stock font up
    // just to ask a question, and leave it there if the load then failed for
    // any other reason. The boot script says which one, when it says anything.
    char probe[7] = {23, 0, (char) 0x95, 0, (char) 0xFF, (char) 0xFF, 0};
    if (scr->bootFont_ >= 0) {
        probe[4] = (char) (scr->bootFont_ & 0xFF);
        probe[5] = (char) ((scr->bootFont_ >> 8) & 0xFF);
    }

    volatile uint8_t* sysvar = mos_sysvars();
    sysvar[sysvar_vdp_pflags] = 0;
    font_put(probe, sizeof(probe));

    return wait_mode_packet(FONT_PROBE_FRAMES);
}

// Streams `size` bytes of the open file straight into a VDP buffer, and returns
// the font's ascent -- the baseline, taken as one past the lowest row any
// capital puts ink on.
//
// The ascent is measured rather than assumed because it cannot be derived from
// the height: a 9-row font made by padding an 8-row one has its baseline at 7,
// exactly where the unpadded font had it, while a font genuinely drawn at 9
// rows does not. It is inert as long as fonts are selected with flags 0 -- the
// VDP only consults it for FONT_SELECTFLAG_ADJUSTBASE -- but a wrong value
// stored is a wrong value waiting.
static char font_upload(char fh, int size, int height, char blo, char bhi) {
    char hdr[8];
    hdr[0] = 23;
    hdr[1] = 0;
    hdr[2] = (char) 0xA0;
    hdr[3] = blo;
    hdr[4] = bhi;
    hdr[5] = 0;                              // BUFFERED_WRITE
    hdr[6] = (char) (size & 0xFF);
    hdr[7] = (char) ((size >> 8) & 0xFF);
    font_put(hdr, sizeof(hdr));

    static char chunk[FONT_CHUNK];
    int ascent = 0;
    int at = 0;

    while (at < size) {
        int want = size - at;
        if (want > FONT_CHUNK) {
            want = FONT_CHUNK;
        }
        const int got = (int) mos_fread(fh, chunk, (unsigned) want);
        if (got <= 0) {
            // The stream is already promised `size` bytes and the VDP is
            // counting them. Stopping short would leave it reading whatever
            // AED sends next as font data -- the editor's first screenful --
            // so make the difference up with blanks.
            memset(chunk, 0, sizeof(chunk));
            for (int left = size - at; left > 0; ) {
                const int n = left > FONT_CHUNK ? FONT_CHUNK : left;
                font_put(chunk, n);
                left -= n;
            }

            return (char) (ascent > 0 ? ascent : height - 1);
        }
        font_put(chunk, got);

        // 'A' to 'Z' sit on the baseline, so the lowest row they reach is it.
        for (int i = 0; i < got; i++) {
            if (chunk[i] == 0) {
                continue;
            }
            const int glyph = (at + i) / height;
            if (glyph < 'A' || glyph > 'Z') {
                continue;
            }
            const int row = ((at + i) % height) + 1;
            if (row > ascent) {
                ascent = row;
            }
        }
        at += got;
    }

    return (char) (ascent > 0 ? ascent : height - 1);
}

// Back to the font the machine started in, and the geometry that goes with it.
// Sending this to a VDP that never took a font is harmless -- font 65535 is the
// system font and selecting it is what it is already using -- but it is only
// worth the round trip when AED changed something, so the caller decides.
void scr_system_font(screen* scr) {
    // Back to the font the machine was in, which is the one the boot script
    // selected if it selected one. Selecting 65535 would be right only for a
    // machine that started in the stock font, and wrong for every other -- it
    // does not restore, it overrides.
    char sel[7] = {23, 0, (char) 0x95, 0, (char) 0xFF, (char) 0xFF, 0};
    if (scr->bootFont_ >= 0) {
        sel[4] = (char) (scr->bootFont_ & 0xFF);
        sel[5] = (char) ((scr->bootFont_ >> 8) & 0xFF);
    }

    volatile uint8_t* sysvar = mos_sysvars();
    sysvar[sysvar_vdp_pflags] = 0;
    font_put(sel, sizeof(sel));
    wait_mode_packet(FONT_MODE_FRAMES);

    derive_geometry(scr);
    scr->fontLoaded_ = false;
    scr_clear(scr);
}

bool scr_load_font(screen* scr, const char* path) {
    if (path == NULL || path[0] == 0) {
        return false;
    }

    char fh = mos_fopen(path, FA_READ);
    if (fh == 0) {
        return false;
    }
    FIL* fil = mos_getfil(fh);
    if (fil == NULL) {
        mos_fclose(fh);

        return false;
    }

    // Compared before narrowing, as tb_load does: objsize is 32 bits and the
    // eZ80's int is 24, so a large file would arrive here as a small number.
    // A font is 256 glyphs of `height` bytes and nothing else, so the height is
    // the size divided by 256 -- there is no header to read it from, and a size
    // that is not a multiple of 256 is not a font.
    if (fil->obj.objsize > (uint32_t) (FONT_GLYPHS * 255)) {
        mos_fclose(fh);

        return false;
    }
    const int size = (int) fil->obj.objsize;
    if (size <= 0 || (size % FONT_GLYPHS) != 0) {
        mos_fclose(fh);

        return false;
    }
    const int height = size / FONT_GLYPHS;

    // Checked before anything is sent, because there is no way back: the mode
    // packet arrives whether or not the font took, so a font that leaves no
    // room to edit in cannot be detected afterwards and undone.
    const int px = getsysvar_scrheight();
    if (height < 1 || height > 255 || px / height < FONT_MIN_ROWS) {
        mos_fclose(fh);

        return false;
    }

    // Last of the checks and the only one that costs anything on the wire, so
    // it goes after the ones that do not. This is the whole difference between
    // a setting that is safe to try and one that ruins the screen when the VDP
    // turns out to be older than the user thought.
    if (!font_api_present(scr)) {
        mos_fclose(fh);

        return false;
    }

    const int buf = scr->bootFont_ == FONT_BUFFER ? FONT_BUFFER_ALT : FONT_BUFFER;
    const char blo = (char) (buf & 0xFF);
    const char bhi = (char) ((buf >> 8) & 0xFF);

    char clear[6] = {23, 0, (char) 0xA0, blo, bhi, 2};   // BUFFERED_CLEAR
    font_put(clear, sizeof(clear));

    const char ascent = font_upload(fh, size, height, blo, bhi);
    mos_fclose(fh);

    char create[10];
    create[0] = 23;
    create[1] = 0;
    create[2] = (char) 0x95;
    create[3] = 1;                           // create font from buffer
    create[4] = blo;
    create[5] = bhi;
    create[6] = 8;                           // width; the VDP has no variable-width fonts
    create[7] = (char) height;
    create[8] = ascent;
    create[9] = 0;                           // flags
    font_put(create, sizeof(create));

    char select[7] = {23, 0, (char) 0x95, 0, blo, bhi, 0};
    volatile uint8_t* sysvar = mos_sysvars();
    sysvar[sysvar_vdp_pflags] = 0;
    font_put(select, sizeof(select));

    // The VDP answers a font change with mode information, and the sysvars hold
    // the old row count until it lands. Reading them early paints the editor
    // off the bottom of the screen.
    //
    // The flag says the VDP replied, not that the font took: FONT_SELECT sends
    // mode information before it knows whether the font exists, so a font
    // rejected for its size raises it just the same. What follows treats it
    // only as "the numbers are now current", which is true either way.
    wait_mode_packet(FONT_MODE_FRAMES);

    derive_geometry(scr);

    // Everything below this is laid out from what MOS reports, and MOS only
    // learns the new mode when the VDP's packet reaches it. If that never
    // happens the sysvars still describe the old font, and the editor would lay
    // out sixty rows on a screen that now has forty-eight -- painting straight
    // off the bottom, scrolling what it just drew away, and leaving the cursor
    // somewhere the screen no longer has. A blank screen and rubbish on every
    // keypress, from numbers that looked perfectly reasonable.
    //
    // The font's own height is the one number that cannot be stale, so it is
    // what the reported geometry gets checked against. A mismatch means the
    // font did not take or MOS did not hear about it; either way the safe state
    // is the font the machine started with.
    if (scr->charH_ != (char) height) {
        scr_system_font(scr);

        return false;
    }

    // Sent, so it has to be put back on the way out whatever the VDP made of
    // it. Not conditional on the geometry having changed: a font the same
    // height as the system one changes no number here and is still a different
    // font on the screen.
    scr->fontLoaded_ = true;

    // Everything on screen was drawn at the old cell size, and the footer's row
    // has just moved. scr_clear repaints and drops the footer's cache with it,
    // which is what stops the next scr_footer deciding nothing has changed and
    // leaving the old row where it is.
    scr_clear(scr);

    return true;
}

void scr_set_ctrl_pause_frames(screen* scr, int frames) {
    (void) scr;
    if (frames < 0 || frames > 255) {
        return;
    }

    // VDU 23, 0, &F8, flag; value; -- flag and value are 16-bit, low byte
    // first. The VDU variable block starts at 0x1000 and the frame count is
    // 0x22 within it.
    char vdu[7];
    vdu[0] = 23;
    vdu[1] = 0;
    vdu[2] = (char) 0xF8;
    vdu[3] = 0x22;
    vdu[4] = 0x10;
    vdu[5] = (char) (frames & 0xFF);
    vdu[6] = 0;
    mos_puts(vdu, sizeof(vdu), 0);
}

void scr_set_tab_size(screen* scr, char tab_size) {
    if (tab_size < 1) {
        tab_size = 1;
    } else if (tab_size > SCR_MAX_TAB_SIZE) {
        tab_size = SCR_MAX_TAB_SIZE;
    }
    scr->tab_size_ = tab_size;
}

char scr_tab_size(screen* scr) {
    return scr->tab_size_;
}

void scr_set_scheme(screen* scr, char fg, char bg) {
    if (fg < 0 || bg < 0 || fg >= scr->colors_ || bg >= scr->colors_) {
        return;   // outside what this screen mode can show
    }
    scr->fg_ = fg;
    scr->bg_ = bg;
    set_colours(scr->fg_, scr->bg_);
}

char scr_fg(screen* scr) {
    return scr->fg_;
}

char scr_bg(screen* scr) {
    return scr->bg_;
}

// Maps a byte offset within a line onto the screen column it renders at. A tab
// advances to the next multiple of the tab width; every other byte is one
// column wide. With no tabs in the line this is simply `len`, which is why this
// is behaviour-preserving today.
int scr_column_of(screen* scr, const char* line, int len) {
    if (line == NULL || len <= 0) {
        return 0;
    }

    const int tab = scr->tab_size_ > 0 ? scr->tab_size_ : 1;
    int col = 0;
    for (int i = 0; i < len; i++) {
        if (line[i] == '\t') {
            col += tab - (col % tab);
        } else {
            col++;
        }
    }

    return col;
}

int scr_byte_at(screen* scr, const char* line, int len, int column) {
    if (line == NULL || len <= 0 || column <= 0) {
        return 0;
    }

    const int tab = scr->tab_size_ > 0 ? scr->tab_size_ : 1;
    int col = 0;
    for (int i = 0; i < len; i++) {
        if (col >= column) {
            return i;
        }
        if (line[i] == '\t') {
            col += tab - (col % tab);
        } else {
            col++;
        }
    }

    return len;
}

int scr_place_cursor(screen* scr, const char* line, int len) {
    const int col = scr_column_of(scr, line, len);
    const int width = scr->cols_ > 0 ? scr->cols_ : 1;
    const int origin = scr->originX_;

    // Keep the cursor inside the window by moving the window, not by pinning
    // the cursor to an edge and losing track of where it really is.
    if (col < origin) {
        // Scrolling left: if everything up to the cursor already fits, show the
        // line from its start rather than leaving the window parked mid-line,
        // which would blank out every shorter row on screen.
        scr->originX_ = col < width ? 0 : col;
    } else if (col > origin + width - 1) {
        scr->originX_ = col - (width - 1);
    }

    int x = col - scr->originX_;
    if (x > width - 1) {
        x = width - 1;
    }
    if (x < 0) {
        x = 0;
    }
    scr->currX_ = (char) x;

    return scr->originX_ - origin;
}

void scr_destroy(screen* scr) {
    // mask 1 keeps only the current bit 0 and the EOR flips it, so this clears
    // scroll protection and leaves the rest of the byte zeroed.
    static char disable_scroll_protect[4] = {23, 16, 1, 1};
    VDP_PUTS(disable_scroll_protect);
    vdp_cursor_enable(true);

    // Hand the machine back as it was found. Font 65535 is the system font;
    // selecting it is the whole of the undo, which is why the font API was
    // taken over reprogramming the system font with VDU 23,n -- that has no
    // way back at all.
    if (scr->fontLoaded_) {
        char sel[7] = {23, 0, (char) 0x95, 0, (char) 0xFF, (char) 0xFF, 0};
        if (scr->bootFont_ >= 0) {
            sel[4] = (char) (scr->bootFont_ & 0xFF);
            sel[5] = (char) ((scr->bootFont_ >> 8) & 0xFF);
        }
        mos_puts(sel, sizeof(sel), 0);
        scr->fontLoaded_ = false;
    }

    // Colours first, then clear, so the cleared screen is in the user's
    // background and not AED's.
    set_colours(scr->entryFg_, scr->entryBg_);
    vdp_clear_screen();

    scr->currX_ = 0;
    scr->currY_ = 0;
    scr->rows_ = 0;
    scr->cols_ = 0;
    scr->barW_ = 0;
    scr->textX_ = 0;
}

void scr_footer_invalidate(screen* scr) {
    scr->footerDrawn_ = false;
}

// Defined below with the rest of the viewport handling; the footer needs them
// to paint its last column, which is above their definitions.
static void define_viewport(char left, char bottom, char right, char top);
static void reset_viewport(void);

// How wide the position field is for these numbers: four columns for the line
// and six for the column, or more when a number does not fit. A document can
// outgrow four digits of line number, and the row still has to total cols_.
static int position_width(int x, int y) {
    static char digits[16];

    i2s(y, digits, 16);
    int w = strlen(digits);
    if (w < 4) {
        w = 4;
    }
    i2s(x, digits, 16);
    int xw = strlen(digits);
    if (xw < 6) {
        xw = 6;
    }

    return w + 1 + xw;
}

// Writes just "  12,34    " at the cursor. Padding is to the field width, and
// a number that fills the field gets none -- the old code added a full field's
// worth of spaces instead of none once the line number reached four digits,
// which pushed the column out of the footer and off the row.
static void footer_position(int x, int y) {
    static char digits[16];

    i2s(y, digits, 16);
    int dsz = strlen(digits);
    out_run(' ', 4 - dsz);
    out_str(digits, dsz);
    out_ch(',');

    i2s(x, digits, 16);
    dsz = strlen(digits);
    out_str(digits, dsz);
    out_run(' ', 6 - dsz);
}

void scr_footer(screen* scr, char* fname, bool dirty, int x, int y) {
    static char* no_file = "[NO FILE]";
    if (fname == NULL) {
        fname = no_file;
    }
    const int fnsz = strlen(fname);

    // Nothing has changed, so there is nothing to send. The caller repaints the
    // footer on every pass of the event loop; on all but a handful of those the
    // three things it shows are the same as last time.
    const int posw = position_width(x, y);
    const bool same_file = scr->footerDrawn_
        && scr->lastDirty_ == dirty
        && scr->lastPosW_ == posw
        && strcmp(scr->lastFname_, fname) == 0;
    if (same_file && scr->lastX_ == x && scr->lastY_ == y) {
        return;
    }
    // Remember it before drawing, not after: a name too long for the cache is
    // truncated the same way on every pass, so it still compares equal and the
    // repaint still stops.
    strncpy(scr->lastFname_, fname, sizeof(scr->lastFname_) - 1);
    scr->lastFname_[sizeof(scr->lastFname_) - 1] = 0;
    scr->lastDirty_ = dirty;
    scr->lastX_ = x;
    scr->lastY_ = y;
    scr->lastPosW_ = posw;
    scr->footerDrawn_ = true;

    // The common case by far: the cursor moved and nothing else did. Only the
    // position field can differ, so only it is sent. Redrawing the whole row
    // for this costs a MOS call per column -- the padding is a putchar loop --
    // and the row is mostly spaces that were already spaces.
    if (same_file) {
        vdp_cursor_tab((char) (scr->barW_ - posw), scr->bottomY_);
        set_colours(scr->bg_, scr->fg_);
        footer_position(x, y);
        set_colours(scr->fg_, scr->bg_);
        scr_tab(scr, scr->currX_, scr->currY_);

        return;
    }

    // Paint the whole row, last column included, before writing any of it.
    //
    // The bar can only *print* as far as barW_: the rightmost column of the
    // screen is the one that must never hold a character, because printing
    // there moves the cursor off the right edge and that is what arms the VDP's
    // CTRL+SHIFT pause. So the footer used to stop one short and leave that cell
    // showing the document's colours, with the bar visibly not reaching the edge.
    //
    // A colour does not need a character. VDU 12 fills the current text viewport
    // with the text background and homes the cursor -- Context::cls does not
    // call cursorRight and never reaches checkPagedMode -- so a viewport over
    // the footer row, cleared in the bar's own colours, covers every column of
    // it including the one nothing may be printed in.
    set_colours(scr->bg_, scr->fg_);
    define_viewport(0, scr->bottomY_, (char) scr->barW_, scr->bottomY_);
    vdp_clear_screen();
    reset_viewport();

    vdp_cursor_tab(0, scr->bottomY_);

    out_str(fname, fnsz);
    out_ch(dirty ? '*' : ' ');
    out_ch(' ');
    out_run(' ', scr->barW_ - fnsz - 2 - posw);
    footer_position(x, y);

    set_colours(scr->fg_, scr->bg_);
    scr_tab(scr, scr->currX_, scr->currY_);
}

char* title = "AED: Another Text Editor";
void scr_clear(screen* scr) {
    // The footer goes with everything else, so it has to be drawn again.
    scr->footerDrawn_ = false;
    vdp_clear_screen();
    vdp_cursor_home();
    vdp_cursor_tab(0,0);
    // Split the remainder rather than halving it twice: an odd number of spare
    // columns would otherwise lose one, leaving the header a column short of
    // the bar it is supposed to span -- and now that the text area reaches the
    // column before the bar's last, that shortfall is visible as text sticking
    // out past the rule above it.
    const int len = strlen(title);
    const int spare = scr->barW_ - len;
    const int left = spare / 2;
    const int right = spare - left;
    out_run('-', left);
    set_colours(scr->bg_, scr->fg_);
    out_str(title, strlen(title));
    set_colours(scr->fg_, scr->bg_);
    out_run('-', right);
    out_flush();
    scr->currX_ = 0;
    scr->currY_ = scr->topY_;
    scr->originX_ = 0;
    scr_tab(scr, scr->currX_, scr->currY_);
}

void scr_hide_cursor_ch(screen* scr, char ch) {
    ch = cursor_glyph(scr, ch);
    out_flush();

    char vdu[6];
    vdu[0] = 17;
    vdu[1] = scr->fg_;
    vdu[2] = 17;
    vdu[3] = (char) (scr->bg_ + 128);
    vdu[4] = ch;
    vdu[5] = 8;
    mos_puts(vdu, sizeof(vdu), 0);
}

static void scr_hide_cursor(screen* scr) {
    scr_hide_cursor_ch(scr, scr->cursor_);
}

// `prefix`/`psz` describe the line up to and including the character just
// inserted, so the cursor lands after it.
int scr_putc(screen* scr, char ch, char* prefix, int psz, char* suffix, int ssz) {
    (void) ch;
    scr_hide_cursor(scr);

    // Repaint from where the inserted character starts, not from the cursor:
    // the cursor now sits after it, and a tab starts several columns back.
    const int at = scr_column_of(scr, prefix, psz > 0 ? psz - 1 : 0);
    const int scrolled = scr_place_cursor(scr, prefix, psz);
    if (scrolled == 0) {
        scr_paint_from(scr, scr->currY_, prefix, psz, suffix, ssz, at);
        scr_sync_cursor(scr);
        scr_show_cursor_ch(scr,
                           (suffix != NULL && ssz > 0) ? suffix[0] : scr->cursor_);
    }

    return scrolled;
}

void scr_del(screen* scr, char* suffix, int sz) {
    scr_paint_tail(scr, suffix, sz);
    scr_show_cursor_ch(scr, sz > 0 ? suffix[0] : scr->cursor_);
}

// `prefix`/`psz` describe the line after the deletion, so the cursor lands on
// the character that moved into the deleted position.
int scr_bksp(screen* scr, char* prefix, int psz, char* suffix, int ssz) {
    scr_hide_cursor(scr);
    const int scrolled = scr_place_cursor(scr, prefix, psz);
    if (scrolled == 0) {
        scr_paint_tail(scr, suffix, ssz);
        scr_sync_cursor(scr);
        scr_show_cursor_ch(scr, ssz > 0 ? suffix[0] : scr->cursor_);
    }

    return scrolled;
}





int scr_up(screen* scr, char from_ch, char to_ch,
           const char* pre, int presz) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_--;
    const int scrolled = scr_place_cursor(scr, pre, presz);
    if (scrolled == 0) {
        scr_tab(scr, scr->currX_, scr->currY_);
        scr_show_cursor_ch(scr, to_ch);
    }

    return scrolled;
}

int scr_down(screen* scr, char from_ch, char to_ch,
           const char* pre, int presz) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_++;
    const int scrolled = scr_place_cursor(scr, pre, presz);
    if (scrolled == 0) {
        scr_tab(scr, scr->currX_, scr->currY_);
        scr_show_cursor_ch(scr, to_ch);
    }

    return scrolled;
}

// VDU 28, left, bottom, right, top -- define a text viewport.
// VDU 28 takes character *positions*, so `right` is the index of the last
// column, not the number of columns. Every call here passed cols_, which is one
// past the end of the screen -- an out-of-range viewport, and the VDP is not
// documented to say what it does with one. What it did: the horizontal scroll
// stopped working at the right edge, and writes afterwards went down a column
// instead of along the row.
static void define_viewport(char left, char bottom, char right, char top) {
    static char viewport[5] = {28, 0, 0, 0, 0};
    viewport[1] = left;
    viewport[2] = bottom;
    viewport[3] = right;
    viewport[4] = top;
    VDP_PUTS(viewport);
}

// VDU 26 -- restore the default viewport.
static void reset_viewport(void) {
    putchar(26);
}

void scr_clear_textarea(screen* scr, char top, char bottom) {
    // The viewport includes `bottom`, and the callers that refresh the whole
    // screen pass bottomY_ -- which is the footer row. So this erases the
    // footer even though nothing here draws it back. That went unnoticed while
    // the footer was redrawn on every pass of the event loop; now that an
    // unchanged one sends nothing, it has to be said out loud or a full
    // refresh leaves the row blank until the cursor happens to move.
    if (bottom >= scr->bottomY_) {
        scr->footerDrawn_ = false;
    }
    define_viewport(scr->textX_, bottom, (char) (scr->textX_ + scr->cols_ - 1), top);
    vdp_clear_screen();
    reset_viewport();

    // VDU 26 homes the text cursor as well as resetting the viewport, so the
    // VDP is left pointing at 0,0 -- the title bar. Anything drawn next lands
    // there, and scr_show_cursor_ch draws wherever the cursor is rather than
    // tabbing first: the cursor block appeared on the title bar and ate the
    // dash under it. Putting the cursor back here rather than in the callers,
    // because the surprise belongs to this function.
    scr_sync_cursor(scr);
}

// Emits one line's worth of cells starting at document column `from_col`,
// expanding tabs, stopping after `budget` screen columns. Returns the document
// column reached, so a caller can continue across the gap split.
// Swaps the colours on the way into the selection and back on the way out, so
// a highlighted run costs two colour changes rather than one per character --
// which matters on a VDP behind a serial link.
static void highlight(screen* scr, int col) {
    const char want = (col >= scr->selFrom_ && col < scr->selTo_) ? 1 : 0;
    if (want == scr->selOn_) {
        return;
    }
    if (want) {
        set_colours(scr->bg_, scr->fg_);
    } else {
        set_colours(scr->fg_, scr->bg_);
    }
    scr->selOn_ = want;
}

static int emit_span(screen* scr, const char* buf, int sz, int col,
                     int from_col, int stop_col) {
    const int tab = scr->tab_size_ > 0 ? scr->tab_size_ : 1;

    for (int i = 0; i < sz && col < stop_col; i++) {
        int width = 1;
        if (buf[i] == '\t') {
            width = tab - (col % tab);
        }
        for (int w = 0; w < width && col < stop_col; w++, col++) {
            if (col >= from_col) {
                highlight(scr, col);
                out_ch(buf[i] == '\t' ? ' ' : buf[i]);
            }
        }
        continue;
    }

    return col;
}

// Paints the row from document column `from_col` rightwards. Columns left of
// the window, or left of from_col, are skipped rather than redrawn.
void scr_paint_span(screen* scr, char ypos, const char* pre, int presz,
                    const char* suf, int sufsz, int from_col, int to_col) {
    const int edge = scr->originX_ + scr->cols_;
    const int from = from_col > scr->originX_ ? from_col : scr->originX_;
    const int stop = to_col < edge ? to_col : edge;
    if (from >= stop) {
        return;
    }

    scr_tab(scr, from - scr->originX_, ypos);
    scr->selOn_ = 0;
    int col = 0;
    if (pre != NULL && presz > 0) {
        col = emit_span(scr, pre, presz, col, from, stop);
    }
    if (suf != NULL && sufsz > 0) {
        col = emit_span(scr, suf, sufsz, col, from, stop);
    }
    // The padding past the end of the text is highlighted too when the
    // selection runs through the line break, which is how a selected newline
    // shows up as anything at all.
    for (; col < stop; col++) {
        if (col >= from) {
            highlight(scr, col);
            out_ch(' ');
        }
    }
    out_flush();
    if (scr->selOn_) {
        set_colours(scr->fg_, scr->bg_);
        scr->selOn_ = 0;
    }
    scr_sync_cursor(scr);
}

void scr_paint_from(screen* scr, char ypos, const char* pre, int presz,
                    const char* suf, int sufsz, int from_col) {
    scr_paint_span(scr, ypos, pre, presz, suf, sufsz, from_col,
                   scr->originX_ + scr->cols_);
}

void scr_write_line_sel(screen* scr, char ypos, char* buf, int sz,
                        int from_col, int to_col) {
    scr_write_line_span(scr, ypos, buf, sz, from_col, to_col,
                        scr->originX_, scr->originX_ + scr->cols_);
}

void scr_write_line_span(screen* scr, char ypos, char* buf, int sz,
                         int from_col, int to_col, int paint_from,
                         int paint_to) {
    scr->selFrom_ = from_col;
    scr->selTo_ = to_col;
    scr_paint_span(scr, ypos, NULL, 0, buf, sz, paint_from, paint_to);
    scr->selFrom_ = 0;
    scr->selTo_ = 0;
}

void scr_paint_row(screen* scr, char ypos, const char* pre, int presz,
                   const char* suf, int sufsz) {
    scr_paint_from(scr, ypos, pre, presz, suf, sufsz, scr->originX_);
}

void scr_paint_tail(screen* scr, const char* suf, int sufsz) {
    const int at = scr->originX_ + scr->currX_;
    const int stop = scr->originX_ + scr->cols_;

    scr_tab(scr, scr->currX_, scr->currY_);
    int col = at;
    if (suf != NULL && sufsz > 0) {
        col = emit_span(scr, suf, sufsz, col, at, stop);
    }
    out_run(' ', stop - col);
    scr_sync_cursor(scr);
}

// Returns true when the horizontal origin moved. The origin is screen-wide, so
// every other visible row is then drawn against the old one and the caller must
// repaint the text area -- the view cannot, it has no access to the document.
int scr_move_cursor(screen* scr, char from_ch, char to_ch,
                    const char* pre, int presz) {
    scr_hide_cursor_ch(scr, from_ch);
    const int scrolled = scr_place_cursor(scr, pre, presz);
    if (scrolled == 0) {
        scr_sync_cursor(scr);
        scr_show_cursor_ch(scr, to_ch);
    }

    return scrolled;
}

void scr_write_line(screen* scr, char ypos, char* buf, int sz) {
    scr_paint_row(scr, ypos, NULL, 0, buf, sz);
}

void scr_overwrite_line(screen* scr, char ypos, char* buf, int sz, int psz) {
    (void) psz;   // scr_paint_row always pads to the full width
    scr_paint_row(scr, ypos, NULL, 0, buf, sz);
}

void scr_tab(screen* scr, int col, char row) {
    vdp_cursor_tab((char) (col + scr->textX_), row);
}

void scr_tab_bar(screen* scr, int col, char row) {
    (void) scr;
    vdp_cursor_tab((char) col, row);
}

void scr_bar_line(screen* scr, char row, const char* buf, int sz) {
    if (sz > scr->barW_) {
        sz = scr->barW_;
    }
    vdp_cursor_tab(0, row);
    out_str(buf, sz);
    out_run(' ', scr->barW_ - sz);
    out_flush();
}

void scr_sync_cursor(screen* scr) {
    out_flush();
    scr_tab(scr, scr->currX_, scr->currY_);
}

// VDU 23,7,extent,direction,movement -- scroll the current text viewport by one
// character row. Direction 2 is down, 3 is up.
static void scroll_region(
        screen* scr, char topY, char bottomY, const char* vdu, char sz,
        char* line, int lsz, char ch) {
    define_viewport(scr->textX_, bottomY, (char) (scr->textX_ + scr->cols_ - 1), topY);
    mos_puts((char*) vdu, sz, 0);
    reset_viewport();
    scr_paint_row(scr, scr->currY_, NULL, 0, line, lsz);
    scr_sync_cursor(scr);
    scr_show_cursor_ch(scr, ch);
}

// VDU 23,7,extent,direction,movement -- direction 0 moves the content right,
// 1 moves it left. Scrolling the window right means moving the content left.
void scr_scroll_h(screen* scr, int cols) {
    if (cols == 0) {
        return;
    }

    static char scroll[5] = {23, 7, 0, 0, 0};
    int n = cols;
    if (n < 0) {
        n = -n;
        scroll[3] = 0;   // window left  -> content right
    } else {
        scroll[3] = 1;   // window right -> content left
    }
    scroll[4] = scr->charW_;   // one character cell, in pixels

    define_viewport(scr->textX_, scr->bottomY_ - 1, (char) (scr->textX_ + scr->cols_ - 1), scr->topY_);
    for (int i = 0; i < n; i++) {
        VDP_PUTS(scroll);
    }
    reset_viewport();
}

char scr_glyph_at(screen* scr, const char* line, int len, int col) {
    if (line == NULL || len <= 0 || col < 0) {
        return ' ';
    }

    const int tab = scr->tab_size_ > 0 ? scr->tab_size_ : 1;
    int at = 0;
    for (int i = 0; i < len; i++) {
        const int width = line[i] == '\t' ? tab - (at % tab) : 1;
        if (col < at + width) {
            // Inside a tab's expansion, or past the start of a normal cell.
            return (line[i] == '\t' || col > at) ? ' ' : line[i];
        }
        at += width;
    }

    return ' ';
}

void scr_put_at(screen* scr, char sx, char sy, char ch) {
    scr_tab(scr, sx, sy);
    putchar(ch);
    scr_sync_cursor(scr);
}

void scr_scroll_rows_up(screen* scr, char topY, char bottomY, int rows) {
    if (rows <= 0 || topY > bottomY) {
        return;
    }
    const char up[] = {23, 7, 0, 3, scr->charH_};
    define_viewport(scr->textX_, bottomY,
                    (char) (scr->textX_ + scr->cols_ - 1), topY);
    for (int i = 0; i < rows; i++) {
        mos_puts((char*) up, sizeof(up), 0);
    }
    reset_viewport();
}

void scr_scroll_rows_down(screen* scr, char topY, char bottomY, int rows) {
    if (rows <= 0 || topY > bottomY) {
        return;
    }
    const char down[] = {23, 7, 0, 2, scr->charH_};
    define_viewport(scr->textX_, bottomY,
                    (char) (scr->textX_ + scr->cols_ - 1), topY);
    for (int i = 0; i < rows; i++) {
        mos_puts((char*) down, sizeof(down), 0);
    }
    reset_viewport();
}

void scr_scroll_down(
        screen* scr, char topY, char bottomY, char* line, int sz, char ch) {
    const char down[] = {23, 7, 0, 2, scr->charH_};
    scroll_region(scr, topY, bottomY, down, sizeof(down), line, sz, ch);
}

void scr_scroll_up(
        screen* scr, char topY, char bottomY, char* line, int sz, char ch) {
    const char up[] = {23, 7, 0, 3, scr->charH_};
    scroll_region(scr, topY, bottomY, up, sizeof(up), line, sz, ch);
}

void scr_erase(screen* scr, int sz) {
    sz = sz + scr->currX_;
    if (sz > scr->cols_) {
        sz = scr->cols_;
    }
    out_run(' ', sz - scr->currX_);
    out_flush();
    scr_tab(scr, scr->currX_, scr->currY_);
}

