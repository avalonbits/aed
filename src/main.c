#include <stdio.h>
#include <stdlib.h>

#include "gap_buffer.h"

int main(void) {
    gap_buffer gb;
    if (!gb_init(&gb, 256 << 10)) {
        return -1;
    }
    printf("Size: %d | Available: %d | Used %d", gb_size(&gb), gb_available(&gb), gb_used(&gb));
    gb_destroy(&gb);
    return 0;
}
