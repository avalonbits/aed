#ifndef _SCREEN_H_
#define _SCREEN_H_

#include <stdint.h>

typedef struct _screen {
    uint8_t rows_;
    uint8_t cols_;

    uint8_t currX_;
    uint8_t currY_;

    char cursor_;
} screen;

#define DEFAULT_CURSOR 32

screen* scr_init(screen* scr,  uint8_t rows, uint8_t cols, char cursor);
void scr_destroy(screen* scr);

void scr_clear(screen* scr);
void scr_putc(screen* scr, char ch);

#endif  // _SCREEN_H_
