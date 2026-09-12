/*
 * Host-test implementations of the agon VDP/MOS APIs.
 *
 * The VDU side is inert -- the tests here assert on buffer and cursor state, not
 * on what reached the screen. The file side records what was written, so a save
 * can be checked without touching the filesystem.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/keyboard.h>
#include <agon/mos.h>
#include <agon/vdp.h>

/* --- VDP: inert --- */
void vdp_cursor_left(void) {}
void vdp_cursor_home(void) {}
/* VDU 12. It used to emit nothing, which meant no test could see a clear
 * happen at all -- and the footer now paints its last column with one, since
 * that column may hold a colour but never a character. */
void vdp_clear_screen(void) { putchar(12); }
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
static int stub_write_n;
static int stub_write_max;
static int stub_tab_write_n;

void stub_writes_reset(void) {
    stub_write_n = 0;
    stub_write_max = 0;
    stub_tab_write_n = 0;
}

int stub_writes(void)   { return stub_write_n; }
int stub_max_write(void) { return stub_write_max; }
int stub_tab_at(void)   { return stub_tab_write_n; }

/* Where the last vdp_cursor_tab put the cursor. The tab itself emits nothing
 * into the captured stream, so without this the column the view chose to paint
 * at is invisible to every test -- which is exactly where the text-area margin
 * lives. */
static int stub_tab_x = -1;
static int stub_tab_y = -1;

int stub_last_tab_x(void) { return stub_tab_x; }
int stub_last_tab_y(void) { return stub_tab_y; }

/* Whether the tab reaches the captured stream. Off by default: it puts control
 * bytes into what most tests read back as text. On, the stream carries what the
 * real call carries -- VDU 31, x, y -- which is the only way to see *where* a
 * row was painted, as opposed to what was in it. */
static int stub_tab_bytes;

void stub_emit_tabs(int on) { stub_tab_bytes = on; }

void vdp_cursor_tab(int x, int y) {
    stub_tab_x = x;
    stub_tab_y = y;
    stub_tab_write_n = stub_write_n;
    if (stub_tab_bytes) {
        putchar(31);
        putchar(x & 0xFF);
        putchar(y & 0xFF);
    }
}

/* --- MOS: screen/system --- */
void waitvblank(void) {}

// Writes to stdout so that VDU bytes sent via mos_puts and via putchar land in
// one ordered stream, which is what the scroll tests read back.
/* The stubbed VDP's font handling. A font change moves the cell size, and the
 * row count with it, and the editor lays the whole screen out from those
 * numbers -- so a stub that leaves the geometry alone is not modelling the
 * thing under test. Watching the two sequences here is what makes a host test
 * of the font path mean anything.
 *
 * stub_vdp_font_applies(0) is a VDP that takes the font but whose mode packet
 * never reaches MOS, leaving the sysvars describing the old font. */
static uint8_t stub_rows = 25;
static uint16_t stub_cellh = 8;

static int stub_font_h = 0;
static int stub_font_applies = 1;

void stub_vdp_font_applies(int on) { stub_font_applies = on; }

/* The panel is a fixed number of pixels tall; a font change divides it up
 * differently. Re-deriving it from cellh * rows instead would lose the
 * remainder every time, so 480 becomes 477 becomes 472 over three font
 * changes -- which is a bug in the stub, not something a real VDP does. */
static int stub_screen_px = 200;

static void stub_apply_cell(int h) {
    if (h <= 0) {
        return;
    }
    stub_cellh = (uint16_t) h;
    stub_rows = (uint8_t) (stub_screen_px / h);
}

/* The stubbed VDP's pixel read, which is how AED finds the colours the machine
 * was using before it started. AED writes a character in the top left cell and
 * asks for the colour of a pixel of it with VDU 23,0,&84; whether that pixel is
 * ink depends on where in the cell the font draws the glyph, and getting that
 * wrong is what left a machine booted into a sixteen-row font with a blank
 * screen and no cursor. So the glyph is modelled: rows below stub_ink_from are
 * blank, the rest are ink.
 *
 * stub_set_screen_colours says what the screen was in before AED ran, which is
 * the thing the probe is trying to recover. */
static int stub_scr_fg = 15;
static int stub_scr_bg = 0;
static int stub_ink_from = 0;
static char stub_cell_ch = ' ';
static uint8_t stub_pixel;

