#include "editor.h"
#include "editor.h"
#include "screen.h"

void runOps(char_buffer* cb);
void info(char_buffer* cb);

int main(void) {
    screen scr;
    char_buffer cb;
    editor ed;

    ed_init(&ed, &scr, &cb, 256, DEFAULT_CURSOR);
    ed_run(&ed);

    ed_destroy(&ed);
    cb_destroy(&cb);
    scr_destroy(&scr);
    return 0;
}
