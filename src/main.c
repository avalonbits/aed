#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gap_buffer.h"
#include "input_handler.h"
#include "screen.h"

void runOps(gap_buffer* gb);
void info(gap_buffer* gb);

int main(void) {
    screen scr;
    scr_init(&scr, DEFAULT_CURSOR);

    gap_buffer gb;
    if (!gb_init(&gb, 256 << 10)) {
        return -1;
    }


    while (true) {
        key_command kc = read_input();
        if (kc.key == 'q') {
            break;
        }
        scr_putc(&scr, kc.key);
    }

    gb_destroy(&gb);
    scr_destroy(&scr);
    return 0;
}


static const char* msg = "Hello, world!";
void runOps(gap_buffer* gb) {
    info(gb);

    for (size_t i = 0; i < strlen(msg); i++) {
        gb_put(gb, (CHAR)msg[i]);
    }
    info(gb);

    int sz = gb_used(gb);
    info(gb);
    for (int j = 0; j < sz+1; j++) {
        gb_prev(gb, 1);
        CHAR* buf = (CHAR*) malloc(sz);
        sz = gb_copy(gb, buf, sz);

        for (int i = 0; i < sz; i++) {
            outchar(buf[i]);
        }
        outchar('\r');
        outchar('\n');
        free(buf);
    }
    info(gb);
    for (int j = 0; j < sz+1; j++) {
        gb_next(gb, 1);
        CHAR* buf = (CHAR*) malloc(sz);
        sz = gb_copy(gb, buf, sz);

        for (int i = 0; i < sz; i++) {
            outchar(buf[i]);
        }
        outchar('\r');
        outchar('\n');
        free(buf);
    }

}

void info(gap_buffer* gb) {
    printf("Size: %d | Available: %d | Used %d\n", gb_size(gb), gb_available(gb), gb_used(gb));
}
