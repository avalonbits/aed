#include "editor.h"
#include "editor.h"
#include "screen.h"

int main(void) {
    editor ed;

    ed_init(&ed, 256, DEFAULT_CURSOR);
    ed_run(&ed);

    ed_destroy(&ed);
    return 0;
}