void stub_set_screen_colours(int fg, int bg) {
    stub_scr_fg = fg;
    stub_scr_bg = bg;
}

void stub_set_glyph_ink(int first_row) { stub_ink_from = first_row; }

static void stub_pixel_vdu(const char* b, unsigned size) {
    if (size < 7 || b[0] != 23 || b[1] != 0 || (unsigned char) b[2] != 0x84) {
        return;
    }
    const int y = (unsigned char) b[5] | ((unsigned char) b[6] << 8);
    const int ink = stub_cell_ch != ' ' && y >= stub_ink_from;
    stub_pixel = (uint8_t) (ink ? stub_scr_fg : stub_scr_bg);
}

static void stub_font_vdu(const char* b, unsigned size) {
    if (size < 7 || b[0] != 23 || b[1] != 0 || (unsigned char) b[2] != 0x95) {
        return;
    }
    if (b[3] == 1 && size >= 10) {          /* create from buffer */
        stub_font_h = (unsigned char) b[7];

        return;
    }
    if (b[3] != 0) {
        return;
    }
    const int id = (unsigned char) b[4] | ((unsigned char) b[5] << 8);
    if (!stub_font_applies) {
        return;
    }
    stub_apply_cell(id == 0xFFFF ? 8 : stub_font_h);
}

void mos_puts(const char* b, unsigned size, char d) {
    (void)d;
    stub_write_n++;
    if (b != NULL) {
        stub_font_vdu(b, size);
        stub_pixel_vdu(b, size);
        if (size == 1 && (unsigned char) b[0] >= 32) {
            stub_cell_ch = b[0];    /* one character on its own: the probe */
        }
    }
    if ((int) size > stub_write_max) {
        stub_write_max = (int) size;
    }
    if (b == NULL || size == 0) {
        return;
    }

    /* Colours travel as VDU 17 pairs inside ordinary writes now rather than
     * through vdp_set_text_colour, so they are recognised here instead. They
     * are recorded either way; whether they reach the stream stays opt-in,
     * because colour 0 puts a NUL in it and that ends the string for every
     * test that reads the stream back as text. */
    for (unsigned i = 0; i < size; i++) {
        if ((unsigned char) b[i] == 17 && i + 1 < size) {
            const int colour = (unsigned char) b[i + 1];
            if (colour >= 128) {
                stub_bg = colour - 128;
            } else {
                stub_fg = colour;
            }
            if (stub_colour_bytes) {
                putchar(17);
                putchar(colour & 0xFF);
            }
            i++;
            continue;
        }
        putchar(b[i]);
    }
}

/* Which replies the stubbed VDP gives. Both are set by default -- every test
 * that is not about the waiting itself wants the wait to be over -- and the
 * mode one can be withheld, because a VDP with no font API never sends it and
 * the editor must not hang waiting. */
static int stub_mode_reply = 1;

void stub_vdp_mode_reply(int on) { stub_mode_reply = on; }

