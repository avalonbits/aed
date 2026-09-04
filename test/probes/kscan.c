#include <agon/vdp.h>
#include <agon/mos.h>

/* Asks for each navigation key in turn and records where it sits in the MOS
 * virtual keyboard map. Prints the table at the end. */

#define NKEYS 8
static const char* names[NKEYS] = {
    "LEFT ", "RIGHT", "UP   ", "DOWN ", "HOME ", "END  ", "PGUP ", "PGDN "
};
static int gotbyte[NKEYS];
static int gotbit[NKEYS];

static void say(int row, const char* s, int n) {
    vdp_cursor_tab(0, (char) row);
    mos_puts((char*) s, n, 0);
}

static void clear_row(int row) {
    static char blank[] = "                                        ";
    say(row, blank, sizeof(blank) - 1);
}

/* Waits until nothing at all is held. */
static void wait_clear(void) {
    for (;;) {
        volatile uint8_t* m = (volatile uint8_t*) mos_getkbmap();
        int any = 0;
        for (int i = 0; i < 16; i++) {
            if (m[i] != 0) {
                any = 1;
            }
        }
        if (!any) {
            return;
        }
    }
}

int main(void) {
    static char head[] = "Press each key when asked, then let go.";
    static char ask[]  = "press: XXXXX";
    static char out[]  = "XXXXX byte=00 bit=0";

    say(0, head, sizeof(head) - 1);
    wait_clear();

    for (int k = 0; k < NKEYS; k++) {
        for (int c = 0; c < 5; c++) {
            ask[7 + c] = names[k][c];
        }
        say(2, ask, sizeof(ask) - 1);

        int b = -1, bit = -1;
        while (b < 0) {
            volatile uint8_t* m = (volatile uint8_t*) mos_getkbmap();
            for (int i = 0; i < 16 && b < 0; i++) {
                const uint8_t v = m[i];
                if (v == 0) {
                    continue;
                }
                for (int j = 0; j < 8; j++) {
                    if (v & (1 << j)) {
                        b = i;
                        bit = j;
                        break;
                    }
                }
            }
        }
        gotbyte[k] = b;
        gotbit[k] = bit;
        wait_clear();
    }

    clear_row(2);
    for (int k = 0; k < NKEYS; k++) {
        for (int c = 0; c < 5; c++) {
            out[c] = names[k][c];
        }
        out[11] = (char)('0' + (gotbyte[k] / 10) % 10);
        out[12] = (char)('0' + gotbyte[k] % 10);
        out[18] = (char)('0' + gotbit[k]);
        say(4 + k, out, sizeof(out) - 1);
    }
    vdp_cursor_tab(0, 4 + NKEYS + 1);

    return 0;
}
