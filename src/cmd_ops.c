#include "cmd_ops.h"

#include <agon/vdp_vdu.h>
#include <stdio.h>

#define UN(x) (void)(x)

static void cmd_noop(screen* scr, gap_buffer* buf, key_command kc) {
    UN(scr);UN(buf);UN(kc);
}

static void cmd_putc(screen* scr, gap_buffer* buf, key_command kc) {
	gb_put(buf, kc.key);
    int sz = 0;
    uint8_t* suffix = gb_suffix(buf, &sz);
    scr_putc(scr, kc.key, suffix, sz);
}

static void cmd_bksp(screen* scr, gap_buffer* buf, key_command kc) {
    if (!gb_bksp(buf)) {
        return;
    }
    scr_bksp(scr);
}

static void cmd_left(screen* scr, gap_buffer* buf, key_command kc) {
    uint8_t from_ch = gb_peek(buf);
    uint8_t to_ch = gb_prev(buf);
    if (to_ch == 0) {
        // We are at the begining of the buffer.
        return;
    }
    scr_left(scr, from_ch, to_ch);
}

static void cmd_rght(screen* scr, gap_buffer* buf, key_command kc) {
    uint8_t from_ch = gb_peek(buf);
    uint8_t to_ch = gb_next(buf);

    if (from_ch == 0 && to_ch == 0) {
        return;
    }
    scr_right(scr, from_ch, to_ch);
}

cmd_op cmds[] = {
    cmd_noop,
    cmd_noop,

    cmd_putc,
    cmd_bksp,
    cmd_left,
    cmd_rght
};