uint8_t* mos_sysvars(void) {
    static uint8_t sysvars[64];
    sysvars[sysvar_vdp_pflags] = vdp_pflag_point;  /* pretend the VDP replied */
    sysvars[sysvar_scrpixelIndex] = stub_pixel;
    if (stub_mode_reply) {
        sysvars[sysvar_vdp_pflags] |= vdp_pflag_mode;
    }

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

/* --- MOS: the key event queue --- */

static int stub_kb_installed;

void kbuf_init(uint8_t buf_len) {
    (void) buf_len;
    stub_kb_installed = 1;
}

void kbuf_deinit(void) {
    stub_kb_installed = 0;
}

void kbuf_clear(void) {}

int stub_keys_installed(void) { return stub_kb_installed; }

static int stub_stall;

void stub_keys_stall(int n) {
    stub_stall = n;
}

bool kbuf_poll_event(struct keyboard_event_t* e) {
    if (stub_stall > 0) {
        stub_stall--;

        return false;               /* nothing has arrived yet */
    }
    if (stub_keys == NULL || stub_key_at >= stub_nkeys) {
        /* ESC, pressed, forever: no prompt loop can hang on an empty script. */
        e->ascii = 27;
        e->kmod = 0;
        e->vkey = 125;                  /* VK_ESCAPE */
        e->isdown = 1;

        return true;
    }

    const stub_key k = stub_keys[stub_key_at++];
    e->ascii = (uint8_t) k.ch;
    e->kmod = k.mods;
    e->vkey = k.vk;
    e->isdown = k.up ? 0 : 1;

    return true;
}

int stub_keys_read(void) { return stub_key_at; }

static uint8_t stub_cols = 80;


void stub_set_screen(int cols, int rows) {
    stub_cols = (uint8_t) cols;
    stub_rows = (uint8_t) rows;
    stub_screen_px = stub_cellh * rows;
}

/* Modifiers held right now, as MOS reports them -- which is not the same as
 * the modifiers that came with the last key. Tests set it directly. */
static uint8_t stub_mods_now;

void stub_set_keymods(int mods) { stub_mods_now = (uint8_t) mods; }

uint8_t getsysvar_keymods(void)    { return stub_mods_now; }
/* Pixel dimensions consistent with the 8x8 system font, so charW_/charH_ come
 * out at 8 unless a test deliberately sets a different cell size. */
static uint16_t stub_cellw = 8;


void stub_set_cell(int w, int h) {
    stub_cellw = (uint16_t) w;
    stub_cellh = (uint16_t) h;
    stub_screen_px = h * stub_rows;
}

uint16_t getsysvar_scrwidth(void)  { return (uint16_t)(stub_cellw * stub_cols); }
uint16_t getsysvar_scrheight(void) { return (uint16_t)(stub_cellh * stub_rows); }
uint8_t getsysvar_scrCols(void)    { return stub_cols; }
uint8_t getsysvar_scrRows(void)    { return stub_rows; }
uint8_t getsysvar_scrColours(void) { return 16; }

/* --- MOS: a small filesystem, in memory ---
 *
 * It used to be one buffer for whatever was being written and one pointer to
 * whatever was being read, which was enough for an editor that opens a file,
 * reads it whole, and writes it back. Paging is not that: it keeps two scratch
 * files open beside the document, reads and writes at offsets in both, and the
 * whole design turns on text being pushed and popped at their ends. None of
 * that can be exercised against a stub that cannot tell one file from another.
 *
 * So: named files with contents, handles with positions, and seeks. The old
 * helpers are all still here, on top of it -- stub_file_bytes in particular is
 * still the log of everything handed to mos_fwrite, because twenty test files
 * read what was saved that way and none of them care which file it went to.
 */
#define STUB_FILE_CAP (512 * 1024)
#define STUB_FILES     8
#define STUB_HANDLES   8
#define STUB_NAME_MAX  80

typedef struct {
    char  name[STUB_NAME_MAX];
    char* data;
    int   len;
    int   cap;
    int   live;
    int   registered;   /* put there by stub_file_add, not written by the code */
} stub_file;

typedef struct {
    int file;           /* index into stub_fs, or -1 for the fallback content */
    int pos;
    int writable;
    int open;

    /* Opened with FA_OPEN_ALWAYS, which on MOS 3.0.2 makes every write append
     * whatever the file position says -- see `appends` in mos_fwrite. */
    int appends;
} stub_handle;

static stub_file   stub_fs[STUB_FILES];
static stub_handle stub_fhs[STUB_HANDLES];

/* The log of every write, in order, whatever file it went to. Kept because it
 * is what the existing tests read back, and because "what did the editor
 * write" is a different question from "what is in that file". */
static char stub_buf[STUB_FILE_CAP];
static int  stub_len;

static int  stub_opens;
static int  stub_closes;
static int  stub_fail_open;
static int  stub_write_opens;
static int  stub_short_read = -1;
static uint32_t stub_objsize;
static int  stub_objsize_set;
static int  stub_mkdir_count;
static int  stub_delete_count;
static int  stub_short = -1;
static char stub_mkdir_path[256];

/* Served by any name the filesystem does not have. Stands in for "the file
 * exists and holds this", which is how most tests set a document up. */
static const char* stub_content;
static int         stub_content_len;

static void stub_fs_wipe(void) {
    for (int i = 0; i < STUB_FILES; i++) {
        free(stub_fs[i].data);
        stub_fs[i].data = NULL;
        stub_fs[i].len = 0;
        stub_fs[i].cap = 0;
        stub_fs[i].live = 0;
        stub_fs[i].registered = 0;
        stub_fs[i].name[0] = 0;
    }
    for (int i = 0; i < STUB_HANDLES; i++) {
        stub_fhs[i].open = 0;
    }
}

static int stub_fs_find(const char* name) {
    if (name == NULL) {
        return -1;
    }
    for (int i = 0; i < STUB_FILES; i++) {
        if (stub_fs[i].live && strcmp(stub_fs[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

static int stub_fs_make(const char* name) {
    int at = stub_fs_find(name);
    if (at >= 0) {
        return at;
    }
    for (int i = 0; i < STUB_FILES; i++) {
        if (!stub_fs[i].live) {
            size_t n = strlen(name);
            if (n >= STUB_NAME_MAX) {
                n = STUB_NAME_MAX - 1;
            }
            memcpy(stub_fs[i].name, name, n);
            stub_fs[i].name[n] = 0;
            stub_fs[i].live = 1;
            stub_fs[i].len = 0;

            return i;
        }
    }

    return -1;      /* the disk is full, which is a real thing to be */
}

/* Grown rather than fixed, so a test does not have to guess how big a scratch
 * file will get, and so ASan sees the real bounds of each one. */
static int stub_fs_room(stub_file* f, int want) {
    if (want <= f->cap) {
        return 1;
    }
    int cap = f->cap > 0 ? f->cap : 1024;
    while (cap < want) {
        cap *= 2;
    }
    char* grown = (char*) realloc(f->data, (size_t) cap);
    if (grown == NULL) {
        return 0;
    }
    f->data = grown;
    f->cap = cap;

    return 1;
}

void stub_file_add(const char* name, const char* data, int len) {
    const int at = stub_fs_make(name);
    if (at < 0) {
        return;
    }
    if (!stub_fs_room(&stub_fs[at], len > 0 ? len : 1)) {
        return;
    }
    if (len > 0) {
        memcpy(stub_fs[at].data, data, (size_t) len);
    }
    stub_fs[at].len = len;
    stub_fs[at].registered = 1;
}

void stub_file_clear_named(void) { stub_fs_wipe(); }

const char* stub_file_content(const char* name, int* len) {
    const int at = stub_fs_find(name);
    if (at < 0) {
        if (len != NULL) {
            *len = 0;
        }

        return NULL;
    }
    if (len != NULL) {
        *len = stub_fs[at].len;
    }

    return stub_fs[at].data;
}

int stub_file_exists(const char* name) { return stub_fs_find(name) >= 0; }

void stub_file_reset(void) {
    stub_len = 0;
    /* Terminated as well as emptied: stub_file_bytes hands back the buffer and
     * every caller reads it as a string, so bytes left from an earlier write
     * would be found by a strstr looking for something this one never wrote. */
    stub_buf[0] = 0;
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
    stub_fs_wipe();
}

/* The counters alone, leaving the files and the handles as they are. For a
 * test that wants to know what a single operation cost on a document that is
 * already open -- stub_file_reset would wipe the filesystem out from under it. */
void stub_file_reset_counts(void) {
    stub_opens = 0;
    stub_closes = 0;
    stub_write_opens = 0;
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

/* Was the only way to read back what had been written, when writes went to one
 * buffer and reads came from somewhere else. A written file can simply be
 * opened now, so this only still exists for the tests that call it. */
void stub_file_readback(void) {
    stub_content = stub_buf;
    stub_content_len = stub_len;
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
    stub_delete_count++;
    const int at = stub_fs_find(filename);
    if (at >= 0) {
        free(stub_fs[at].data);
        stub_fs[at].data = NULL;
        stub_fs[at].len = 0;
        stub_fs[at].cap = 0;
        stub_fs[at].live = 0;
    }

    return 0;
}

uint8_t mos_ren(const char* filename, const char* newname) {
    const int at = stub_fs_find(filename);
    if (at < 0 || newname == NULL) {
        return 4;       /* FR_NO_FILE */
    }
    mos_del(newname);
    stub_delete_count--;    /* a rename is not a delete the test asked for */
    size_t n = strlen(newname);
    if (n >= STUB_NAME_MAX) {
        n = STUB_NAME_MAX - 1;
    }
    memcpy(stub_fs[at].name, newname, n);
    stub_fs[at].name[n] = 0;

    return 0;
}

uint8_t mos_fopen(const char* filename, uint8_t mode) {
    stub_opens++;
    if ((mode & FA_WRITE) != 0) {
        stub_write_opens++;
    }
    if (stub_fail_open) {
        return 0;       /* MOS reports failure as handle 0 */
    }

    int at = stub_fs_find(filename);
    if (at < 0 && filename != NULL) {
        if (stub_content != NULL && stub_content_len > 0) {
            /* The fallback stands in for "the file exists and holds this", and
             * the first open is where it becomes a file like any other -- so a
             * write to it truncates and a read after that sees the write,
             * rather than the two going to different places. */
            at = stub_fs_make(filename);
            if (at >= 0 && stub_fs_room(&stub_fs[at], stub_content_len)) {
                memcpy(stub_fs[at].data, stub_content, (size_t) stub_content_len);
                stub_fs[at].len = stub_content_len;
            }
        } else if ((mode & FA_WRITE) != 0) {
            at = stub_fs_make(filename);
        }
    }
    if (at < 0) {
        return 0;       /* no such file, and nothing to make one out of */
    }
    if ((mode & FA_CREATE_ALWAYS) != 0) {
        stub_fs[at].len = 0;        /* truncate, as FatFS does */
    }
    /* A file a test put there by name is its own size, whatever size some
     * earlier case asked mos_getfil to report. Opening one is how a test says
     * "and now this is the file", and the size has to follow the content or a
     * font would be read as a document's length. */
    if (stub_fs[at].registered) {
        stub_objsize = (uint32_t) stub_fs[at].len;
        stub_objsize_set = 1;
    }

    for (int i = 0; i < STUB_HANDLES; i++) {
        if (!stub_fhs[i].open) {
            stub_fhs[i].open = 1;
            stub_fhs[i].file = at;
            stub_fhs[i].writable = (mode & FA_WRITE) != 0;
            stub_fhs[i].appends = (mode & FA_WRITE) != 0
                                  && (mode & FA_OPEN_ALWAYS) == FA_OPEN_ALWAYS;
            stub_fhs[i].pos = 0;
            if (at >= 0 && (mode & FA_OPEN_APPEND) == FA_OPEN_APPEND) {
                stub_fhs[i].pos = stub_fs[at].len;
            }

            return (uint8_t) (i + 1);
        }
    }

    return 0;           /* out of handles */
}

static stub_handle* stub_handle_of(uint8_t fh) {
    if (fh == 0 || fh > STUB_HANDLES || !stub_fhs[fh - 1].open) {
        return NULL;
    }

    return &stub_fhs[fh - 1];
}

uint8_t mos_fclose(uint8_t fh) {
    stub_closes++;
    stub_handle* h = stub_handle_of(fh);
    if (h != NULL) {
        h->open = 0;
    }

    return 0;
}

uint8_t mos_flseek(uint8_t fh, uint32_t offset) {
    stub_handle* h = stub_handle_of(fh);
    if (h == NULL) {
        return 9;       /* FR_INVALID_OBJECT */
    }
    h->pos = (int) offset;

    return 0;
}

unsigned mos_fread(uint8_t fh, char* buffer, unsigned numbytes) {
    stub_handle* h = stub_handle_of(fh);
    const char* src;
    int len;
    int pos;

    if (h != NULL && h->file >= 0) {
        src = stub_fs[h->file].data;
        len = stub_fs[h->file].len;
        pos = h->pos;
    } else {
        src = stub_content;
        len = stub_content_len;
        pos = h != NULL ? h->pos : 0;
    }

    unsigned n = numbytes;
    const int left = len - pos;
    if ((int) n > left) {
        n = (unsigned) (left > 0 ? left : 0);
    }
    if (stub_short_read >= 0 && (int) n > stub_short_read) {
        n = (unsigned) stub_short_read;
    }
    /* Actually fill the caller's buffer: a short read would hide an
     * out-of-bounds destination from the sanitizer. */
    if (src != NULL && n > 0) {
        memcpy(buffer, src + pos, n);
        if (h != NULL) {
            h->pos = pos + (int) n;
        }
    }

    return n;
}

unsigned mos_fwrite(uint8_t fh, char* buffer, unsigned numbytes) {
    if (stub_short >= 0 && (int) numbytes > stub_short) {
        numbytes = (unsigned) stub_short;   /* pretend the card filled up */
    }

    stub_handle* h = stub_handle_of(fh);
    if (h != NULL && h->file >= 0) {
        stub_file* f = &stub_fs[h->file];
        /* FA_OPEN_ALWAYS on MOS 3.0.2 writes at the end of the file whatever
         * the position is. A seek before the write reports success and moves
         * fptr, and then the bytes land on the end anyway -- measured on the
         * emulator's MOS 3.0.2, where a seek to 100 in a 1000 byte file put
         * the write at 1000 and left the file 1004 long. Seeking to read is
         * unaffected, and a handle opened FA_READ|FA_WRITE without the flag
         * writes where it was told to.
         *
         * Modelled because the paging store seeks backwards to write, and a
         * stub that let it silently turned the whole feature into something
         * that only worked when the bytes it wrote were already there. */
        if (h->appends) {
            h->pos = f->len;
        }
        if (!stub_fs_room(f, h->pos + (int) numbytes)) {
            return 0;
        }
        /* A write past the end leaves a hole, which FatFS fills with whatever
         * was on the disk. Zeroed here so a test reading it back sees something
         * it can recognise rather than something it cannot reproduce. */
        if (h->pos > f->len) {
            memset(f->data + f->len, 0, (size_t) (h->pos - f->len));
        }
        memcpy(f->data + h->pos, buffer, numbytes);
        h->pos += (int) numbytes;
        if (h->pos > f->len) {
            f->len = h->pos;
        }
    }

    /* And the log, whatever file it went to. It has a ceiling and the files
     * do not, so a long enough test fills it. What it must not do then is
     * report a short write: the bytes went into the file, and the code above
     * reads a short return as the card having filled up. A paging test that
     * pushed a few hundred kilobytes through here had slides start failing
     * for a reason that existed only in the stub. */
    int logged = (int) numbytes;
    if (stub_len + logged > STUB_FILE_CAP) {
        logged = STUB_FILE_CAP - stub_len;
    }
    if (logged > 0) {
        memcpy(stub_buf + stub_len, buffer, (size_t) logged);
        stub_len += logged;
        if (stub_len < STUB_FILE_CAP) {
            stub_buf[stub_len] = 0;     /* see stub_file_reset */
        }
    }

    return numbytes;
}

FIL* mos_getfil(uint8_t fh) {
    static FIL fil;
    stub_handle* h = stub_handle_of(fh);

    /* Normally the content is the file, but a test can say otherwise to
     * exercise a size the read does not agree with. */
    if (stub_objsize_set) {
        fil.obj.objsize = stub_objsize;
    } else if (h != NULL && h->file >= 0) {
        fil.obj.objsize = (uint32_t) stub_fs[h->file].len;
    } else {
        fil.obj.objsize = (uint32_t) stub_content_len;
    }

    return &fil;
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

/* --- MOS: directory walking --- */

static const char* const* stub_dir_names;
static const unsigned*    stub_dir_sizes;
static int                stub_dir_n;
static int                stub_dir_at;

void stub_set_dir(const char* const* names, const unsigned* sizes, int n) {
    stub_dir_names = names;
    stub_dir_sizes = sizes;
    stub_dir_n = n;
    stub_dir_at = 0;
}

uint8_t ffs_dopen(DIR* dir, const char* path) {
    (void) dir;
    (void) path;
    stub_dir_at = 0;

    return stub_dir_names == NULL ? 5 : 0;   /* 5 is FR_NO_PATH */
}

/* An empty name is how FatFS says the directory has ended. */
uint8_t ffs_dread(DIR* dir, FILINFO* info) {
    (void) dir;
    if (info == NULL) {
        return 9;
    }
    if (stub_dir_names == NULL || stub_dir_at >= stub_dir_n) {
        info->fname[0] = 0;
        info->fsize = 0;

        return 0;
    }
    const char* name = stub_dir_names[stub_dir_at];
    size_t n = strlen(name);
    if (n >= sizeof(info->fname)) {
        n = sizeof(info->fname) - 1;
    }
    memcpy(info->fname, name, n);
    info->fname[n] = 0;
    info->fsize = stub_dir_sizes[stub_dir_at];
    info->fattrib = 0;
    stub_dir_at++;

    return 0;
}

uint8_t ffs_dclose(DIR* dir) {
    (void) dir;

    return 0;
}

