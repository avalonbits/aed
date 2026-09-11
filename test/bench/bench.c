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

/* AED's CPU-bound paths, timed on the machine they run on.
 *
 * The host is not a proxy for this target -- it is biased rather than noisy,
 * and in the direction that flatters the work -- so a change made for speed
 * has to be measured here. This links AED's real modules and drives the paths
 * that are bounded by the eZ80 rather than by the VDP link or the card:
 * searching, walking lines, copying a range, and typing with undo on.
 *
 *     aedbench [corpus] [repeats]
 *
 * The corpus defaults to /bench.txt, which mkcorpus.sh writes. Results go to
 * the screen and to /bench.out, so a headless run can be read off the card.
 *
 * METHOD, from the optimisation guide this was written against:
 *
 *  - Run the emulator at the real 18.432 MHz. With `-u` the guest's clock()
 *    measures how fast the host emulated the work and means nothing.
 *  - clock() counts hundredths, so each case repeats until it has tens of
 *    them. The tick boundary falls differently on each repeat, so the
 *    quantisation averages out rather than accumulating.
 *  - One change, one measurement, nothing else running. Two emulators on one
 *    host time each other's work.
 *  - The emulator is deterministic to about 0.25%. Do not claim a change
 *    under half a percent from a single run, and do not dismiss a consistent
 *    0.4% as noise.
 *
 * What this does NOT measure: repainting. Every byte of that goes down a UART
 * the emulator does not rate-limit, so a screen test here would say the link
 * is free, and on real hardware it is most of the time a keystroke takes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <agon/mos.h>

#include "char_buffer.h"
#include "conv.h"
#include "text_buffer.h"
#include "undo.h"

#define MEM_KB      256
#define OUT_PATH    "/bench.out"

static char out_line[128];

static void say(const char* s) {
    printf("%s\r\n", s);
}

/* One case's result, in the form the method wants: total centiseconds over a
 * known number of repeats, so the reader can see the quantisation. */
static void report(char fh, const char* name, clock_t ticks, int reps) {
    int k = 0;
    const int n = (int) strlen(name);
    memcpy(out_line, name, (size_t) n);
    k = n;
    while (k < 18) {
        out_line[k++] = ' ';
    }
    k += (int) strlen(i2s((int) ticks, out_line + k, 12));
    while (k < 26) {
        out_line[k++] = ' ';
    }
    out_line[k++] = 'x';
    k += (int) strlen(i2s(reps, out_line + k, 12));
    out_line[k++] = '\r';
    out_line[k++] = '\n';
    out_line[k] = 0;

    printf("%s", out_line);
    if (fh != 0) {
        mos_fwrite(fh, out_line, (unsigned) k);
    }
}

int main(int argc, char** argv) {
    const char* corpus = argc > 1 ? argv[1] : "/bench.txt";
    const int reps = argc > 2 ? atoi(argv[2]) : 0;

    static text_buffer tb;
    static undo u;
    static char_buffer cb;

    say("aedbench");

    const char fh = mos_fopen(OUT_PATH, FA_WRITE | FA_CREATE_ALWAYS);

    /* --- load: the card and the line index --- */
    int load_reps = reps > 0 ? reps : 8;
    clock_t t0 = clock();
    for (int i = 0; i < load_reps; i++) {
        if (tb_init(&tb, MEM_KB, corpus) == NULL) {
            say("cannot open corpus");
            if (fh != 0) {
                mos_fclose(fh);
            }

            return 1;
        }
        if (i + 1 < load_reps) {
            tb_destroy(&tb);
        }
    }
    report(fh, "load", clock() - t0, load_reps);

    const int used = tb_used(&tb);
    const int lines = tb_ymax(&tb);

    /* --- find: a full scan of the document, missing --- */
    static const char miss[] = "zqxjkvwyzqxjkvwy";
    int find_reps = reps > 0 ? reps : 4;
    t0 = clock();
    for (int i = 0; i < find_reps; i++) {
        tb_pos from = { 1, 0 };
        tb_pos at;
        tb_find(&tb, miss, (int) sizeof(miss) - 1, from, true, &at);
    }
    report(fh, "find-miss", clock() - t0, find_reps);

    /* --- seek: walking the document by line, both ways --- */
    int seek_reps = reps > 0 ? reps : 4;
    t0 = clock();
    for (int i = 0; i < seek_reps; i++) {
        for (int y = 1; y <= lines; y++) {
            tb_pos p = { y, 0 };
            tb_seek(&tb, p);
        }
        for (int y = lines; y >= 1; y--) {
            tb_pos p = { y, 0 };
            tb_seek(&tb, p);
        }
    }
    report(fh, "seek-lines", clock() - t0, seek_reps);

    /* --- copy: the whole document into a buffer --- */
    int copy_reps = reps > 0 ? reps : 4;
    if (cb_init(&cb, MEM_KB) != NULL) {
        t0 = clock();
        for (int i = 0; i < copy_reps; i++) {
            tb_pos a = { 1, 0 };
            tb_pos b = { lines, 0 };
            cb_clear(&cb);
            tb_range_copy(&tb, a, b, &cb);
        }
        report(fh, "range-copy", clock() - t0, copy_reps);
        cb_destroy(&cb);
    }

    /* --- typing, with undo recording, which is what a keystroke costs --- */
    int type_reps = reps > 0 ? reps * 200 : 2000;
    if (undo_init(&u, UNDO_TEXT_BYTES, UNDO_MAX_RECS) != NULL) {
        tb_set_undo(&tb, &u);
        tb_pos mid = { lines / 2, 0 };
        tb_seek(&tb, mid);
        t0 = clock();
        for (int i = 0; i < type_reps; i++) {
            tb_put(&tb, (char) ('a' + (i & 15)));
        }
        report(fh, "type", clock() - t0, type_reps);

        t0 = clock();
        for (int i = 0; i < type_reps; i++) {
            undo_apply(&u, &tb);
        }
        report(fh, "undo", clock() - t0, type_reps);
        tb_set_undo(&tb, NULL);
        undo_destroy(&u);
    }

    int k = 0;
    out_line[k++] = '(';
    k += (int) strlen(i2s(used, out_line + k, 12));
    memcpy(out_line + k, " bytes, ", 8);
    k += 8;
    k += (int) strlen(i2s(lines, out_line + k, 12));
    memcpy(out_line + k, " lines)\r\n", 9);
    k += 9;
    out_line[k] = 0;
    printf("%s", out_line);
    if (fh != 0) {
        mos_fwrite(fh, out_line, (unsigned) k);
        mos_fclose(fh);
    }

    tb_destroy(&tb);

    return 0;
}
