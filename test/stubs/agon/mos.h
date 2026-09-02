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
uint8_t  getsysvar_scrCols(void);
uint8_t  getsysvar_scrRows(void);
uint8_t  getsysvar_scrColours(void);

uint8_t  mos_fopen(const char* filename, uint8_t mode);
uint8_t  mos_fclose(uint8_t fh);
unsigned mos_fread(uint8_t fh, char* buffer, unsigned numbytes);
unsigned mos_fwrite(uint8_t fh, char* buffer, unsigned numbytes);
FIL*     mos_getfil(uint8_t fh);

/* --- test-only introspection, not part of the real MOS API --- */

/* Bytes handed to mos_fwrite since the last stub_file_reset(). */
void        stub_file_reset(void);
const char* stub_file_bytes(void);
int         stub_file_size(void);
int         stub_file_opens(void);   /* mos_fopen calls */
int         stub_file_closes(void);  /* mos_fclose calls */
void        stub_file_fail_open(int fail);  /* make the next mos_fopen return 0 */

/* Content mos_fread serves, and the size mos_getfil reports. */
void        stub_file_set_content(const char* data, int len);

#endif
