/*
 * Keys through the keymap and the loop's two halves: ed_translate says what a
 * key means, ed_handle runs it through the editor.
 *
 * A program with keys of its own gives the editor its own table, so these
 * check the matching rules the table is read by, that a binding's flags are
 * what the loop acts on, and that the whole path -- key, translation,
 * selection, command, repaint -- lands in the document.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "aed.h"
#include "editor.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static editor ed;

static key_command meaning(const keymap* km, char ch, VKey vkey, char mods) {
    const key_press kp = { ch, vkey, mods };

    return ed_translate(km, kp);
}

/* One key, the way ed_run hands it over. */
static int handle(char ch, VKey vkey, char mods) {
    return ed_handle(&ed, meaning(ed.keys_, ch, vkey, mods)) ? 1 : 0;
}

/* Whether line 1 of the document is `want`, exactly. */
static int first_line_is(const char* want) {
    const tb_pos was = tb_tell(&ed.doc_.buf_);
    const tb_pos top = { .line = 1, .x = 0 };
    tb_seek(&ed.doc_.buf_, top);
    const split_line sl = tb_curr_line(&ed.doc_.buf_);
    const int n = (int) strlen(want);
    int len = sl.ssz_;
    while (len > 0 && (sl.suffix_[len - 1] == '\n' || sl.suffix_[len - 1] == '\r')) {
        len--;
    }
    tb_seek(&ed.doc_.buf_, was);

    return len == n && memcmp(sl.suffix_, want, (size_t) n) == 0;
}

static int f9s = 0;

static void count(editor* e) {
    (void) e;
    f9s++;
}

/* A program's own keys: F9, a copy that is only a count, and nothing else. */
static const key_binding MINE[] = {
    { VK_F9, 0,        0,           count },
    { VK_c,  MOD_CTRL, KC_OWNS_SEL, count },
};
static const keymap MY_KEYS = { MINE, 2, NULL };

/* The same keys in front of AED's, which is how a program adds a few. */
static const keymap MINE_THEN_AED = { MINE, 2, &AED_KEYS };

