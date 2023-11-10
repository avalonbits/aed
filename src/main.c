#include "editor.h"
#include "editor.h"
#include "screen.h"

#include <stdio.h>

int main(int argc, char** argv) {
    editor ed;

    const char* fname = NULL;
    if (argc > 1) {
        fname = argv[1];
    }
    ed_init(&ed, 256, fname);
    ed_run(&ed);

    ed_destroy(&ed);
    return 0;
}
