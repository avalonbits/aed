/*
 * arrows -- move a marker with the arrow keys, whatever modifiers are held.
 *
 * The question this answers: does the Agon deliver arrow keys to a program
 * while CTRL and SHIFT are held down? Nothing here is borrowed from any
 * editor. It reads MOS key events, moves a character around the screen, and
 * counts what arrives.
 *
 *   keys   -- arrow-key events this program acted on
 *   vkc    -- MOS's own sysvar_vkeycount, which it bumps for every key packet
 *             it receives from the VDP
 *
 * Hold CTRL+SHIFT and tap an arrow. If the marker moves, the platform is
 * delivering the chord. If vkc climbs while keys does not, MOS is receiving
 * packets it is not passing on. If neither moves, the key never arrived.
 *
 * ESC on its own quits.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <agon/keyboard.h>
#include <agon/mos.h>
#include <agon/vdp.h>

#define VK_ESCAPE  125
#define VK_UP      150
#define VK_DOWN    152
#define VK_LEFT    154
#define VK_RIGHT   156

static void put_at(int x, int y, char ch) {
    vdp_cursor_tab(x, y);
    putchar(ch);
}

static void d3(char* at, int v) {
    at[0] = (char) ('0' + (v / 100) % 10);
    at[1] = (char) ('0' + (v / 10) % 10);
    at[2] = (char) ('0' + v % 10);
}

int main(void) {
    struct keyboard_event_t e;
    static char status[] = "keys000 vkc000 vkey000 mods000";
    int x = 20, y = 12;
    int keys = 0;

    vdp_cls();
    vdp_cursor_tab(0, 0);
    printf("arrows: hold CTRL+SHIFT and tap an arrow. ESC alone quits.");

    kbuf_init(32);
    put_at(x, y, '#');

    for (;;) {
        if (!kbuf_poll_event(&e)) {
            continue;
        }
        if (!e.isdown) {
            continue;
        }
        if (e.vkey == VK_ESCAPE && e.kmod == 0) {
            break;
        }

        int nx = x;
        int ny = y;
        switch (e.vkey) {
            case VK_LEFT:  nx = x > 0  ? x - 1 : x; break;
            case VK_RIGHT: nx = x < 60 ? x + 1 : x; break;
            case VK_UP:    ny = y > 2  ? y - 1 : y; break;
            case VK_DOWN:  ny = y < 20 ? y + 1 : y; break;
            default: continue;      /* not an arrow: ignore it entirely */
        }

        keys++;
        put_at(x, y, ' ');
        x = nx;
        y = ny;
        put_at(x, y, '#');

        volatile uint8_t* sv = (volatile uint8_t*) mos_sysvars();
        d3(&status[4],  keys);
        d3(&status[11], sv[sysvar_vkeycount]);
        d3(&status[19], e.vkey);
        d3(&status[27], e.kmod);
        vdp_cursor_tab(0, 1);
        mos_puts(status, sizeof(status) - 1, 0);
    }

    kbuf_deinit();
    vdp_cursor_tab(0, 22);

    return 0;
}
