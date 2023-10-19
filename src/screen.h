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

    char cursor_;
    char fg_;
    char bg_;
} screen;

#define DEFAULT_CURSOR 32
#define DEFAULT_FG 15
#define DEFAULT_BG 0

screen* scr_init(screen* scr, char cursor, char fg, char bg);
void scr_destroy(screen* scr);

void scr_clear(screen* scr);
void scr_footer(screen* scr, int x, int y);
void scr_putc(screen* scr, uint8_t ch, uint8_t* suffix, int sz);
void scr_del(screen* scr, uint8_t* suffix, int sz);
void scr_bksp(screen* scr, uint8_t* suffix, int sz);
void scr_newl(screen* scr, uint8_t* suffix, int sz);
void scr_left(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t deltaX);
void scr_right(screen* scr, uint8_t from_ch, uint8_t to_c, uint8_t deltaX);
void scr_home(screen* scr, uint8_t from_ch, uint8_t to_ch);
void scr_end(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t deltaX);
void scr_up(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t currX);
void scr_erase(screen* scr, int sz);
#endif  // _SCREEN_H_
