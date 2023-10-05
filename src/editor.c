#include "editor.h"

#include <mos_api.h>
#include <stdbool.h>

editor* ed_init(editor* ed, screen* scr, gap_buffer* gb, int mem_kb, char cursor) {
    if (!gb_init(gb, mem_kb << 10)) {
       return NULL;
    }
    scr_init(scr, cursor, DEFAULT_FG, DEFAULT_BG);
    ed->scr_ = scr;
    ed->buf_ = gb;
    return ed;
}

void ed_destroy(editor* ed) {
    ed->scr_ = NULL;
    ed->buf_ = NULL;
}

typedef enum _command {
    CMD_NOP = 0,
    CMD_PUTC = 1,
} Command;

typedef struct _key_command {
    Command cmd;
    char key;
} key_command;

key_command read_input();

void ed_run(editor* ed) {
    screen* scr = ed->scr_;

    while (true) {
        key_command kc = read_input();
        if (kc.key == 'q') {
            break;
        }
        if (kc.key >= 0x7F) {
            // These are control chars. Ignore them for now.
            continue;
        }
        scr_putc(scr, kc.key);
    }
}



key_command read_input() {
    char ch = getch();

    key_command kc = {CMD_PUTC, ch};
    return kc;
}

