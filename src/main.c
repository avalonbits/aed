#include <stdio.h>

#include "gap_buffer.h"

int main(void) {
    gap_buffer gb;
    if (!gb_init(&gb, 256 << 10)) {
        return -1;
    }
    gb_destroy(&gb);
    return 0;
}
