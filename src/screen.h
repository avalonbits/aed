#ifndef _SCREEN_H_
#define _SCREEN_H_

#include <stdint.h>

typedef struct _screen {
    uint8_t rows_;
    uint8_t cols_;

    uint8_t currX_;
    uint8_t currY_;

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
void scr_putc(screen* scr, uint8_t ch, uint8_t* suffix, int sz);
void scr_bksp(screen* scr);

void scr_left(screen* scr, uint8_t from_ch, uint8_t to_char);
void scr_right(screen* scr, uint8_t from_ch, uint8_t to_char);

#endif  // _SCREEN_H_
