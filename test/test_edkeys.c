/*
 * The editor's own keys, which any program built on the UI gets.
 *
 * ED_KEYS binds moving, editing, selecting, the clipboard, finding, going to a
 * line, undo and redo, and leaves a program's own keys -- its files, leaving,
 * its help and settings -- to the program, which puts a table of them in front
 * with its keymap's `next`. This includes only the UI's headers, so it is
 * linked against the core and the UI alone: ED_KEYS is the UI's, not AED's.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

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

static key_command meaning(const keymap* km, char ch, VKey vkey, char mods) {
    const key_press kp = { ch, vkey, mods };

    return ed_translate(km, kp);
}

static int saves;

static void my_save(editor* ed) {
    (void) ed;
    saves++;
}

/* A program's own keys in front of the editor's. */
static const key_binding MINE[] = {
    { VK_s, MOD_CTRL, 0, my_save },
};
static const keymap MY_KEYS = { MINE, 1, &ED_KEYS };

int main(void) {
    stub_discard_output();

    /* --- what the editor binds --- */
    {
        check("LEFT moves left", meaning(&ED_KEYS, 0, VK_LEFT, 0).cmd == cmd_left, 1);
        check("CTRL+LEFT moves a word",
              meaning(&ED_KEYS, 0, VK_LEFT, MOD_CTRL).cmd == cmd_w_left, 1);
        check("CTRL+END is the end of the document",
              meaning(&ED_KEYS, 0, VK_END, MOD_CTRL).cmd == cmd_doc_end, 1);
        check("PAGE DOWN pages", meaning(&ED_KEYS, 0, VK_PAGEDOWN, 0).cmd
                                 == cmd_page_down, 1);
        check("CTRL+D deletes the line",
              meaning(&ED_KEYS, 4, VK_d, MOD_CTRL).cmd == cmd_del_line, 1);
        const key_command c = meaning(&ED_KEYS, 3, VK_c, MOD_CTRL);
        check("CTRL+C copies", c.cmd == cmd_copy, 1);
        check("  and owns the selection", c.flags & KC_OWNS_SEL, KC_OWNS_SEL);
        check("CTRL+F finds", meaning(&ED_KEYS, 6, VK_f, MOD_CTRL).cmd == cmd_find, 1);
        check("CTRL+G goes to a line",
              meaning(&ED_KEYS, 7, VK_g, MOD_CTRL).cmd == cmd_goto, 1);
        check("CTRL+Z undoes", meaning(&ED_KEYS, 26, VK_z, MOD_CTRL).cmd == cmd_undo, 1);
        check("  and it ends the chain", ED_KEYS.next == NULL ? 1 : 0, 1);
    }

    /* --- what it leaves to the program --- */
    {
        static const VKey theirs[] = { VK_s, VK_o, VK_q, VK_h, VK_e };
        int bound = 0;
        for (int i = 0; i < 5; i++) {
            bound += meaning(&ED_KEYS, 0, theirs[i], MOD_CTRL).cmd != NULL;
        }
        check("CTRL+S, O, Q, H and E are left to the program", bound, 0);
    }

    /* --- characters a keyboard layout composes --- */
    {
        /* A dead key and a letter -- ´ then a, on the Spanish layout -- reach
         * MOS as one key carrying the accented letter, in the VDP's
         * Windows-1252: á is 0xE1, ñ 0xF1. char is signed on the eZ80 (and
         * here), so those arrive negative, and were dropped as control
         * codes. */
        check("á, as the Spanish layout sends it, is typed",
              (meaning(&ED_KEYS, (char) 0xE1, VK_ACUTE_a, 0).flags & KC_PUTC) != 0, 1);
        check("  ñ too",
              (meaning(&ED_KEYS, (char) 0xF1, VK_TILDE_n, 0).flags & KC_PUTC) != 0, 1);
        check("  and DEL still is not",
              (meaning(&ED_KEYS, 0x7F, VK_DELETE, 0).flags & KC_PUTC) != 0, 0);
    }

    /* --- a program's keys in front of the editor's --- */
    {
        check("its own CTRL+S is its own",
              meaning(&MY_KEYS, 19, VK_s, MOD_CTRL).cmd == my_save, 1);
        check("  and the editor's keys come after it",
              meaning(&MY_KEYS, 3, VK_c, MOD_CTRL).cmd == cmd_copy, 1);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
