/*
 * Host tests for copy, cut and paste.
 *
 * The clipboard is 8KiB of RAM, and a selection larger than that is refused
 * rather than truncated. That is the rule everything here turns on: a cut is a
 * copy followed by a delete, so a copy that quietly kept only part of the range
 * would make cut into a delete wearing the wrong name. The same reasoning
 * applies at the other end -- a paste with no room writes nothing rather than
 * as much as fits.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "clipboard.h"
#include "cmd_ops.h"
#include "editor.h"
#include "text_buffer.h"
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

static void check_s(const char* name, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-52s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s\n        got  \"%s\"\n        want \"%s\"\n",
                name, got, want);
        failures++;
    }
}

static tb_pos at(int line, int x) {
    tb_pos p;
    p.line = line;
    p.x = x;

    return p;
}

/* The document with lines joined by '/', so a whole edit reads as one check. */
static const char* doc_of(text_buffer* tb) {
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_seek(&cp, at(1, 0));

    static char out[2048];
    int n = 0;
    int tpos = tb_ypos(&cp);
    for (;;) {
        int sz = 0;
        char* line = tb_suffix(&cp, &sz);
        if (line != NULL && sz > 0 && n + sz < (int) sizeof(out) - 2) {
            memcpy(out + n, line, (size_t) sz);
            n += sz;
        }
        tb_down(&cp);
        const int npos = tb_ypos(&cp);
        if (npos == tpos) {
            break;
        }
        tpos = npos;
        out[n++] = '/';
    }
    out[n] = 0;

    return out;
}

static const char DOC[] = "one\r\ntwo\r\nthree\r\n";

static editor ed;
static bool started = false;

static void restart(void) {
    if (started) {
        ed_destroy(&ed);
    }
    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    ed_init(&ed, 8, "doc.txt");
    started = true;
}

/* Selects from a to b and leaves the cursor at b, the way shift+motion would. */
static void select_from(tb_pos a, tb_pos b) {
    ed.anchor_ = a;
    ed.selecting_ = true;
    tb_seek(&ed.buf_, b);
}

static key_command press(VKey vkey, char ch, cmd_op cmd) {
    key_command kc;
    kc.cmd = cmd;
    kc.k.key = ch;
    kc.k.vkey = vkey;
    kc.mods = 0;

    return kc;
}

