/* Host-test stubs for the MOS API. Signatures mirror ~/agondev/include/agon/mos.h
 * closely enough that the real sources compile unchanged.
 *
 * Implementations live in test/stubs/agon_stubs.c rather than here, because the
 * file-I/O stubs need shared state that a header of static inlines cannot hold. */
#ifndef _TEST_STUB_AGON_MOS_H_
#define _TEST_STUB_AGON_MOS_H_

#include <stdint.h>

#define sysvar_vdp_pflags    0x04
#define sysvar_scrpixelIndex 0x16

#define FA_READ           0x01
#define FA_WRITE          0x02
#define FA_CREATE_ALWAYS  0x08
#define FA_OPEN_ALWAYS    0x10

typedef struct { uint32_t objsize; } FFOBJID;
typedef struct { FFOBJID obj; } FIL;

void     waitvblank(void);
void     mos_puts(const char* buffer, unsigned size, char delimiter);
uint8_t* mos_sysvars(void);
/* The screen MOS reports. 80x25 unless a test says otherwise -- the widest
 * real mode is 128 columns, which does not fit in a signed char. */
void     stub_set_screen(int cols, int rows);

uint8_t  getsysvar_scrCols(void);
uint8_t  getsysvar_scrRows(void);
uint8_t  getsysvar_scrColours(void);

/* Keyboard. The prompts in user_input.c drive their own blocking loops; these
 * let the controller link without any test actually pressing a key. The
 * modifier masks come from vkey.h, not here. */

/* A scripted key sequence for the modal prompts, which drive their own blocking
 * getch loops. When it runs out, ESCAPE is returned forever so a test can never
 * hang in one of them. */
/* `up` marks a key release. It is last and defaults to zero so that the three
 * field initialisers everywhere else stay presses, which is what a test writing
 * `{ 'y', VK_y, 0 }` means. */
typedef struct {
    char ch;
    unsigned char vk;
    unsigned char mods;
    unsigned char up;
} stub_key;
void        stub_set_keys(const stub_key* keys, int n);

/* The last colours handed to vdp_set_text_colour. Foreground and background are
 * distinguished the way the VDP does it: background is offset by 128. */
/* Whether vdp_set_text_colour also writes its VDU bytes to the stream. Off by
 * default: colour 0 emits a NUL, which truncates the stream for tests that read
 * it back as text. Turn it on to see where a highlight starts and stops. */
void        stub_emit_colours(int on);

int         stub_last_fg(void);
int         stub_last_bg(void);
void        stub_colours_reset(void);
uint8_t  getsysvar_vkeycode(void);

/* The key-packet sysvars. vkeycount moves for every packet the VDP sends,
 * which is how a keypress is noticed; the rest describe the packet it last
 * reported. */
uint8_t  getsysvar_vkeycount(void);
uint8_t  getsysvar_vkeydown(void);
uint8_t  getsysvar_keyascii(void);
uint8_t  getsysvar_keymods(void);

uint8_t  mos_fopen(const char* filename, uint8_t mode);
uint8_t  mos_mkdir(const char* path);
uint8_t  mos_del(const char* filename);
uint8_t  mos_fclose(uint8_t fh);
unsigned mos_fread(uint8_t fh, char* buffer, unsigned numbytes);
unsigned mos_fwrite(uint8_t fh, char* buffer, unsigned numbytes);
FIL*     mos_getfil(uint8_t fh);

/* --- test-only introspection, not part of the real MOS API --- */

/* Bytes handed to mos_fwrite since the last stub_file_reset(). */
void        stub_file_reset(void);
const char* stub_file_bytes(void);
int         stub_file_size(void);
/* Scripted keys consumed so far. Lets a test assert that something actually
 * waited for a keypress rather than drawing and moving straight on. */
int         stub_keys_read(void);

/* Makes mos_fread return fewer bytes than asked for, so the read path can be
 * checked against a card that stops part way. */
void        stub_file_short_read(int n);

/* Reports a file size that the content does not match, for the case where the
 * filesystem claims more than can actually be read. */
void        stub_file_set_objsize(uint32_t n);

int         stub_file_opens(void);   /* mos_fopen calls */
int         stub_file_opens_for_write(void); /* those asking to write */
int         stub_file_closes(void);  /* mos_fclose calls */
void        stub_file_fail_open(int fail);  /* make the next mos_fopen return 0 */

/* Content mos_fread serves, and the size mos_getfil reports. */
void        stub_file_set_content(const char* data, int len);
void        stub_file_readback(void);

/* Directories mos_mkdir was asked to create since the last reset. */
int         stub_mkdirs(void);
const char* stub_last_mkdir(void);

/* Make mos_fwrite accept only `n` bytes, as a full card would. */
void        stub_file_short_write(int n);
/* Files mos_del was asked to remove since the last reset. */
int         stub_deletes(void);

/* Sends VDU bytes and MOS diagnostics to /dev/null. Tests that assert on state
 * rather than on what reached the screen should call this first, so the suite's
 * stdout stays clean for anything parsing it. Tests that read the VDU stream
 * back redirect stdout themselves instead. */
void        stub_discard_output(void);

#endif
