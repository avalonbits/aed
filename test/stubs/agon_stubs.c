/*
 * Host-test implementations of the agon VDP/MOS APIs.
 *
 * The VDU side is inert -- the tests here assert on buffer and cursor state, not
 * on what reached the screen. The file side records what was written, so a save
 * can be checked without touching the filesystem.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>
#include <agon/vdp.h>

/* --- VDP: inert --- */
void vdp_cursor_left(void) {}
void vdp_cursor_home(void) {}
void vdp_clear_screen(void) {}
void vdp_cursor_enable(bool flag)    { (void)flag; }
static int stub_fg = -1;
static int stub_colour_bytes = 0;
static int stub_bg = -1;

void vdp_set_text_colour(int colour) {
    if (colour >= 128) {
        stub_bg = colour - 128;
    } else {
        stub_fg = colour;
    }
    /* Off by default. The real call is "VDU 17, colour" and a test that wants
     * to see a highlight needs those bytes in the stream, but colour 0 puts a
     * NUL in it -- which ends the string for every other test that reads the
     * stream back and searches it for text. So it is asked for. */
    if (stub_colour_bytes) {
        putchar(17);
        putchar(colour & 0xFF);
    }
}

void stub_emit_colours(int on) { stub_colour_bytes = on; }

int  stub_last_fg(void) { return stub_fg; }
int  stub_last_bg(void) { return stub_bg; }
void stub_colours_reset(void) { stub_fg = -1; stub_bg = -1; }
void vdp_cursor_tab(int x, int y)    { (void)x; (void)y; }

/* --- MOS: screen/system --- */
void waitvblank(void) {}

// Writes to stdout so that VDU bytes sent via mos_puts and via putchar land in
// one ordered stream, which is what the scroll tests read back.
void mos_puts(const char* b, unsigned size, char d) {
    (void)d;
    if (b != NULL && size > 0) {
        fwrite(b, 1, size, stdout);
    }
}

uint8_t* mos_sysvars(void) {
    static uint8_t sysvars[64];
    sysvars[sysvar_vdp_pflags] = 0x04;  /* pretend the VDP already replied */

    return sysvars;
}

static const stub_key* stub_keys;
static int stub_nkeys;
/* Packets reported since the process started. Never reset: on the device the
 * counter only ever climbs, and a reader remembers the last value it saw. A
 * stub that restarted it at zero could hand back a value the reader had already
 * seen, and the reader would wait for a key that had in fact arrived. */
static int stub_delivered;
static int stub_key_base;       /* what it read when this script was set */
static int stub_polls;

void stub_set_keys(const stub_key* keys, int n) {
    stub_keys = keys;
    stub_nkeys = n;
    stub_key_base = stub_delivered;
}

/* The scripted keys are modelled the way the VDP delivers real ones: a packet
 * counter that moves when a key arrives, and fields describing the packet the
 * counter last reported. Every poll of the counter reports a new packet, so a
 * reader waiting for it to change never spins.
 *
 * A script that has run out keeps reporting ESCAPE presses rather than
 * stopping, so a modal prompt driven by one can never loop forever. */
static stub_key stub_current(void) {
    stub_key esc;
    esc.ch = 27;
    esc.vk = 125;                       /* VK_ESCAPE */
    esc.mods = 0;
    esc.up = 0;

    const int at = stub_delivered - stub_key_base - 1;
    if (stub_keys == NULL || at < 0 || at >= stub_nkeys) {
        return esc;
    }

    return stub_keys[at];
}

uint8_t getsysvar_vkeycount(void) {
    /* A packet arrives on every other poll rather than on every one, so the
     * counter sometimes reads the same twice running -- which is the normal
     * case on the device, where most polls find nothing new. A reader that did
     * not remember the last value it saw would take the same packet twice, and
     * that is worth being able to catch. */
    if (++stub_polls % 2 == 0) {
        stub_delivered++;
    }

    return (uint8_t) stub_delivered;
}

uint8_t getsysvar_vkeydown(void) {
    return stub_current().up ? 0 : 1;
}

uint8_t getsysvar_keyascii(void) {
    return (uint8_t) stub_current().ch;
}

uint8_t getsysvar_vkeycode(void) {
    return stub_current().vk;
}
uint8_t getsysvar_keymods(void) {
    return stub_current().mods;
}

int stub_keys_read(void) { return stub_delivered - stub_key_base; }

static uint8_t stub_cols = 80;
static uint8_t stub_rows = 25;

void stub_set_screen(int cols, int rows) {
    stub_cols = (uint8_t) cols;
    stub_rows = (uint8_t) rows;
}

uint8_t getsysvar_scrCols(void)    { return stub_cols; }
uint8_t getsysvar_scrRows(void)    { return stub_rows; }
uint8_t getsysvar_scrColours(void) { return 16; }

/* --- MOS: file I/O, recorded in memory --- */
#define STUB_FILE_CAP (512 * 1024)

static char stub_buf[STUB_FILE_CAP];
static int  stub_len;
static int  stub_opens;
static int  stub_closes;
static int  stub_fail_open;
static int  stub_write_opens;
static int  stub_short_read = -1;
static int  stub_read_pos;
static uint32_t stub_objsize;
static int  stub_objsize_set;
static int         stub_mkdir_count;
static int         stub_delete_count;
static int         stub_short = -1;
static char        stub_mkdir_path[256];
static const char* stub_content;
static int         stub_content_len;

