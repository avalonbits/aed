#include "editor.h"
#include "editor.h"
#include "screen.h"

void runOps(gap_buffer* gb);
void info(gap_buffer* gb);

int main(void) {
    screen scr;
    gap_buffer gb;
    editor ed;

    ed_init(&ed, &scr, &gb, 256, DEFAULT_CURSOR);
    ed_run(&ed);

    ed_destroy(&ed);
    gb_destroy(&gb);
    scr_destroy(&scr);
    return 0;
}
