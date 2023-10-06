#include "cmd_ops.h"

#define UN(x) (void)(x)

void cmd_noop(screen* scr, gap_buffer* buf, key_command kc) {
    UN(scr);UN(buf);UN(kc);
}

void cmd_putc(screen* scr, gap_buffer* buf, key_command kc) {
    scr_putc(scr, kc.key);
	gb_put(buf, kc.key);
}

void cmd_bksp(screen* scr, gap_buffer* buf, key_command kc) {
}

void cmd_left(screen* scr, gap_buffer* buf, key_command kc) {
}


cmd_op cmds[] = {
    cmd_noop,
    cmd_noop,

    cmd_putc,
    cmd_bksp,
    cmd_left
};


