#ifndef _SCREEN_H_
#define _SCREEN_H_

#include <stdint.h>

typedef struct _screen {
    uint8_t rows_;
    uint8_t cols_;

    uint8_t currX_;
    uint8_t currY_;

    uint8_t topY_;
    uint8_t bottomY_;

    char tab_size_;
    char cursor_;
    char fg_;
    char bg_;
} screen;

#define DEFAULT_CURSOR 32
#define DEFAULT_FG 15
#define DEFAULT_BG 0

// Setup.
screen* scr_init(screen* scr, char cursor, char fg, char bg);
void scr_destroy(screen* scr);
void scr_clear(screen* scr);
void scr_footer(screen* scr, int x, int y);

// Input.
void scr_putc(screen* scr, uint8_t ch, uint8_t* prefix, int psz, uint8_t* suffix, int ssz);
void scr_del(screen* scr, uint8_t* suffix, int sz);
void scr_bksp(screen* scr, uint8_t* suffix, int sz);

// Navigation.
void scr_left(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t deltaX, uint8_t* suffix, int sz);
void scr_right(screen* scr, uint8_t from_ch, uint8_t to_c, uint8_t deltaX, uint8_t* prefix, int sz);
void scr_up(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t currX);
void scr_down(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t currX);
void scr_home(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t* prefix, int sz);
void scr_end(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t deltaX, uint8_t* suffix, int sz);

// Screen management.
void scr_write_line(screen* scr, uint8_t ypos, uint8_t* buf, int sz);
void scr_show_cursor_ch(screen* scr, uint8_t ch);
void scr_erase(screen* scr, int sz);

#endif  // _SCREEN_H_
