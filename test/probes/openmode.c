/*
 * What MOS does with FA_OPEN_EXISTING, and what ffs_stat says about a file
 * that is there and one that is not.
 *
 * tb_load stopped passing FA_OPEN_ALWAYS so that naming a file which is not
 * there leaves the card alone, and it tells absence from failure with
 * ffs_stat. Both are MOS behaviour the host stubs only model, so both are
 * measured here. Results go to /probe.out.
 */
#include <stdio.h>
#include <string.h>
#include <agon/mos.h>

static char out[512];
static int n;

int main(void) {
    static FILINFO info;

    const char h = mos_fopen("big.c", FA_READ | FA_WRITE);
    n += sprintf(out + n, "open existing rw: handle=%d\n", (int) h);
    if (h != 0) {
        FIL* f = mos_getfil(h);
        n += sprintf(out + n, "  objsize=%ld\n",
                     f != NULL ? (long) f->obj.objsize : -1L);
        mos_fclose(h);
    }

    const char h2 = mos_fopen("nosuch.c", FA_READ | FA_WRITE);
    n += sprintf(out + n, "open missing rw: handle=%d\n", (int) h2);
    if (h2 != 0) {
        mos_fclose(h2);
    }

    memset(&info, 0, sizeof(info));
    n += sprintf(out + n, "stat big.c:  r=%d size=%ld\n",
                 (int) ffs_stat(&info, "big.c"), (long) info.fsize);
    memset(&info, 0, sizeof(info));
    n += sprintf(out + n, "stat nosuch: r=%d\n",
                 (int) ffs_stat(&info, "nosuch.c"));

    const char w = mos_fopen("/probe.out", FA_WRITE | FA_CREATE_ALWAYS);
    if (w != 0) {
        mos_fwrite(w, out, (unsigned) n);
        mos_fclose(w);
    }

    return 0;
}
