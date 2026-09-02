/* Host-test stubs for the VDP API. See mos.h in this directory. */
#ifndef _TEST_STUB_AGON_VDP_H_
#define _TEST_STUB_AGON_VDP_H_

#include <stdbool.h>

#include <agon/mos.h>

#define VDP_PUTS(S) mos_puts((char*)&(S), sizeof(S), 0)

void vdp_cursor_left(void);
void vdp_cursor_home(void);
void vdp_clear_screen(void);
void vdp_cursor_enable(bool flag);
void vdp_set_text_colour(int colour);
void vdp_cursor_tab(int xpos, int ypos);

#endif
