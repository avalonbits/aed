#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <agon/vdp.h>
#include <agon/mos.h>

/* Loads a font and shows the result, so a person can look at it.
 *
 * Everything AED's scr_load_font does, on its own and with the workings on
 * screen: whether the VDP has the font API at all, what height the file
 * implies, whether the mode packet came back, what the geometry became, and --
 * the question the rest is in aid of -- whether glyphs actually render
 * afterwards. That last one is checked rather than eyeballed: a known string is
 * written and read back off the screen with VDU 23,0,&83.
 *
 *     fonttest                       -- /config/aed/unscii8x9.bin
 *     fonttest /config/aed/agon8x9.bin
 *     fonttest /config/aed/unscii_16.bin
 *
 * The sample deliberately puts descenders directly above capitals, which is
 * the thing an 8x8 font cannot do anything about: with a 9-row font there is a
 * blank row between the two, and with an 8-row font there is not.
 *
 * A key press between each stage, so there is time to look. The system font
 * goes back on the way out. */
#define BUF_ID       0x0AED
#define GLYPHS       256
#define CHUNK        256
#define PROBE_FRAMES 20
#define MODE_FRAMES  60

static char chunk[CHUNK];

static void put(const char* vdu, int n) {
    mos_puts((char*) vdu, (unsigned) n, 0);
}

static bool wait_mode(int frames) {
    volatile uint8_t* sv = mos_sysvars();

    for (int i = 0; i < frames; i++) {
        waitvblank();
        sv = mos_sysvars();
        if ((sv[sysvar_vdp_pflags] & vdp_pflag_mode) != 0) {
            return true;
        }
    }

    return false;
}

/* Selecting the system font is a no-op on a VDP that has the font API, and it
 * answers with mode information. On one that has not, nothing comes back. */
static bool have_font_api(void) {
    static char probe[7] = {23, 0, (char) 0x95, 0, (char) 0xFF, (char) 0xFF, 0};
    volatile uint8_t* sv = mos_sysvars();

    sv[sysvar_vdp_pflags] = 0;
    put(probe, sizeof(probe));

    return wait_mode(PROBE_FRAMES);
}

static char read_at(int x, int y) {
    char vdu[7] = {23, 0, (char) 0x83,
                   (char) (x & 0xFF), (char) (x >> 8),
                   (char) (y & 0xFF), (char) (y >> 8)};
    volatile uint8_t* sv = mos_sysvars();

    sv[sysvar_vdp_pflags] = 0;
    put(vdu, sizeof(vdu));
    for (long i = 0; i < 3000000L; i++) {
        sv = mos_sysvars();
        if ((sv[sysvar_vdp_pflags] & vdp_pflag_scrchar) != 0) {
            return (char) sv[sysvar_scrchar];
        }
    }

    return 0;
}

/* Writes a known string on `row` and reads it straight back. Anything other
 * than "renders" means the glyphs are not reaching the screen, whatever the
 * geometry says. */
static void render_check(int row) {
    static const char want[] = "RENDERCHECK0123";
    const int n = (int) sizeof(want) - 1;

    vdp_cursor_tab(0, (char) row);
    printf("%s", want);

    int same = 0;
    for (int i = 0; i < n; i++) {
        if (read_at(i, row) == want[i]) {
            same++;
        }
    }
    vdp_cursor_tab(0, (char) (row + 1));
    printf("read back %d of %d -- %s", same, n,
           same == n ? "renders" : (same == 0 ? "NOTHING RENDERS" : "partial"));
}

static void sample(int row) {
    vdp_cursor_tab(0, (char) row);
    printf("happy gypsy jumping quaff");
    vdp_cursor_tab(0, (char) (row + 1));
    printf("Editor {[(<= >=)]} 0123456789");
    vdp_cursor_tab(0, (char) (row + 2));
    printf("The quick brown fox jumps over");
    vdp_cursor_tab(0, (char) (row + 3));
    printf("jgpqy JGPQY |_iIl1 oO0 ,.;:!?");
}

static void geometry(int row, const char* tag) {
    vdp_cursor_tab(0, (char) row);
    printf("%s: %d cols x %d rows, %dx%d px, cell %dx%d",
           tag, getsysvar_scrCols(), getsysvar_scrRows(),
           getsysvar_scrwidth(), getsysvar_scrheight(),
           getsysvar_scrCols() ? getsysvar_scrwidth() / getsysvar_scrCols() : 0,
           getsysvar_scrRows() ? getsysvar_scrheight() / getsysvar_scrRows() : 0);
}

