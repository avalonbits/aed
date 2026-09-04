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
void     stub_set_cell(int w, int h);

uint8_t  getsysvar_keymods(void);
uint16_t getsysvar_scrwidth(void);
uint16_t getsysvar_scrheight(void);
uint8_t  getsysvar_scrCols(void);
uint8_t  getsysvar_scrRows(void);
uint8_t  getsysvar_scrColours(void);

/* A scripted key sequence, served through the stubbed <agon/keyboard.h> queue.
 * Every blocking read in AED -- the main loop and the modal prompts alike --
 * drains that queue, so one script drives all of them. When it runs out,
 * ESCAPE is returned forever so a test can never hang waiting for a key.
 *
 * `up` marks an entry as a key being released rather than pressed. MOS reports
 * both; AED acts on neither release nor modifier-only press, and leaving the
 * field out gives a press, which is what nearly every test wants. The modifier
 * masks come from vkey.h, not here. */
typedef struct {
    char ch;
    unsigned char vk;
    unsigned char mods;
    unsigned char up;
} stub_key;
void        stub_set_keys(const stub_key* keys, int n);

/* Whether the MOS key vector is currently installed. AED must take it back down
 * on the way out: MOS keeps calling it otherwise. */
int         stub_keys_installed(void);

/* Makes the next `n` polls of the event queue come up empty, so a test can see
 * what the caller does while nothing has arrived. On real hardware that is the
 * normal case -- the queue is empty far more often than not. */
void        stub_keys_stall(int n);

/* The modifiers MOS reports as held right now. */
void        stub_set_keymods(int mods);

/* The last colours handed to vdp_set_text_colour. Foreground and background are
 * distinguished the way the VDP does it: background is offset by 128. */
/* Whether vdp_set_text_colour also writes its VDU bytes to the stream. Off by
 * default: colour 0 emits a NUL, which truncates the stream for tests that read
 * it back as text. Turn it on to see where a highlight starts and stops. */
void        stub_emit_colours(int on);

int         stub_last_fg(void);
int         stub_last_bg(void);
void        stub_colours_reset(void);

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

/* Writes to the VDP, counted. The bytes reaching the screen are the same
 * whether they go one per call or a row at a time, so the stream cannot show
 * the difference -- but every call is an entry into MOS, and that is what the
 * paint paths are trying to avoid. stub_max_write is the largest single write,
 * which says whether a row went out in one piece.
 *
 * stub_tab_at is the write count as of the last cursor move. Buffered output
 * has to be flushed before the cursor is moved, or it lands somewhere else;
 * when that is done right, the last move comes after the last write. */
void        stub_writes_reset(void);
int         stub_writes(void);
int         stub_max_write(void);
int         stub_tab_at(void);

/* Sends VDU bytes and MOS diagnostics to /dev/null. Tests that assert on state
 * rather than on what reached the screen should call this first, so the suite's
 * stdout stays clean for anything parsing it. Tests that read the VDU stream
 * back redirect stdout themselves instead. */
void        stub_discard_output(void);

#endif
