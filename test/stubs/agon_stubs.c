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
static int stub_key_at;

void stub_set_keys(const stub_key* keys, int n) {
    stub_keys = keys;
    stub_nkeys = n;
    stub_key_at = 0;
}

char getch(void) {
    if (stub_keys == NULL || stub_key_at >= stub_nkeys) {
        return 27;                      /* ESC, so no prompt loops forever */
    }

    return stub_keys[stub_key_at].ch;
}

uint8_t getsysvar_vkeycode(void) {
    if (stub_keys == NULL || stub_key_at >= stub_nkeys) {
        return 125;                     /* VK_ESCAPE */
    }

    return stub_keys[stub_key_at++].vk;
}
uint8_t getsysvar_keymods(void) {
    if (stub_keys == NULL || stub_key_at >= stub_nkeys) {
        return 0;
    }

    return stub_keys[stub_key_at].mods;
}

int stub_keys_read(void) { return stub_key_at; }

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
    if ((mode & FA_WRITE) != 0) {
        stub_write_opens++;
    }
    if (stub_fail_open) {
        return 0;  /* MOS reports failure as handle 0 */
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
    unsigned n = numbytes;
    if ((int) n > stub_content_len) {
        n = (unsigned) stub_content_len;
    }
    if (stub_short_read >= 0 && (int) n > stub_short_read) {
        n = (unsigned) stub_short_read;
    }
    if (stub_content != NULL && n > 0) {
        memcpy(buffer, stub_content, n);
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
