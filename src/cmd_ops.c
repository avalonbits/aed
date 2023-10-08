#include "cmd_ops.h"

#include <agon/vdp_vdu.h>
#include <stdio.h>

#define UN(x) (void)(x)

static void cmd_noop(screen* scr, char_buffer* buf, key_command kc) {
    UN(scr);UN(buf);UN(kc);
}

static void cmd_putc(screen* scr, char_buffer* buf, key_command kc) {
	cb_put(buf, kc.key);
    int sz = 0;
    uint8_t* suffix = cb_suffix(buf, &sz);
    scr_putc(scr, kc.key, suffix, sz);
}

static void cmd_bksp(screen* scr, char_buffer* buf, key_command kc) {
    UN(kc);

    int sz = 0;
    cb_suffix(buf, &sz);
    scr_erase(scr, sz);

    if (!cb_bksp(buf)) {
        return;
    }
    sz = 0;
    uint8_t* suffix = cb_suffix(buf, &sz);
    scr_bksp(scr, suffix, sz);
}

static void cmd_left(screen* scr, char_buffer* buf, key_command kc) {
    UN(kc);
    uint8_t from_ch = cb_peek(buf);
    uint8_t to_ch = cb_prev(buf);
    if (to_ch == 0) {
        // We are at the begining of the buffer.
        return;
    }
    scr_left(scr, from_ch, to_ch);
}

static void cmd_rght(screen* scr, char_buffer* buf, key_command kc) {
    UN(kc);
    uint8_t from_ch = cb_peek(buf);
    if (from_ch == 0) {
        return;
    }
    uint8_t to_ch = cb_next(buf);
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


