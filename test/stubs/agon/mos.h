/* Host-test stubs for the MOS API. Signatures mirror ~/agondev/include/agon/mos.h
 * closely enough that the real sources compile unchanged; none of these do
 * anything, because the tests exercise buffer/screen arithmetic, not the VDP. */
#ifndef _TEST_STUB_AGON_MOS_H_
#define _TEST_STUB_AGON_MOS_H_

#include <stdint.h>

#define sysvar_vdp_pflags    0x04
#define sysvar_scrpixelIndex 0x16

static inline void waitvblank(void) {}
static inline void mos_puts(const char* b, unsigned size, char d) {
    (void)b; (void)size; (void)d;
}
static inline uint8_t* mos_sysvars(void) {
    static uint8_t sysvars[64];
    sysvars[sysvar_vdp_pflags] = 0x04;  /* pretend the VDP already replied */
    return sysvars;
}
static inline uint8_t getsysvar_scrCols(void)    { return 80; }
static inline uint8_t getsysvar_scrRows(void)    { return 25; }
static inline uint8_t getsysvar_scrColours(void) { return 16; }

#endif
