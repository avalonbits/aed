/*
 * What MOS does with mos_ren over an existing file, and what attributes a
 * directory carries.
 *
 * The save path writes a scratch file and puts it in the document's place,
 * which for a while was a bare rename. FatFS's f_rename refuses a target that
 * already exists and so does MOS -- FR_EXIST, measured here -- so every save
 * over a file that was already there failed, and a reader on real hardware
 * could not save at all. The host stub had modelled a rename as an overwrite,
 * so the whole save suite passed against it.
 *
 * Results go to /probe.out.
 */
#include <stdio.h>
#include <string.h>
#include <agon/mos.h>

static char out[1024];
static int n;

static void say_attr(const char* path) {
    static FILINFO info;
    memset(&info, 0, sizeof(info));
    const uint8_t r = ffs_stat(&info, path);
    n += sprintf(out + n, "  %-24s r=%d attrib=%02X%s%s%s%s%s\n", path, (int) r,
                 (unsigned) info.fattrib,
                 (info.fattrib & AM_RDO) ? " RDO" : "",
                 (info.fattrib & AM_HID) ? " HID" : "",
                 (info.fattrib & AM_SYS) ? " SYS" : "",
                 (info.fattrib & AM_DIR) ? " DIR" : "",
                 (info.fattrib & AM_ARC) ? " ARC" : "");
}

static void put(const char* path, const char* text) {
    const char fh = mos_fopen(path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh != 0) {
        mos_fwrite(fh, (char*) text, (unsigned) strlen(text));
        mos_fclose(fh);
    }
}

static long size_of(const char* path) {
    static FILINFO info;
    memset(&info, 0, sizeof(info));

    return ffs_stat(&info, path) == 0 ? (long) info.fsize : -1L;
}

int main(void) {
    /* 1. Does a rename replace an existing target? */
    put("/ren_a.txt", "aaaaaaaaaa");          /* 10 bytes, the replacement */
    put("/ren_b.txt", "bbbbbbbbbbbbbbbbbbbb");/* 20 bytes, the one replaced */
    const uint8_t rr = mos_ren("/ren_a.txt", "/ren_b.txt");
    n += sprintf(out + n, "ren over existing: r=%d\n", (int) rr);
    n += sprintf(out + n, "  b is now %ld bytes (10 = replaced, 20 = not)\n",
                 size_of("/ren_b.txt"));
    n += sprintf(out + n, "  a still there? %ld\n", size_of("/ren_a.txt"));

    /* 2. And over a target that is not there, which must work. */
    mos_del("/ren_c.txt");
    const uint8_t rc = mos_ren("/ren_b.txt", "/ren_c.txt");
    n += sprintf(out + n, "ren over absent:   r=%d c=%ld\n", (int) rc,
                 size_of("/ren_c.txt"));

    /* 3. What attributes things carry. */
    n += sprintf(out + n, "attributes:\n");
    say_attr("/config");
    say_attr("/config/aed");
    say_attr("/config/aed/syntax");
    say_attr("/config/aed/themes");
    say_attr("/config/aed.ini");
    say_attr("/config/aed/syntax/c.cfg");

    /* 4. And what mos_mkdir gives a fresh one. */
    const uint8_t md = mos_mkdir("/probedir");
    n += sprintf(out + n, "mkdir /probedir: r=%d\n", (int) md);
    say_attr("/probedir");

    const char w = mos_fopen("/probe.out", FA_WRITE | FA_CREATE_ALWAYS);
    if (w != 0) {
        mos_fwrite(w, out, (unsigned) n);
        mos_fclose(w);
    }

    return 0;
}
