#include <stdio.h>
#include <string.h>
#include <agon/mos.h>

#include "ftrunc.h"

/* Does MOS 2.3's ffs_ftruncate work?
 *
 * It matters because the supported MOS floor was raised from 2.2.3 to 2.3.3 for
 * this one function: incremental save -- writing only from the lowest offset an
 * edit touched -- needs a way to shorten a file, and without it any net-deleting
 * edit, which is the commonest kind, falls back to a full rewrite.
 *
 * Calling agondev's ffs_ftruncate hangs the machine. It hangs it on MOS 3.0.2
 * and on console8, which looked like the floor bump having bought nothing at
 * all. It is not MOS: the library's stub pops IX without pushing it, so it
 * returns to whatever was above the return address. src/ftrunc.asm has the
 * disassembly and the corrected binding.
 *
 * This writes a 1,000 byte file, reopens it, seeks to 500, truncates, and
 * reports the result and the size. Numbers are FRESULTs, so 0 is FR_OK; the
 * last line is the size, which wants to be 500.
 *
 * Measured 2026-09-12 on both firmwares, with the corrected binding:
 *
 *     ffs_fopen                         0
 *     ffs_flseek(500)                   0
 *     aed_ftruncate                     0
 *     ffs_fclose                        0
 *     size after truncate (want 500)    500
 *
 * So API 0x85 works, the 2.3.3 floor buys what it was raised to buy, and
 * incremental save needs an asm stub rather than a different design.
 *
 * Writes its output to /ftrunc.out as well as the screen, because nothing on
 * the emulated screen reaches the host when the emulator runs without a
 * display -- which is the only way it runs here. */

static char log_;

static void say(const char* s) {
    printf("%s\r\n", s);
    if (log_) {
        mos_fwrite(log_, (char*) s, (unsigned) strlen(s));
        mos_fwrite(log_, "\r\n", 2);
    }
}

static void sayn(const char* tag, long v) {
    static char b[80];
    int k = (int) strlen(tag);

    memcpy(b, tag, (size_t) k);
    while (k < 34) {
        b[k++] = ' ';
    }
    char t[16];
    int n = 0;
    if (v < 0) {
        b[k++] = '-';
        v = -v;
    }
    if (v == 0) {
        t[n++] = '0';
    }
    while (v > 0) {
        t[n++] = (char) ('0' + (int) (v % 10));
        v /= 10;
    }
    while (n > 0) {
        b[k++] = t[--n];
    }
    b[k] = 0;
    say(b);
}

static char buf[1024];

int main(void) {
    log_ = mos_fopen("/ftrunc.out", FA_WRITE | FA_CREATE_ALWAYS);

    memset(buf, 'a', 1000);
    const char w = mos_fopen("/ftrunc.dat", FA_WRITE | FA_CREATE_ALWAYS);
    mos_fwrite(w, buf, 1000);
    mos_fclose(w);

    static FIL f;
    sayn("ffs_fopen", (long) ffs_fopen(&f, "/ftrunc.dat", FA_READ | FA_WRITE));
    sayn("ffs_flseek(500)", (long) ffs_flseek(&f, 500));
    sayn("aed_ftruncate", (long) aed_ftruncate(&f));
    sayn("ffs_fclose", (long) ffs_fclose(&f));

    const char r = mos_fopen("/ftrunc.dat", FA_READ);
    FIL* g = mos_getfil(r);
    sayn("size after truncate (want 500)", g != NULL ? (long) g->obj.objsize : -1);
    mos_fclose(r);

    mos_del("/ftrunc.dat");
    if (log_) {
        mos_fclose(log_);
    }

    return 0;
}
