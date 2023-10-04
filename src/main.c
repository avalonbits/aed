#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gap_buffer.h"

void runOps(gap_buffer* gb);
void info(gap_buffer* gb);

int main(void) {
    gap_buffer gb;
    if (!gb_init(&gb, 256 << 10)) {
        return -1;
    }

    runOps(&gb);
    gb_destroy(&gb);

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
    CHAR* buf = (CHAR*) malloc(sz+1);
    sz = gb_copy(gb, buf, sz);
    buf[sz] = '\0';

    for (int i = 0; i < sz; i++) {
        outchar(buf[i]);
    }
    free(buf);
}

void info(gap_buffer* gb) {
    printf("Size: %d | Available: %d | Used %d\n", gb_size(gb), gb_available(gb), gb_used(gb));
}