void stub_file_reset(void) {
    stub_len = 0;
    stub_opens = 0;
    stub_closes = 0;
    stub_fail_open = 0;
    stub_write_opens = 0;
    stub_short_read = -1;
    stub_read_pos = 0;
    stub_objsize = 0;
    stub_objsize_set = 0;
    stub_content = NULL;
    stub_content_len = 0;
    stub_mkdir_count = 0;
    stub_mkdir_path[0] = 0;
    stub_delete_count = 0;
    stub_short = -1;
}

const char* stub_file_bytes(void)  { return stub_buf; }
int         stub_file_size(void)   { return stub_len; }
int         stub_file_opens(void)  { return stub_opens; }
int         stub_file_opens_for_write(void) { return stub_write_opens; }
int         stub_file_closes(void) { return stub_closes; }
void        stub_file_fail_open(int fail) { stub_fail_open = fail; }

void stub_discard_output(void) {
    if (freopen("/dev/null", "w", stdout) == NULL) {
        fprintf(stderr, "warning: could not discard stdout\n");
    }
}

void stub_file_set_content(const char* data, int len) {
    stub_content = data;
    stub_content_len = len;
    stub_read_pos = 0;
}

/* Serves back whatever has been written, so a test can write a file and then
 * read it in again -- which is the whole of a spilled copy and its paste. */
void stub_file_readback(void) {
    stub_content = stub_buf;
    stub_content_len = stub_len;
    stub_read_pos = 0;
}

int         stub_mkdirs(void)      { return stub_mkdir_count; }
int         stub_deletes(void)     { return stub_delete_count; }
void        stub_file_short_write(int n) { stub_short = n; }
void        stub_file_short_read(int n)  { stub_short_read = n; }

void stub_file_set_objsize(uint32_t n) {
    stub_objsize = n;
    stub_objsize_set = 1;
}

uint8_t mos_del(const char* filename) {
    (void)filename;
    stub_delete_count++;

    return 0;
}
const char* stub_last_mkdir(void)  { return stub_mkdir_path; }

uint8_t mos_mkdir(const char* path) {
    stub_mkdir_count++;
    if (path != NULL) {
        size_t n = strlen(path);
        if (n >= sizeof(stub_mkdir_path)) {
            n = sizeof(stub_mkdir_path) - 1;
        }
        memcpy(stub_mkdir_path, path, n);
        stub_mkdir_path[n] = 0;
    }

    return 0;
}

uint8_t mos_fopen(const char* filename, uint8_t mode) {
    (void)filename;
    stub_opens++;
    stub_read_pos = 0;      /* reads start at the beginning of the file */
    if ((mode & FA_WRITE) != 0) {
        stub_write_opens++;
    }
    if (stub_fail_open) {
        return 0;  /* MOS reports failure as handle 0 */
    }
    /* Opening for reading alone fails when there is nothing to read, which is
     * how a file that is not there behaves. The stub cannot tell one name from
     * another, so "content has been set" stands in for "the file exists" --
     * enough for the code that probes whether a path is already taken. Opens
     * that ask to write are unaffected: those create the file. */
    if ((mode & FA_WRITE) == 0 && stub_content_len == 0) {
        return 0;
    }

    return 1;
}

uint8_t mos_fclose(uint8_t fh) {
    (void)fh;
    stub_closes++;

    return 0;
}

unsigned mos_fread(uint8_t fh, char* buffer, unsigned numbytes) {
    (void)fh;
    /* Actually fill the caller's buffer: a short read would hide an
     * out-of-bounds destination from the sanitizer. */
    /* Reads advance through the content the way a real file does, so a caller
     * that streams it in chunks gets each chunk once and in order. */
    unsigned n = numbytes;
    const int left = stub_content_len - stub_read_pos;
    if ((int) n > left) {
        n = (unsigned) (left > 0 ? left : 0);
    }
    if (stub_short_read >= 0 && (int) n > stub_short_read) {
        n = (unsigned) stub_short_read;
    }
    if (stub_content != NULL && n > 0) {
        memcpy(buffer, stub_content + stub_read_pos, n);
        stub_read_pos += (int) n;
    }

    return n;
}

unsigned mos_fwrite(uint8_t fh, char* buffer, unsigned numbytes) {
    (void)fh;
    if (stub_short >= 0 && (int) numbytes > stub_short) {
        numbytes = (unsigned) stub_short;   // pretend the card filled up
    }
    if (stub_len + (int)numbytes > STUB_FILE_CAP) {
        numbytes = STUB_FILE_CAP - stub_len;
    }
    memcpy(stub_buf + stub_len, buffer, numbytes);
    stub_len += numbytes;

    return numbytes;
}

FIL* mos_getfil(uint8_t fh) {
    (void)fh;
    static FIL fil;
    /* Normally the content is the file, but a test can say otherwise to
     * exercise a size the read does not agree with. */
    fil.obj.objsize = stub_objsize_set
        ? stub_objsize : (uint32_t) stub_content_len;

    return &fil;
}