int main(void) {
    stub_discard_output();

    /* --- the clipboard on its own --- */
    clipboard c;
    check("a clipboard", clip_init(&c, 64) != NULL, 1);
    check("  starts empty", clip_has(&c) ? 1 : 0, 0);
    check("  with its capacity", clip_capacity(&c), 64);

    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    text_buffer tb;
    check("a document", tb_init(&tb, 4, "doc.txt") != NULL, 1);

    check("copying a range", clip_copy(&c, &tb, at(1, 0), at(2, 3)) ? 1 : 0, 1);
    check("  holds its bytes", clip_size(&c), 8);       /* "one" CRLF "two" */
    check("  counts its line breaks", clip_lines(&c), 1);
    check("  and has something", clip_has(&c) ? 1 : 0, 1);
    check_s("  leaving the document alone", doc_of(&tb), "one/two/three/");

    check("copying within one line", clip_copy(&c, &tb, at(2, 0), at(2, 2)) ? 1 : 0, 1);
    check("  has no line breaks", clip_lines(&c), 0);
    check("  and replaces what was there", clip_size(&c), 2);

    /* Too large: nothing is kept, and the clipboard says it has nothing rather
     * than leaving the previous copy looking like the new one. */
    /* Sized to hold "one\r\ntwo" but not the whole document, so a copy that
     * fits and one that does not can both be made into it. */
    clipboard tiny;
    clip_init(&tiny, 10);
    check("a range too large is refused",
          clip_copy(&tiny, &tb, at(1, 0), at(3, 5)) ? 1 : 0, 0);
    check("  and it holds nothing", clip_has(&tiny) ? 1 : 0, 0);
    check("  not even part of it", clip_size(&tiny), 0);

    /* A previous copy must not be left standing in its place -- including what
     * was counted about it, since a stale line count would be checked against
     * the line index at paste time and is nobody's idea of what is there. */
    check("a copy that fits, with a line break in it",
          clip_copy(&tiny, &tb, at(1, 0), at(2, 3)) ? 1 : 0, 1);
    check("  which was counted", clip_lines(&tiny), 1);
    check("  then one that does not fit",
          clip_copy(&tiny, &tb, at(1, 0), at(3, 5)) ? 1 : 0, 0);
    check("  leaves nothing to paste", clip_has(&tiny) ? 1 : 0, 0);
    check("  and no count of what is not there", clip_lines(&tiny), 0);
    clip_destroy(&tiny);

    /* An empty range is not a copy. */
    check("an empty range copies nothing",
          clip_copy(&c, &tb, at(2, 2), at(2, 2)) ? 1 : 0, 0);
    check("  and leaves nothing to paste", clip_has(&c) ? 1 : 0, 0);

    /* --- pasting --- */
    check("with nothing copied, paste does nothing",
          clip_paste(&c, &tb) ? 1 : 0, 0);
    check_s("  and the document is untouched", doc_of(&tb), "one/two/three/");

    clip_copy(&c, &tb, at(1, 0), at(2, 3));       /* "one\r\ntwo" */
    tb_seek(&tb, at(3, 5));
    check("pasting puts it in", clip_paste(&c, &tb) ? 1 : 0, 1);
    check_s("  at the cursor", doc_of(&tb), "one/two/threeone/two/");
    check("  leaving the cursor after it", tb_ypos(&tb), 4);

    clip_destroy(&c);
    tb_destroy(&tb);

    /* --- the commands --- */
    restart();
    select_from(at(1, 0), at(1, 3));
    cmd_copy(&ed);
    check("copy takes the selection", clip_size(&ed.clip_), 3);
    check("  and keeps it selected", ed.selecting_ ? 1 : 0, 1);
    check_s("  without changing the document", doc_of(&ed.buf_), "one/two/three/");

    restart();
    select_from(at(1, 0), at(1, 3));
    cmd_cut(&ed);
    check("cut takes the selection too", clip_size(&ed.clip_), 3);
    check_s("  and removes it", doc_of(&ed.buf_), "/two/three/");
    check("  leaving nothing selected", ed.selecting_ ? 1 : 0, 0);
    check("  with the cursor where it was", tb_ypos(&ed.buf_), 1);
    check("  at the start", tb_xpos(&ed.buf_), 1);

    /* Cut across lines joins what is left of them. */
    restart();
    select_from(at(1, 1), at(2, 2));
    cmd_cut(&ed);
    check_s("cut across a line break joins them", doc_of(&ed.buf_), "oo/three/");
    check("  and the clipboard holds the break", clip_lines(&ed.clip_), 1);

    /* Paste puts it back. */
    tb_seek(&ed.buf_, at(2, 5));
    cmd_paste(&ed);
    check_s("paste puts it back", doc_of(&ed.buf_), "oo/threene/tw/");

    /* Pasting with a selection live replaces it, the same way typing does. */
    restart();
    select_from(at(1, 0), at(1, 3));      /* "one" */
    cmd_copy(&ed);
    select_from(at(3, 0), at(3, 5));      /* "three" */
    cmd_paste(&ed);
    check_s("paste replaces the selection", doc_of(&ed.buf_), "one/two/one/");
    check("  and nothing is selected afterwards", ed.selecting_ ? 1 : 0, 0);

    /* Paste with nothing copied does nothing at all. */
    restart();
    check("paste with an empty clipboard does nothing",
          clip_has(&ed.clip_) ? 1 : 0, 0);
    cmd_paste(&ed);
    check_s("  and changes nothing", doc_of(&ed.buf_), "one/two/three/");

    /* Copy with nothing selected is not a command that does something odd. */
    restart();
    ed.selecting_ = false;
    cmd_copy(&ed);
    check("copy with no selection copies nothing",
          clip_has(&ed.clip_) ? 1 : 0, 0);
    cmd_cut(&ed);
    check_s("cut with no selection changes nothing",
            doc_of(&ed.buf_), "one/two/three/");

    /* --- copy, then open another file, then paste --- */
    /* The clipboard belongs to the session, not the document. This is the case
     * the whole feature is for. */
    restart();
    select_from(at(1, 0), at(2, 3));
    cmd_copy(&ed);
    check("copied from the first document", clip_size(&ed.clip_), 8);

    stub_key keys[32];
    int n = 0;
    for (int i = 0; i < 7; i++) {
        keys[n].ch = 0x7F; keys[n].vk = VK_BACKSPACE; keys[n++].mods = 0;
    }
    for (const char* q = "other.txt"; *q; q++) {
        keys[n].ch = *q; keys[n].vk = VK_NONE; keys[n++].mods = 0;
    }
    keys[n].ch = 0; keys[n].vk = VK_RETURN; keys[n++].mods = 0;
    static const char OTHER[] = "zzz\r\n";
    stub_file_reset();
    stub_file_set_content(OTHER, (int) sizeof(OTHER) - 1);
    stub_set_keys(keys, n);
    cmd_open(&ed);
    stub_set_keys(NULL, 0);
    check_s("opened another document", doc_of(&ed.buf_), "zzz/");
    check("  and the clipboard survived", clip_size(&ed.clip_), 8);
    tb_seek(&ed.buf_, at(1, 3));
    cmd_paste(&ed);
    check_s("  so it pastes into the new one", doc_of(&ed.buf_), "zzzone/two/");

    /* --- select all --- */
    /* A document whose last line has text on it: with a trailing newline the
     * last line is empty and stopping at its start looks the same as reaching
     * its end, so that shape proves nothing. */
    static const char NOEOL[] = "alpha\r\nbeta\r\ngamma";
    stub_file_reset();
    stub_file_set_content(NOEOL, (int) sizeof(NOEOL) - 1);
    ed_destroy(&ed);
    ed_init(&ed, 8, "noeol.txt");
    cmd_select_all(&ed);
    check("select all reaches the last line", tb_ypos(&ed.buf_), 3);
    check("  and the end of its text", tb_xpos(&ed.buf_), 6);
    cmd_copy(&ed);
    check("  so the copy is the whole document",
          clip_size(&ed.clip_), (int) sizeof(NOEOL) - 1);

    restart();
    cmd_select_all(&ed);
    check("select all is selecting", ed.selecting_ ? 1 : 0, 1);
    check("  anchored at the very start", ed.anchor_.line, 1);
    check("  at its first byte", ed.anchor_.x, 0);
    check("  with the cursor on the last line", tb_ypos(&ed.buf_), tb_ymax(&ed.buf_));
    cmd_copy(&ed);
    check("  and it copies the whole document",
          clip_size(&ed.clip_), (int) sizeof(DOC) - 1);

    cmd_select_all(&ed);
    cmd_cut(&ed);
    check_s("select all then cut empties it", doc_of(&ed.buf_), "");
    cmd_paste(&ed);
    check_s("  and pasting puts it all back", doc_of(&ed.buf_), "one/two/three/");

    /* --- a cut that cannot be copied is not a cut --- */
    /* The clipboard is smaller than the document can be, so a big enough
     * selection cannot be held. Deleting it anyway would be a delete wearing
     * the name of a cut, and the text would be gone with nothing to paste. */
    static char big[CLIP_SIZE + 2048];
    for (int i = 0; i < (int) sizeof(big); i++) {
        big[i] = 'a' + (i % 26);
    }
    stub_file_reset();
    stub_file_set_content(big, (int) sizeof(big));
    ed_destroy(&ed);
    check("a document larger than the clipboard",
          ed_init(&ed, 32, "big.txt") != NULL, 1);
    check("  which it is", tb_used(&ed.buf_) > clip_capacity(&ed.clip_), 1);

    const int before = tb_used(&ed.buf_);
    cmd_select_all(&ed);
    cmd_cut(&ed);
    check("a cut too large to copy deletes nothing", tb_used(&ed.buf_), before);
    check("  and leaves nothing on the clipboard", clip_has(&ed.clip_) ? 1 : 0, 0);
    /* Copy alone is the same story. */
    cmd_select_all(&ed);
    cmd_copy(&ed);
    check("a copy too large copies nothing", clip_has(&ed.clip_) ? 1 : 0, 0);
    check("  and still deletes nothing", tb_used(&ed.buf_), before);
    started = true;

    /* --- a paste that will not fit must not eat the selection --- */
    /* Deleting the selection first frees room, so the check has to allow for
     * that -- but a paste bigger than the selection it replaces still will not
     * fit, and finding out after the deletion leaves the text gone with nothing
     * in its place and no undo to get it back. */
    static char half[4500];
    for (int i = 0; i < (int) sizeof(half); i++) {
        half[i] = 'a' + (i % 26);
    }
    stub_file_reset();
    stub_file_set_content(half, (int) sizeof(half));
    ed_destroy(&ed);
    check("a document filling most of the buffer",
          ed_init(&ed, 8, "half.txt") != NULL, 1);
    started = true;

    cmd_select_all(&ed);
    cmd_copy(&ed);
    check("  copied whole", clip_size(&ed.clip_), (int) sizeof(half));
    /* Pasting it again needs as much room as the document already takes, and
     * there is not that much left. */
    check("  and there is not room for it twice",
          tb_available(&ed.buf_) < clip_size(&ed.clip_), 1);

    /* Select a few characters and paste over them. The selection is far smaller
     * than the clipboard, so the paste cannot fit even once they are gone. */
    select_from(at(1, 0), at(1, 10));
    const int used_before = tb_used(&ed.buf_);
    cmd_paste(&ed);
    check("a paste with no room changes nothing", tb_used(&ed.buf_), used_before);
    check("  and the selection is still there", ed.selecting_ ? 1 : 0, 1);
    check("  with its anchor intact", ed.anchor_.x, 0);

    /* One that does fit still goes in, so the check is not simply refusing. */
    clipboard small;
    clip_init(&small, 64);
    clip_copy(&small, &ed.buf_, at(1, 0), at(1, 4));
    clip_destroy(&ed.clip_);
    ed.clip_ = small;
    select_from(at(1, 0), at(1, 10));
    cmd_paste(&ed);
    check("a paste that fits replaces the selection",
          tb_used(&ed.buf_), used_before - 10 + 4);

    /* ...and one that fits only *because* the selection is going. The document
     * is filled to within a few bytes of the buffer, so the paste has nowhere
     * to go until the selection it replaces is counted as room. */
    static char nearly[7900];
    for (int i = 0; i < (int) sizeof(nearly); i++) {
        nearly[i] = 'a' + (i % 26);
    }
    stub_file_reset();
    stub_file_set_content(nearly, (int) sizeof(nearly));
    ed_destroy(&ed);
    check("a nearly full document", ed_init(&ed, 8, "full.txt") != NULL, 1);
    started = true;

    clipboard fifty;
    clip_init(&fifty, 128);
    clip_copy(&fifty, &ed.buf_, at(1, 0), at(1, 50));
    clip_destroy(&ed.clip_);
    ed.clip_ = fifty;
    check("  with less room left than the clipboard holds",
          tb_available(&ed.buf_) < clip_size(&ed.clip_), 1);

    const int full_before = tb_used(&ed.buf_);
    select_from(at(1, 0), at(1, 100));
    cmd_paste(&ed);
    check("a paste fits when the selection it replaces makes room",
          tb_used(&ed.buf_), full_before - 100 + 50);

    /* --- the line index is budgeted too, not just the characters --- */
    /* A document of short lines fills the line index long before the
     * characters, so a paste can have room for its bytes and nowhere to put
     * its line breaks. */
    static char shortlines[4096];
    int sn = 0;
    for (int i = 0; i < 200; i++) {
        shortlines[sn++] = 'a';
        shortlines[sn++] = '\r';
        shortlines[sn++] = '\n';
    }
    stub_file_reset();
    stub_file_set_content(shortlines, sn);
    ed_destroy(&ed);
    check("a document of short lines", ed_init(&ed, 8, "lines.txt") != NULL, 1);
    started = true;
    ed.selecting_ = false;

    const int slots = lb_avai(&ed.buf_.lb_);
    check("  with the line index nearly full", slots > 1 && slots < 60, 1);
    check("  and plenty of characters left",
          tb_available(&ed.buf_) > slots * 8, 1);

    /* With a selection live, the line budget is the one that decides it, and
     * asking after the deletion is too late: tb_insert would refuse, but the
     * selection would already be gone. Three more breaks than the index can
     * take even once the selection has given its slots back. */
    clipboard breaks;
    clip_init(&breaks, 2048);
    clip_copy(&breaks, &ed.buf_, at(1, 0), at(1 + slots + 3, 0));
    clip_destroy(&ed.clip_);
    ed.clip_ = breaks;
    check("a clipboard with more breaks than the index can take",
          clip_lines(&ed.clip_), slots + 3);

    const int lines_before = tb_used(&ed.buf_);
    select_from(at(1, 0), at(4, 0));      /* three lines, so three slots freed */
    cmd_paste(&ed);
    check("a paste with nowhere to put its lines changes nothing",
          tb_used(&ed.buf_), lines_before);
    check("  and does not eat the selection first", ed.selecting_ ? 1 : 0, 1);

    /* And the slots the selection gives back do count: this one fits only
     * because they do. As many breaks as there are free slots, replacing a
     * selection that spans one line, needs exactly the spare that frees. */
    clipboard justfits;
    clip_init(&justfits, 2048);
    clip_copy(&justfits, &ed.buf_, at(1, 0), at(1 + slots, 0));
    clip_destroy(&ed.clip_);
    ed.clip_ = justfits;
    check("a clipboard with exactly as many breaks as slots",
          clip_lines(&ed.clip_), slots);
    select_from(at(1, 0), at(2, 0));      /* one line, one slot back */
    cmd_paste(&ed);
    check("  fits once the selection gives its slot back",
          tb_used(&ed.buf_) > lines_before, 1);

    /* --- typing over a selection replaces it --- */
    check("a printable key edits",
          ed_key_edits(press(VK_a, 'a', CMD_PUTC)) ? 1 : 0, 1);
    check("RETURN edits", ed_key_edits(press(VK_RETURN, 0, NULL)) ? 1 : 0, 1);
    check("TAB edits", ed_key_edits(press(VK_TAB, '\t', NULL)) ? 1 : 0, 1);
    check("BACKSPACE edits", ed_key_edits(press(VK_BACKSPACE, 0, NULL)) ? 1 : 0, 1);
    check("DELETE edits", ed_key_edits(press(VK_DELETE, 0, NULL)) ? 1 : 0, 1);
    check("an arrow does not", ed_key_edits(press(VK_RIGHT, 0, NULL)) ? 1 : 0, 0);
    check("nor does HOME", ed_key_edits(press(VK_HOME, 0, NULL)) ? 1 : 0, 0);

    restart();
    select_from(at(1, 0), at(1, 3));
    check("a printable key replaces the selection",
          ed_selection_for(&ed, press(VK_a, 'a', CMD_PUTC)), SEL_REPLACE);
    check("  and the selection is still there to delete", ed.selecting_ ? 1 : 0, 1);
    cmd_delete_selection(&ed);
    check_s("  which deleting removes", doc_of(&ed.buf_), "/two/three/");
    check("  and clears", ed.selecting_ ? 1 : 0, 0);

    /* The commands that act on the selection do not end it. */
    restart();
    select_from(at(1, 0), at(1, 3));
    check("copy does not end the selection",
          ed_selection_for(&ed, press(VK_c, 0, cmd_copy)), SEL_NONE);
    check("  still selecting", ed.selecting_ ? 1 : 0, 1);
    check("cut does not either",
          ed_selection_for(&ed, press(VK_x, 0, cmd_cut)), SEL_NONE);
    check("paste does not either",
          ed_selection_for(&ed, press(VK_v, 0, cmd_paste)), SEL_NONE);
    check("nor does select all",
          ed_selection_for(&ed, press(VK_a, 0, cmd_select_all)), SEL_NONE);
    check("  and the selection is still live", ed.selecting_ ? 1 : 0, 1);

    /* --- the bindings --- */
    key_command kc;
    memset(&kc, 0, sizeof(kc));
    kc.k.vkey = VK_c;
    check("CTRL+C copies", ctrlCmds(kc, 0).cmd == cmd_copy, 1);
    check("CTRL+ALT+C is still the colour picker",
          ctrlCmds(kc, MOD_ALT).cmd == cmd_color_picker, 1);
    kc.k.vkey = VK_x;
    check("CTRL+X cuts", ctrlCmds(kc, 0).cmd == cmd_cut, 1);
    kc.k.vkey = VK_v;
    check("CTRL+V pastes", ctrlCmds(kc, 0).cmd == cmd_paste, 1);
    kc.k.vkey = VK_a;
    check("CTRL+A selects everything", ctrlCmds(kc, 0).cmd == cmd_select_all, 1);
    /* And none of them took a key something else was using. */
    kc.k.vkey = VK_s;
    check("CTRL+S still saves", ctrlCmds(kc, 0).cmd == CMD_SAVE, 1);
    kc.k.vkey = VK_o;
    check("CTRL+O still opens", ctrlCmds(kc, 0).cmd == cmd_open, 1);
    kc.k.vkey = VK_q;
    check("CTRL+Q still quits", ctrlCmds(kc, 0).cmd == CMD_QUIT, 1);

    ed_destroy(&ed);
    started = false;

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
