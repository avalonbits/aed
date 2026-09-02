/*
 * Host-test implementations of the agon VDP/MOS APIs.
 *
 * The VDU side is inert -- the tests here assert on buffer and cursor state, not
 * on what reached the screen. The file side records what was written, so a save
 * can be checked without touching the filesystem.
 */

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

void mos_puts(const char* b, unsigned size, char d) {
    (void)b; (void)size; (void)d;
}

uint8_t* mos_sysvars(void) {
    static uint8_t sysvars[64];
    sysvars[sysvar_vdp_pflags] = 0x04;  /* pretend the VDP already replied */

    return sysvars;
}

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

void stub_file_reset(void) {
    stub_len = 0;
    stub_opens = 0;
    stub_closes = 0;
    stub_fail_open = 0;
}

const char* stub_file_bytes(void)  { return stub_buf; }
int         stub_file_size(void)   { return stub_len; }
int         stub_file_opens(void)  { return stub_opens; }
int         stub_file_closes(void) { return stub_closes; }
void        stub_file_fail_open(int fail) { stub_fail_open = fail; }

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
    (void)fh; (void)buffer;

    return numbytes;
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
    fil.obj.objsize = 0;

    return &fil;
}
