#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <agon/mos.h>
#include <agon/vdp.h>

/* How fast is the card, and where does the time actually go?
 *
 * Written for the paging design (.internal/docs/PAGING.md), which needs three
 * numbers and only one of them is throughput:
 *
 *   1. bulk read and write rates, separately -- a paged save is a read pass and
 *      a write pass, so they do not average.
 *   2. the cost of a small operation against a large one. Total bytes moved per
 *      unit of cursor travel does not depend on the chunk size, so smaller
 *      chunks are smoother -- until per-operation overhead dominates. That
 *      crossover is the floor on CHUNK.
 *   3. seeking backwards against forwards. FatFS walks the cluster chain from
 *      the start of the file unless a fast-seek table is set up, so scrolling
 *      up may cost more than scrolling down. This is CPU work, so it is the one
 *      figure the emulator reports faithfully.
 *
 * ON THE EMULATOR the first two measure MOS and FatFS, not a card: the host
 * filesystem serves the reads and no SPI timing is emulated. Treat them as a
 * lower bound. The third is a code path and transfers.
 *
 * sysvar_time is centiseconds, so every measurement is a loop long enough to
 * be worth more than a tick or two. A result of 0 cs means "too fast to
 * measure here", which is itself worth knowing.
 */

#define CHUNK   4096
#define SMALL   1024
#define TOTAL   (512L * 1024L)      /* bytes moved per bulk pass */
#define SEEKS   64                  /* seek+read operations per direction */
#define SEEKRD  512

static char buf[CHUNK];
static const char* PATH = "sdrate.tmp";

static uint32_t cs(void) {
    return getsysvar_time();
}

/* KiB/s from a byte count and a centisecond count, in integer maths. */
static void report(const char* what, uint32_t bytes, uint32_t ticks) {
    if (ticks == 0) {
        printf("%-22s %6u cs   (too fast to measure)\r\n", what, 0u);

        return;
    }
    const uint32_t kibps = (bytes / 1024u) * 100u / ticks;
    printf("%-22s %6u cs  %6u KiB/s\r\n",
           what, (unsigned) ticks, (unsigned) kibps);
}

static uint32_t write_pass(uint24_t chunk) {
    uint8_t fh = mos_fopen(PATH, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return 0;
    }
    const uint32_t t0 = cs();
    for (uint32_t done = 0; done < TOTAL; done += chunk) {
        mos_fwrite(fh, buf, chunk);
    }
    const uint32_t t1 = cs();
    mos_fclose(fh);

    return t1 - t0;
}

static uint32_t read_pass(uint24_t chunk) {
    uint8_t fh = mos_fopen(PATH, FA_READ);
    if (fh == 0) {
        return 0;
    }
    const uint32_t t0 = cs();
    for (uint32_t done = 0; done < TOTAL; done += chunk) {
        mos_fread(fh, buf, chunk);
    }
    const uint32_t t1 = cs();
    mos_fclose(fh);

    return t1 - t0;
}

/* Seek to a spread of offsets and read a little at each. Ascending is what
 * FatFS is built for; descending is what scrolling up will do. */
static uint32_t seek_pass(bool ascending) {
    uint8_t fh = mos_fopen(PATH, FA_READ);
    if (fh == 0) {
        return 0;
    }
    const uint32_t step = TOTAL / SEEKS;
    const uint32_t t0 = cs();
    for (int i = 0; i < SEEKS; i++) {
        const uint32_t at = ascending
            ? (uint32_t) i * step
            : (uint32_t) (SEEKS - 1 - i) * step;
        mos_flseek(fh, at);
        mos_fread(fh, buf, SEEKRD);
    }
    const uint32_t t1 = cs();
    mos_fclose(fh);

    return t1 - t0;
}

int main(void) {
    memset(buf, 'x', sizeof(buf));

    vdp_clear_screen();
    printf("SD throughput -- %u KiB per bulk pass\r\n\r\n",
           (unsigned) (TOTAL / 1024));

    report("write, 4K chunks",  TOTAL, write_pass(CHUNK));
    report("read,  4K chunks",  TOTAL, read_pass(CHUNK));
    report("write, 1K chunks",  TOTAL, write_pass(SMALL));
    report("read,  1K chunks",  TOTAL, read_pass(SMALL));

    printf("\r\n%d seek+read of %d bytes:\r\n", SEEKS, SEEKRD);
    const uint32_t up = seek_pass(true);
    const uint32_t down = seek_pass(false);
    printf("  ascending offsets   %6u cs\r\n", (unsigned) up);
    printf("  descending offsets  %6u cs\r\n", (unsigned) down);
    if (up > 0) {
        printf("  descending costs    %6u%% of ascending\r\n",
               (unsigned) (down * 100u / up));
    }

    mos_del(PATH);
    printf("\r\ndone.\r\n");

    return 0;
}
