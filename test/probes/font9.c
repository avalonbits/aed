#include <stdio.h>
#include <stdint.h>
#include <agon/vdp.h>
#include <agon/mos.h>

/* Does the VDP accept a font height that is not a multiple of 8?
 *
 * It matters because an 8x8 font has no room for a blank row between lines --
 * the stock Agon font, IBM VGA 8x8 and unscii-8 all put descenders on the last
 * row -- and every mechanical way of freeing one wrecks the glyphs. But height
 * is a plain parameter of the create command, and the VDP's only size check is
 * that the buffer equals 256 * ((width + 7) >> 3) * height. If that is the whole
 * of it, an 8x9 font is legal: eight rows of glyph and a blank ninth, which buys
 * the separation without redrawing anything and without giving up a column.
 *
 * Loads FONT_PATH, uploads it, selects it, and reports the geometry before and
 * after. Build with FONT_H matched to the file: a FONT_H-row font is
 * 256 * FONT_H bytes.
 *
 * Measured on VDP 2.16.0 / MOS 3.0.2 with an 8x9 build: 80x60 before, 80x53
 * after, and the VDP logs "Created text cursor bitmap 8x9" -- so the height is
 * honoured throughout, not just stored. See .internal/docs/FONTS.md.
 *
 * Any key quits and puts the system font back. */
#define FONT_PATH  "/fonts/agon.F09"
#define FONT_W     8
#define FONT_H     9
#define BUF_ID     100

static char font[256 * FONT_H];

/* Nothing printed on the emulated screen reaches the host when the emulator is
 * run without a display, which is the only way to run it here. So report
 * through the VDP's own debug log as well: asking for debug info on a buffer
 * that does not exist makes it log the id, and the id is a free 16-bit word.
 * Surfaced by fab-agon-emulator's --verbose. */
static void log_word(uint16_t v) {
    VDU_FONT dbg = {23, 0, 0x95, 0x20, v};

    mos_puts((char*) &dbg, sizeof(dbg), 0);
}

static void report(const char* when) {
    printf("%-7s cols=%d rows=%d  %dx%d px\r\n", when,
           getsysvar_scrCols(), getsysvar_scrRows(),
           getsysvar_scrwidth(), getsysvar_scrheight());
    log_word(9000 + getsysvar_scrRows());
    log_word(8000 + getsysvar_scrCols());
}

int main(void) {
    uint8_t f = mos_fopen(FONT_PATH, FA_READ);
    if (f == 0) {
        printf("cannot open %s\r\n", FONT_PATH);

        return 1;
    }
    const unsigned n = mos_fread(f, font, sizeof(font));
    mos_fclose(f);
    if (n != sizeof(font)) {
        printf("read %u bytes, wanted %u\r\n", n, (unsigned) sizeof(font));

        return 1;
    }

    vdp_clear_screen();
    report("before");

    vdp_adv_clear_buffer(BUF_ID);
    vdp_adv_write_block_data(BUF_ID, sizeof(font), font);

    /* VDU 23, 0, &95, 1, bufferId; width, height, ascent, flags */
    VDU_FONT_B_B_B_B create = {23, 0, 0x95, 1, BUF_ID, FONT_W, FONT_H, FONT_H - 2, 0};
    mos_puts((char*) &create, sizeof(create), 0);

    /* VDU 23, 0, &95, 0, bufferId; flags. The struct carries one byte too many
     * for this command, hence the - 1. */
    VDU_FONT_B_B select = {23, 0, 0x95, 0, BUF_ID, 0, 0};
    volatile uint8_t* sysvar = mos_sysvars();
    sysvar[sysvar_vdp_pflags] = 0;
    mos_puts((char*) &select, sizeof(select) - 1, 0);

    /* The VDP sends a mode packet after a font change. Which flag it raises was
     * the open question; wait a fixed spell and report what arrived rather than
     * blocking on a guess. Measured: 0x10, vdp_pflag_mode. */
    for (int i = 0; i < 30; i++) {
        waitvblank();
    }
    sysvar = mos_sysvars();
    log_word(7000 + sysvar[sysvar_vdp_pflags]);

    report("after");
    printf("happy\r\nEjump\r\ngypsy jag\r\nThe quick brown fox.\r\n");

    /* Makes the VDP print the font's real geometry to its own debug log. */
    log_word(BUF_ID);

    getch();

    /* Hand the machine back with the system font, as scr_destroy does for
     * colours. Font 65535 is the system font. */
    VDU_FONT restore = {23, 0, 0x95, 0, 65535};
    char flags = 0;
    mos_puts((char*) &restore, sizeof(restore), 0);
    mos_puts(&flags, 1, 0);
    vdp_clear_screen();

    return 0;
}
