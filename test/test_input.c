/*
 * Host tests for where every keystroke enters AED.
 *
 * The bug these exist for: AED read keys with getch(), which returns typed
 * *characters*. CTRL+SHIFT+<arrow> does not produce one, so the editor sat
 * blocked in getch() while MOS reported the chord perfectly well -- and
 * word-wise selection, the whole point of that chord, did nothing.
 *
 * So the translation is worth pinning directly. It has three jobs: block until
 * a key goes *down*, ignoring releases; carry the modifiers that came with that
 * key rather than whatever is held by the time they are read; and decide what
 * the key means from its virtual key code, not from an ASCII code that several
 * keys do not have at all.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "cmd_ops.h"
#include "editor.h"
#include "keys.h"
#include "vkey.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* Virtual key codes as MOS reports them, measured on VDP 2.16.0 rather than
 * counted out of vkey.h by hand. The enum agrees; this is the evidence. */
#define VKC_RIGHT  156
#define VKC_LEFT   154
#define VKC_HOME   133
#define VKC_PGUP   146
#define VKC_LSHIFT 117
#define VKC_LCTRL  121

/* The ASCII that comes with each, which is the trap: HOME and PAGE UP have
 * none, and RIGHT reports the same 21 whether or not modifiers are held. */
#define ASC_RIGHT  21
#define ASC_LEFT    8
#define ASC_NONE    0

static key_command one(stub_key k) {
    static stub_key script[1];
    script[0] = k;
    stub_set_keys(script, 1);

    return read_input();
}

