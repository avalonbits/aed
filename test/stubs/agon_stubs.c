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
void vdp_set_text_colour(int colour) { (void)colour; }
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

char    getch(void)              { return 0; }
uint8_t getsysvar_vkeycode(void) { return 0; }
uint8_t getsysvar_keymods(void)  { return 0; }

uint8_t getsysvar_scrCols(void)    { return 80; }
uint8_t getsysvar_scrRows(void)    { return 25; }
uint8_t getsysvar_scrColours(void) { return 16; }

/* --- MOS: file I/O, recorded in memory --- */
#define STUB_FILE_CAP (512 * 1024)

static char stub_buf[STUB_FILE_CAP];
static int  stub_len;
static int  stub_opens;
static int  stub_closes;
static int  stub_fail_open;
static int         stub_mkdir_count;
static char        stub_mkdir_path[256];
static const char* stub_content;
static int         stub_content_len;

void stub_file_reset(void) {
    stub_len = 0;
    stub_opens = 0;
    stub_closes = 0;
    stub_fail_open = 0;
    stub_content = NULL;
    stub_content_len = 0;
    stub_mkdir_count = 0;
    stub_mkdir_path[0] = 0;
}

const char* stub_file_bytes(void)  { return stub_buf; }
int         stub_file_size(void)   { return stub_len; }
int         stub_file_opens(void)  { return stub_opens; }
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
    (void)filename; (void)mode;
    stub_opens++;
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
    if (stub_content != NULL && n > 0) {
        memcpy(buffer, stub_content, n);
    }

    return n;
}

unsigned mos_fwrite(uint8_t fh, char* buffer, unsigned numbytes) {
    (void)fh;
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
    fil.obj.objsize = (uint32_t) stub_content_len;

    return &fil;
}
