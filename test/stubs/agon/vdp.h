/* Host-test stubs for the VDP API. See mos.h in this directory. */
#ifndef _TEST_STUB_AGON_VDP_H_
#define _TEST_STUB_AGON_VDP_H_

#include <stdbool.h>

#include <agon/mos.h>

#define VDP_PUTS(S) mos_puts((char*)&(S), sizeof(S), 0)

static inline void vdp_cursor_left(void) {}
static inline void vdp_cursor_home(void) {}
static inline void vdp_clear_screen(void) {}
static inline void vdp_cursor_enable(bool f)         { (void)f; }
static inline void vdp_set_text_colour(int c)        { (void)c; }
static inline void vdp_cursor_tab(int x, int y)      { (void)x; (void)y; }

#endif
