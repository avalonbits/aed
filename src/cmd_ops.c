#include "cmd_ops.h"

#include <agon/vdp_vdu.h>
#include <stdio.h>

#define UN(x) (void)(x)

static void cmd_noop(screen* scr, text_buffer* buf, key_command kc) {
    UN(scr);UN(buf);UN(kc);
}

static void cmd_putc(screen* scr, text_buffer* buf, key_command kc) {
	tb_put(buf, kc.key);
    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    scr_putc(scr, kc.key, suffix, sz);
}

static void cmd_del(screen* scr, text_buffer* buf, key_command kc) {
    if (tb_eol(buf)) {
        return;
    }
    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    if (!tb_del(buf)) {
        return;
    }
    scr_del(scr, suffix, sz);
}

static void cmd_bksp(screen* scr, text_buffer* buf, key_command kc) {
    UN(kc);
    if (tb_bol(buf)) {
        return;
    }

    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    scr_erase(scr, sz);

    if (!tb_bksp(buf)) {
        return;
    }
    scr_bksp(scr, suffix, sz);
}

static void cmd_newl(screen* scr, text_buffer* buf, key_command kc) {
    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    if (tb_newline(buf)) {
        scr_newl(scr, suffix, sz);
    }
}

static void cmd_left(screen* scr, text_buffer* buf, key_command kc) {
    UN(kc);
    if (tb_bol(buf)) {
        return;
    }
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_prev(buf);
    if (to_ch == 0) {
        // We are at the begining of the buffer.
        return;
    }
    scr_left(scr, from_ch, to_ch);
}

static void cmd_rght(screen* scr, text_buffer* buf, key_command kc) {
    UN(kc);
    if (tb_xpos(buf) >= scr->cols_ || tb_eol(buf)) {
        return;
    }

    uint8_t from_ch = tb_peek(buf);
    if (from_ch == 0 ) {
        return;
    }
    uint8_t to_ch = tb_next(buf);
    scr_right(scr, from_ch, to_ch);
}

static void cmd_up(screen* scr, text_buffer* buf, key_command kc) {
    if (tb_ypos(buf) == 1) {
        return;
    }
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_up(buf);
    scr_up(scr, from_ch, to_ch, tb_xpos(buf)-1);
}

static void cmd_down(screen* scr, text_buffer* buf, key_command kc) {
    tb_down(buf);
}

static void cmd_home(screen* scr, text_buffer* buf, key_command kc) {
    if (tb_bol(buf)) {
        return;
    }

    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_home(buf);
    if (to_ch != 0) {
        scr_home(scr, from_ch, tb_peek(buf));
    }
}

static void cmd_end(screen* scr, text_buffer* buf, key_command kc) {
    if (tb_eol(buf)) {
        return;
    }
    tb_end(buf);
    scr_end(scr);
}

cmd_op cmds[] = {
    cmd_noop,
    cmd_noop,

    cmd_putc,
    cmd_del,
    cmd_bksp,
    cmd_newl,

    cmd_left,
    cmd_rght,
    cmd_up,
    cmd_down,
    cmd_home,
    cmd_end
};


