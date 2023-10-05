#include "editor.h"
#include "editor.h"
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

    editor ed;
    ed_init(&ed, &scr, &gb);
    ed_run(&ed);

    ed_destroy(&ed);
    gb_destroy(&gb);
    scr_destroy(&scr);
    return 0;
}