static void pause_here(int row, const char* what) {
    vdp_cursor_tab(0, (char) row);
    printf("-- %s, press a key --", what);
    getch();
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/config/aed/unscii8x9.bin";

    vdp_clear_screen();
    geometry(0, "before");
    vdp_cursor_tab(0, 1);
    printf("stock font");
    sample(3);
    render_check(8);
    pause_here(11, "this is the stock font");

    vdp_clear_screen();
    vdp_cursor_tab(0, 0);
    printf("font: %s", path);

    int line = 2;
    if (!have_font_api()) {
        vdp_cursor_tab(0, (char) line);
        printf("NO FONT API on this VDP -- needs Console8 2.8.0 or newer.");
        vdp_cursor_tab(0, (char) (line + 1));
        printf("Nothing was uploaded. This is what AED now checks for.");
        pause_here(line + 3, "done");
        vdp_clear_screen();

        return 0;
    }
    vdp_cursor_tab(0, (char) line++);
    printf("font API: present");

    uint8_t fh = mos_fopen(path, FA_READ);
    if (fh == 0) {
        vdp_cursor_tab(0, (char) line);
        printf("cannot open %s", path);
        pause_here(line + 2, "done");
        vdp_clear_screen();

        return 1;
    }
    FIL* fil = mos_getfil(fh);
    const int size = fil != NULL ? (int) fil->obj.objsize : 0;
    if (size <= 0 || (size % GLYPHS) != 0) {
        mos_fclose(fh);
        vdp_cursor_tab(0, (char) line);
        printf("%d bytes is not 256 glyphs of whole rows", size);
        pause_here(line + 2, "done");
        vdp_clear_screen();

        return 1;
    }
    const int height = size / GLYPHS;
    vdp_cursor_tab(0, (char) line++);
    printf("file: %d bytes -> %d rows per glyph", size, height);

    /* Upload. The VDP counts the declared number of bytes off the stream, so
     * every one of them has to be sent even if the file comes up short. */
    static char clear[6] = {23, 0, (char) 0xA0,
                            (char) (BUF_ID & 0xFF), (char) (BUF_ID >> 8), 2};
    put(clear, sizeof(clear));

    char hdr[8] = {23, 0, (char) 0xA0,
                   (char) (BUF_ID & 0xFF), (char) (BUF_ID >> 8), 0,
                   (char) (size & 0xFF), (char) ((size >> 8) & 0xFF)};
    put(hdr, sizeof(hdr));

    int at = 0;
    int ascent = 0;
    while (at < size) {
        int want = size - at;
        if (want > CHUNK) {
            want = CHUNK;
        }
        const int got = (int) mos_fread(fh, chunk, (unsigned) want);
        if (got <= 0) {
            memset(chunk, 0, sizeof(chunk));
            for (int left = size - at; left > 0; ) {
                const int n = left > CHUNK ? CHUNK : left;
                put(chunk, n);
                left -= n;
            }
            break;
        }
        put(chunk, got);
        for (int i = 0; i < got; i++) {
            const int glyph = (at + i) / height;
            const int r = ((at + i) % height) + 1;
            if (chunk[i] != 0 && glyph >= 'A' && glyph <= 'Z' && r > ascent) {
                ascent = r;
            }
        }
        at += got;
    }
    mos_fclose(fh);
    if (ascent <= 0) {
        ascent = height - 1;
    }

    char create[10] = {23, 0, (char) 0x95, 1,
                       (char) (BUF_ID & 0xFF), (char) (BUF_ID >> 8),
                       8, (char) height, (char) ascent, 0};
    put(create, sizeof(create));

    static char select[7] = {23, 0, (char) 0x95, 0,
                             (char) (BUF_ID & 0xFF), (char) (BUF_ID >> 8), 0};
    volatile uint8_t* sv = mos_sysvars();
    sv[sysvar_vdp_pflags] = 0;
    put(select, sizeof(select));
    const bool answered = wait_mode(MODE_FRAMES);

    /* The screen is still full of text drawn at the old cell size, and those
     * pixels do not line up with the new grid. AED clears for this reason. */
    vdp_clear_screen();

    vdp_cursor_tab(0, 0);
    printf("%s: %d bytes, %d rows, ascent %d, mode packet %s",
           path, size, height, ascent, answered ? "yes" : "NO");
    geometry(1, "after");
    sample(3);
    render_check(8);
    pause_here(11, "this is the loaded font");

    /* Hand the machine back with the system font, as AED does on exit. */
    static char restore[7] = {23, 0, (char) 0x95, 0,
                              (char) 0xFF, (char) 0xFF, 0};
    put(restore, sizeof(restore));
    wait_mode(MODE_FRAMES);
    vdp_clear_screen();

    return 0;
}
