#include <agon/vdp.h>
#include <agon/mos.h>

/* Dumps the MOS virtual keyboard map live: 16 bytes, one bit per key, 1 while
 * the key is held. Unbuffered and never blocking. Press ESC alone to quit. */
static char hexd(int v) { return (char)(v < 10 ? '0' + v : 'A' + v - 10); }

int main(void) {
    static char head[] = "keyboard map -- hold keys. ESC alone quits.";
    static char line[] = "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00";
    static char mods[] = "keymods=000  ascii=000";

    vdp_cursor_tab(0, 0);
    mos_puts(head, sizeof(head) - 1, 0);

    for (;;) {
        volatile uint8_t* map = (volatile uint8_t*) mos_getkbmap();
        volatile uint8_t* sv  = (volatile uint8_t*) mos_sysvars();
        int any = 0;
        for (int i = 0; i < 16; i++) {
            const uint8_t b = map[i];
            line[i * 3]     = hexd((b >> 4) & 0x0F);
            line[i * 3 + 1] = hexd(b & 0x0F);
            if (b != 0) {
                any = 1;
            }
        }
        int v = sv[sysvar_keymods];
        mods[8]  = '0' + (v / 100) % 10;
        mods[9]  = '0' + (v / 10) % 10;
        mods[10] = '0' + v % 10;
        v = sv[sysvar_keyascii];
        mods[19] = '0' + (v / 100) % 10;
        mods[20] = '0' + (v / 10) % 10;
        mods[21] = '0' + v % 10;

        vdp_cursor_tab(0, 2);
        mos_puts(line, sizeof(line) - 1, 0);
        vdp_cursor_tab(0, 4);
        mos_puts(mods, sizeof(mods) - 1, 0);

        /* ESC on its own: nothing else held. */
        if (!any && sv[sysvar_keyascii] == 27) {
            break;
        }
    }
    vdp_cursor_tab(0, 6);
    return 0;
}
