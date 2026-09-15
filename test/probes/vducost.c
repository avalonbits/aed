#include <stdio.h>
#include <string.h>
#include <time.h>
#include <agon/mos.h>
#include <agon/vdp.h>

/* What does a colour change cost while painting?
 *
 * Syntax highlighting turns one painted row into several: screen.c batches
 * bytes into out_buf and flushes on a colour change, so a plain row is one
 * mos_puts and a row of k tokens is k+1 of them plus k four-byte VDU writes.
 *
 * Two costs, and only one of them can be measured here.
 *
 * THE LINK is arithmetic. 1,152,000 bits a second, ten bits to the byte, is
 * 115,200 bytes a second -- 8.68 us each. A colour change is VDU 17,fg,17,
 * bg+128: four bytes, 34.7 us.
 *
 * THE CALLS are what this measures. Every mos_puts is an rst.lil $10 and MOS's
 * dispatch on the far side, and that is guest work at the real clock.
 *
 * The two turn out to agree, which is the useful part. Fitting the cases below
 * gives 11.1 us a byte against the wire's 8.68, so the wire is 78% of the cost
 * and MOS and the VDP are the rest. A colour change measures 53 us against the
 * 34.7 its bytes must spend. There is no hidden per-call cliff to design
 * around: the cost is the bytes, and four is the fewest a colour change can
 * be.
 *
 * THE CPU is what this measures. Every mos_puts is an rst.lil $10 and MOS's
 * own dispatch on the other side, and that is guest work the emulator runs at
 * the real clock. If a call costs much more than the 34.7 us its four bytes
 * spend on the wire, then highlighting is limited by the calls rather than by
 * the bytes -- and the fix is to coalesce runs rather than to send fewer
 * colours.
 *
 * So: the same 80 bytes of text sent as 1, 3, 5, 9 and 17 separate writes,
 * with a four-byte write between each pair, which is exactly the shape of a
 * row carrying 0, 2, 4, 8 and 16 colour changes.
 *
 * Output goes to the screen and to /vducost.out, so a headless run can be read
 * back off the card.
 *
 * Measured 2026-09-14, 400 rows a case, both firmwares:
 *
 *   changes   bytes     platform cs   console8 cs
 *   0         32,000    36            46
 *   2         35,200    40            52
 *   4         38,400    44            60
 *   8         44,800    52            70
 *   16        57,600    70            94
 *
 * Fitting time = a*calls + b*bytes gives 11.1 us a byte and 8.6 a call on
 * platform, 14.1 and 18.4 on console8. The wire floor is 8.68 us a byte, so
 * platform is within 28% of the physics and console8 within 62%.
 *
 * A colour change costs about five characters of text. A row of ten tokens
 * goes from 0.90 to 1.43 ms (platform) or 1.15 to 1.90 (console8), which is
 * the per-keystroke case and is comfortable. A full 24-row repaint goes from
 * 22 to 34 ms, and from 28 to 46 on console8, which is the one to watch.
 */

#define COLS      80        /* the text a row carries */
#define REPEATS  400        /* rows per case; clock() counts hundredths */

static char row[COLS];
static char vdu[4];
static char out[128];
static char log_fh;

static int put_num(char* o, long v) {
    char t[16];
    int n = 0;
    if (v < 0) { *o++ = '-'; v = -v; }
    if (v == 0) { t[n++] = '0'; }
    while (v > 0) { t[n++] = (char) ('0' + (int) (v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) { o[i] = t[n - 1 - i]; }

    return n + (v < 0 ? 1 : 0);
}

static void say(const char* tag, long a, long b, long c) {
    int k = 0;
    const int n = (int) strlen(tag);
    memcpy(out, tag, (size_t) n);
    k = n;
    while (k < 10) { out[k++] = ' '; }
    k += put_num(out + k, a);
    while (k < 20) { out[k++] = ' '; }
    k += put_num(out + k, b);
    while (k < 32) { out[k++] = ' '; }
    k += put_num(out + k, c);
    out[k++] = '\r'; out[k++] = '\n'; out[k] = 0;
    printf("%s", out);
    if (log_fh) { mos_fwrite(log_fh, out, (unsigned) k); }
}

/* One row: `text` bytes of text broken into `writes` pieces, with a four-byte
 * colour change between each pair. writes == 1 is a plain row. */
static void paint_row(int writes) {
    const int per = COLS / writes;
    int at = 0;
    for (int w = 0; w < writes; w++) {
        const int n = (w == writes - 1) ? (COLS - at) : per;
        mos_puts(row + at, (unsigned) n, 0);
        at += n;
        if (w != writes - 1) {
            mos_puts(vdu, 4, 0);        /* the colour change */
        }
    }
}

int main(void) {
    memset(row, '.', sizeof(row));
    vdu[0] = 17; vdu[1] = 15; vdu[2] = 17; vdu[3] = (char) (0 + 128);

    log_fh = mos_fopen("/vducost.out", FA_WRITE | FA_CREATE_ALWAYS);
    say("changes", 0, 0, 0);            /* header: changes, cs, bytes */

    static const int CASES[] = { 1, 3, 5, 9, 17 };
    for (int c = 0; c < 5; c++) {
        const int writes = CASES[c];
        const int changes = writes - 1;

        /* Warm: the first row through a cold path is not the one to time. */
        paint_row(writes);

        const clock_t t0 = clock();
        for (int r = 0; r < REPEATS; r++) {
            paint_row(writes);
        }
        const long cs = (long) (clock() - t0);

        const long bytes = (long) REPEATS * (COLS + 4L * changes);
        say("k=", changes, cs, bytes);
    }

    if (log_fh) { mos_fclose(log_fh); }

    return 0;
}