int main(void) {
    stub_discard_output();

    /* The chord that did not work. It has to arrive at all, name the word-wise
     * command, and still carry shift -- because the command is the same one
     * CTRL+RIGHT runs, and shift is the only thing that says to select. */
    {
        const stub_key chord = { ASC_RIGHT, VKC_RIGHT, MOD_CTRL | MOD_SHFT, 0 };
        const key_command kc = one(chord);
        check("CTRL+SHIFT+RIGHT -> cmd_w_right", kc.cmd == cmd_w_right, 1);
        check("CTRL+SHIFT+RIGHT keeps shift", (kc.mods & MOD_SHFT) != 0, 1);
        check("CTRL+SHIFT+RIGHT keeps ctrl", (kc.mods & MOD_CTRL) != 0, 1);
        check("CTRL+SHIFT+RIGHT reports the vkey", kc.k.vkey == VK_RIGHT, 1);

        const stub_key left = { ASC_LEFT, VKC_LEFT, MOD_CTRL | MOD_SHFT, 0 };
        const key_command lc = one(left);
        check("CTRL+SHIFT+LEFT -> cmd_w_left", lc.cmd == cmd_w_left, 1);
        check("CTRL+SHIFT+LEFT keeps shift", (lc.mods & MOD_SHFT) != 0, 1);
    }

    /* And that it is genuinely a selection, not just a word jump: the editor
     * has to see shift with a motion key and start one. */
    {
        editor ed;
        memset(&ed, 0, sizeof(ed));
        ed.selecting_ = false;

        const stub_key chord = { ASC_RIGHT, VKC_RIGHT, MOD_CTRL | MOD_SHFT, 0 };
        const key_command kc = one(chord);
        check("CTRL+SHIFT+RIGHT extends a selection",
              ed_selection_for(&ed, kc) == SEL_EXTEND, 1);
        check("...and leaves the editor selecting", ed.selecting_, 1);
    }

    /* The same key without shift is the same command and no selection. */
    {
        const stub_key ctrl = { ASC_RIGHT, VKC_RIGHT, MOD_CTRL, 0 };
        const key_command kc = one(ctrl);
        check("CTRL+RIGHT -> cmd_w_right", kc.cmd == cmd_w_right, 1);
        check("CTRL+RIGHT has no shift", (kc.mods & MOD_SHFT) != 0, 0);
    }

    /* Plain and shifted motion, which used to work and must keep working. */
    {
        const stub_key plain = { ASC_RIGHT, VKC_RIGHT, 0, 0 };
        check("RIGHT -> cmd_right", one(plain).cmd == cmd_right, 1);

        const stub_key shifted = { ASC_RIGHT, VKC_RIGHT, MOD_SHFT, 0 };
        const key_command kc = one(shifted);
        check("SHIFT+RIGHT -> cmd_right", kc.cmd == cmd_right, 1);
        check("SHIFT+RIGHT keeps shift", (kc.mods & MOD_SHFT) != 0, 1);
    }

    /* Keys that produce no ASCII at all. Dispatching on the character would
     * drop these, since 0 is not a printable and not a motion either. */
    {
        const stub_key home = { ASC_NONE, VKC_HOME, 0, 0 };
        check("HOME has no ASCII but still commands", one(home).cmd == cmd_home, 1);

        const stub_key pgup = { ASC_NONE, VKC_PGUP, 0, 0 };
        check("PAGE UP has no ASCII but still commands",
              one(pgup).cmd == cmd_page_up, 1);

        const stub_key shome = { ASC_NONE, VKC_HOME, MOD_SHFT, 0 };
        const key_command kc = one(shome);
        check("SHIFT+HOME -> cmd_home", kc.cmd == cmd_home, 1);
        check("SHIFT+HOME keeps shift", (kc.mods & MOD_SHFT) != 0, 1);
    }

    /* A printable is a printable. */
    {
        const stub_key a = { 'a', 22 /* VK_a */, 0, 0 };
        const key_command kc = one(a);
        check("'a' -> CMD_PUTC", kc.cmd == CMD_PUTC, 1);
        check("'a' carries the character", kc.k.key, 'a');
    }

    /* Releases are not keystrokes. MOS reports one for every key, so passing
     * them through would run every command twice -- and the release of shift
     * would arrive as a bare keypress and cancel the selection it just made. */
    {
        static const stub_key script[] = {
            { 'a',       22,         0,                  1 },  /* release */
            { ASC_RIGHT, VKC_LSHIFT, MOD_CTRL,           1 },  /* release */
            { ASC_RIGHT, VKC_RIGHT,  MOD_CTRL | MOD_SHFT, 0 }, /* press   */
        };
        stub_set_keys(script, 3);
        const key_command kc = read_input();
        check("releases are skipped", kc.cmd == cmd_w_right, 1);
        check("...all of them", stub_keys_read(), 3);
    }

    /* Pressing shift is its own event, before the key it modifies -- and the
     * editor has no use for it. What the chord means arrives in the modifier
     * bits of the arrow or the letter. So the wait swallows them: waking the
     * editor for one costs a turn of the loop, and it is not reading the queue
     * while it takes one. */
    {
        static const stub_key script[] = {
            { ASC_NONE, VKC_LCTRL,  0,        0 },   /* CTRL down  */
            { ASC_NONE, VKC_LSHIFT, MOD_CTRL, 0 },   /* SHIFT down */
            { ASC_RIGHT, VKC_RIGHT, MOD_CTRL | MOD_SHFT, 0 },
        };
        stub_set_keys(script, 3);
        const key_command kc = read_input();
        check("the modifiers themselves never reach the editor",
              kc.cmd == cmd_w_right, 1);
        check("...and all three were taken from the queue",
              stub_keys_read(), 3);
        check("...with the chord's modifiers intact", kc.mods,
              MOD_CTRL | MOD_SHFT);
    }

    /* The predicate is still what the selection machinery asks with, and still
     * has to be right: it is what stops a stray modifier ending a selection. */
    {
        check("SHIFT is a modifier", ed_is_modifier(VK_LSHIFT), 1);
        check("CTRL is a modifier", ed_is_modifier(VK_LCTRL), 1);
        check("an arrow is not", ed_is_modifier(VK_RIGHT), 0);
    }

    /* Drawing the footer between keystrokes is what stops the next one
     * arriving, and a held chord is when that matters: it is the one thing
     * CTRL+SHIFT with an arrow is for. So it is left alone while both are
     * down, and drawn for everything else. */
    {
        check("no modifiers: draw it", ed_footer_wanted(0), 1);
        check("CTRL alone: draw it", ed_footer_wanted(MOD_CTRL), 1);
        check("SHIFT alone: draw it", ed_footer_wanted(MOD_SHFT), 1);
        check("ALT alone: draw it", ed_footer_wanted(MOD_ALT), 1);
        check("CTRL+SHIFT: leave it", ed_footer_wanted(MOD_CTRL | MOD_SHFT), 0);
        check("...with ALT too: leave it",
              ed_footer_wanted(MOD_CTRL | MOD_SHFT | MOD_ALT), 0);
    }

    /* The MOS key vector outlives the process if AED does not take it back
     * down, so closing has to be part of shutting down rather than something
     * the exit path is trusted to remember. */
    {
        stub_set_keys(NULL, 0);
        check("no vector before ed_init", stub_keys_installed(), 0);

        editor ed;
        if (ed_init(&ed, 8, NULL) == NULL) {
            check("ed_init succeeded", 0, 1);
        } else {
            check("ed_init installs the key vector", stub_keys_installed(), 1);
            ed_destroy(&ed);
            check("ed_destroy removes it", stub_keys_installed(), 0);
        }
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