int main(void) {
    stub_discard_output();

    static const char DOC[] = "one\r\ntwo\r\n";
    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    check("an editor starts", ed_init(&ed, 8, "doc.txt") != NULL, 1);
    check("  reading AED's keys", ed.keys_ == &AED_KEYS ? 1 : 0, 1);

    /* --- a key goes through translation and the loop into the document --- */
    {
        check("typing through the loop carries on", handle('x', VK_x, 0), 1);
        check("  and the character is in the document", first_line_is("xone"), 1);
        const int lines = tb_ymax(&ed.doc_.buf_);
        handle(13, VK_RETURN, 0);
        check("RETURN through the loop splits the line",
              tb_ymax(&ed.doc_.buf_), lines + 1);
        check("  leaving the first half", first_line_is("x"), 1);
    }

    /* --- the flags the loop reads come from the binding and the key --- */
    {
        handle(0, VK_UP, 0);
        handle(0, VK_HOME, 0);
        handle(0, VK_RIGHT, MOD_SHFT);
        check("SHIFT+RIGHT starts a selection", ed.doc_.selecting_ ? 1 : 0, 1);
        handle(3, VK_c, MOD_CTRL);
        check("  which CTRL+C, owning it, leaves alone",
              ed.doc_.selecting_ ? 1 : 0, 1);
        handle(0, VK_LEFT, 0);
        check("  and a plain LEFT ends", ed.doc_.selecting_ ? 1 : 0, 0);

        /* CTRL+BACKSPACE is bound to nothing, and still edits: it is the key
         * that says so, not a binding. */
        handle(0, VK_RIGHT, MOD_SHFT);
        const key_command kc = meaning(&AED_KEYS, 0x7F, VK_BACKSPACE, MOD_CTRL);
        check("CTRL+BACKSPACE is bound to nothing", kc.cmd == NULL ? 1 : 0, 1);
        check("  and still edits", (kc.flags & KC_EDITS) ? 1 : 0, 1);
        ed_handle(&ed, kc);
        check("  so it takes a selection away", first_line_is(""), 1);
        check("  and ends it", ed.doc_.selecting_ ? 1 : 0, 0);
    }

    /* --- how a table is read --- */
    {
        check("CTRL+S saves",
              meaning(&AED_KEYS, 19, VK_s, MOD_CTRL).cmd == ed_cmd_save, 1);
        check("CTRL+ALT+S, listed first, is save-as",
              meaning(&AED_KEYS, 19, VK_s, MOD_CTRL | MOD_ALT).cmd == cmd_save_as, 1);
        check("CTRL+SHIFT+S is still save, SHIFT unnamed",
              meaning(&AED_KEYS, 19, VK_S, MOD_CTRL | MOD_SHFT).cmd == ed_cmd_save, 1);
        const key_command s = meaning(&AED_KEYS, 's', VK_s, 0);
        check("an s without CTRL is typed", (s.flags & KC_PUTC) ? 1 : 0, 1);
        check("  and runs no binding", s.cmd == NULL ? 1 : 0, 1);
        check("a CTRL binding needs CTRL: HOME alone is the line's start",
              meaning(&AED_KEYS, 0, VK_HOME, 0).cmd == cmd_home, 1);
        check("  and a plain one does not match with it",
              meaning(&AED_KEYS, 0, VK_HOME, MOD_CTRL).cmd == cmd_doc_top, 1);
        check("SHIFT+LEFT finds LEFT, which is how a selection grows",
              meaning(&AED_KEYS, 0, VK_LEFT, MOD_SHFT).cmd == cmd_left, 1);
    }

    /* --- a program's own keys --- */
    {
        ed.keys_ = &MY_KEYS;
        check("its own key runs its own command", handle(0, VK_F9, 0), 1);
        check("  once", f9s, 1);
        handle('y', VK_y, 0);
        check("typing does not need the table", first_line_is("y"), 1);
        check("a key it does not bind means nothing",
              meaning(&MY_KEYS, 19, VK_s, MOD_CTRL).cmd == NULL ? 1 : 0, 1);

        handle(0, VK_HOME, MOD_SHFT);
        check("with no LEFT or HOME bound, SHIFT+HOME only starts selecting",
              ed.doc_.selecting_ ? 1 : 0, 1);
        handle(3, VK_c, MOD_CTRL);
        check("its CTRL+C runs its command", f9s, 2);
        check("  and its flag keeps the selection", ed.doc_.selecting_ ? 1 : 0, 1);
        ed.keys_ = &AED_KEYS;
    }

    /* --- a program's keys in front of AED's --- */
    {
        check("its own key still runs its own command",
              meaning(&MINE_THEN_AED, 0, VK_F9, 0).cmd == count, 1);
        check("a key it does not bind falls through to AED's",
              meaning(&MINE_THEN_AED, 19, VK_s, MOD_CTRL).cmd == ed_cmd_save, 1);
        const key_command c = meaning(&MINE_THEN_AED, 3, VK_c, MOD_CTRL);
        check("a key both bind is its own", c.cmd == count, 1);
        check("  with its own flags", c.flags & KC_OWNS_SEL, KC_OWNS_SEL);
        check("AED's own keys, asked directly, are unchanged",
              meaning(&AED_KEYS, 3, VK_c, MOD_CTRL).cmd == cmd_copy, 1);
    }

    /* --- no keymap at all --- */
    {
        const key_command s = meaning(NULL, 's', VK_s, 0);
        check("with no keymap, typing still types", (s.flags & KC_PUTC) ? 1 : 0, 1);
        check("  and CTRL+S means nothing",
              meaning(NULL, 19, VK_s, MOD_CTRL).cmd == NULL ? 1 : 0, 1);
        ed.keys_ = NULL;
        check("  so an editor with none keeps going", handle(19, VK_s, MOD_CTRL), 1);
        ed.keys_ = &AED_KEYS;
    }

    /* --- leaving --- */
    {
        /* The document has changed, so CTRL+Q asks first. ESC is cancel. */
        const stub_key esc[] = { { .ch = 27, .vk = VK_ESCAPE } };
        stub_set_keys(esc, 1);
        check("CTRL+Q, cancelled, carries on", handle(17, VK_q, MOD_CTRL), 1);
        const stub_key no[] = { { .ch = 'n', .vk = VK_n } };
        stub_set_keys(no, 1);
        check("CTRL+Q, not saving, stops the loop", handle(17, VK_q, MOD_CTRL), 0);
    }

    ed_destroy(&ed);
    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
