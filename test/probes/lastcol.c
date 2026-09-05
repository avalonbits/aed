#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <agon/keyboard.h>
#include <agon/vdp.h>
#include <agon/mos.h>

/* Does writing the last column of the screen arm the VDP's CTRL+SHIFT pause?
 *
 * The claim under test is that it does, and that it is why AED reserves that
 * column. The counter on row 0 advances once per key event. Hold CTRL+SHIFT and
 * tap an arrow: if the counter keeps up, the writes are harmless; if it freezes
 * until the keys are released, the write is arming the pause.
 *
 * Build twice, changing COL_FROM_RIGHT: 1 writes the last column of the screen,
 * 2 writes the one before it -- which is what AED does now. Everything else is
 * identical, so the two builds differ in one column.
 *
 * ESC alone quits. */
#define COL_FROM_RIGHT 1

int main(void) {
    struct keyboard_event_t e;
    static char msg[] = "hold CTRL+SHIFT and tap an arrow. ESC quits.";
    static char count[] = "events=00000";

    const int cols = getsysvar_scrCols();
    const char col = (char) (cols - COL_FROM_RIGHT);

    vdp_clear_screen();
    vdp_cursor_tab(0, 0);
    mos_puts(msg, sizeof(msg) - 1, 0);

    kbuf_init(32);

    unsigned n = 0;
    for (;;) {
        if (!kbuf_poll_event(&e)) {
            continue;
        }
        if (!e.isdown) {
            continue;
        }
        if (e.ascii == 27 && e.kmod == 0) {
            break;
        }

        n++;
        count[7]  = (char) ('0' + (n / 10000) % 10);
        count[8]  = (char) ('0' + (n / 1000) % 10);
        count[9]  = (char) ('0' + (n / 100) % 10);
        count[10] = (char) ('0' + (n / 10) % 10);
        count[11] = (char) ('0' + n % 10);

        /* The write under test: one character in the column being probed, on a
         * row of its own, repeated so a pause is unmistakable. */
        for (int row = 4; row < 12; row++) {
            vdp_cursor_tab(col, (char) row);
            putchar('#');
        }

        vdp_cursor_tab(0, 2);
        mos_puts(count, sizeof(count) - 1, 0);
    }

    kbuf_deinit();
    vdp_cursor_tab(0, 14);

    return 0;
}
