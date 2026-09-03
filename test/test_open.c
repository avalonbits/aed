/*
 * Host tests for opening a file over an existing document (CTRL+O).
 *
 * The property that matters most here is that a failed open changes nothing.
 * tb_open replaces the document in place -- there is no room on an Agon for a
 * second buffer to load into and swap -- so the only thing standing between a
 * mistyped name and the user's work is that every failure it can report is
 * detected *before* the buffer is cleared. These tests check the current
 * document is still there after each one.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "text_buffer.h"
#include "editor.h"
#include "cmd_ops.h"
#include "user_input.h"
#include "vkey.h"

static FILE* cap;
static long mark;

/* The VDU stream the editor writes goes to stdout (see test/stubs). Reading it
 * back is how a test can tell whether something was drawn. */
static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

static const char* cap_read(void) {
    fflush(stdout);
    const long end = ftell(stdout);
    static char buf[8192];
    long n = end - mark;
    if (n < 0 || n > (long) sizeof(buf) - 1) {
        n = sizeof(buf) - 1;
    }
    fseek(stdout, mark, SEEK_SET);
    size_t got = fread(buf, 1, (size_t) n, stdout);
    buf[got] = 0;
    fseek(stdout, end, SEEK_SET);

    return buf;
}

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* The document as one string, lines joined with '\n', so a whole-buffer
 * comparison reads as one check rather than a pile of line assertions.
 * tb_prefix/tb_suffix are line-scoped, so this walks the lines the way the
 * repaint path does. A trailing CRLF makes a final empty line, which shows up
 * here as a trailing '\n' of its own. */
static const char* doc_of(text_buffer* tb) {
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    while (tb_ypos(&cp) > 1) {
        tb_up(&cp);
    }

    static char got[4096];
    int n = 0;
    int tpos = tb_ypos(&cp);
    for (;;) {
        int sz = 0;
        char* line = tb_suffix(&cp, &sz);
        if (line != NULL && sz > 0 && n + sz < (int) sizeof(got) - 2) {
            memcpy(got + n, line, (size_t) sz);
            n += sz;
        }
        got[n++] = '\n';

        tb_down(&cp);
        const int npos = tb_ypos(&cp);
        if (npos == tpos) {
            break;
        }
        tpos = npos;
    }
    got[n] = 0;

    return got;
}

static int failed_doc = 0;

static int doc_is(text_buffer* tb, const char* want) {
    const char* got = doc_of(tb);
    if (strcmp(got, want) == 0) {
        return 1;
    }
    fprintf(stderr, "      document is %s, want %s\n", got, want);
    failed_doc++;

    return 0;
}

int main(void) {
    stub_discard_output();

    /* --- tb_clear --- */
    stub_file_reset();
    static const char first[] = "alpha\r\nbeta\r\n";
    stub_file_set_content(first, (int) sizeof(first) - 1);
    text_buffer tb;
    check("a document loads", tb_init(&tb, 4, "first.txt") != NULL, 1);
    check("  with its contents", doc_is(&tb, "alpha\nbeta\n\n"), 1);
    const int total = tb_size(&tb);

    tb_put(&tb, 'x');
    check("typing makes it dirty", tb_changed(&tb) ? 1 : 0, 1);
    tb_clear(&tb);
    check("clear empties the buffer", tb_used(&tb), 0);
    /* The line index as well as the characters: lb_cinc counts a line up from
     * whatever is in the slot, so a leftover length is not overwritten by the
     * next document, it is added to. */
    check("  and the line index with it", tb_ymax(&tb), 1);
    check("  gives back the whole buffer", tb_available(&tb), total);
    check("  puts the cursor at the start", tb_xpos(&tb), 1);
    check("  and it is not dirty", tb_changed(&tb) ? 1 : 0, 0);
    check("  but keeps the file name", strcmp(tb_fname(&tb), "first.txt"), 0);
    tb_destroy(&tb);

    /* --- tb_open replaces the document --- */
    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    check("open: a document to replace", tb_init(&tb, 4, "first.txt") != NULL, 1);

    stub_file_reset();
    static const char second[] = "gamma\r\ndelta\r\nepsilon\r\n";
    stub_file_set_content(second, (int) sizeof(second) - 1);
    check("open succeeds", tb_open(&tb, "second.txt", 10), TB_OK);
    check("  the new document is there", doc_is(&tb, "gamma\ndelta\nepsilon\n\n"), 1);
    check("  and the old one is not", tb_used(&tb), (int) sizeof(second) - 1);
    check("  the name changed", strcmp(tb_fname(&tb), "second.txt"), 0);
    check("  the cursor is at the top", tb_ypos(&tb), 1);
    check("  and at the start of the line", tb_xpos(&tb), 1);
    check("  a freshly opened file is not dirty", tb_changed(&tb) ? 1 : 0, 0);
    check("  and the lines are counted from scratch", tb_ymax(&tb), 4);

    /* A short name must not carry any of the longer one it replaced. */
    stub_file_reset();
    stub_file_set_content(second, (int) sizeof(second) - 1);
    check("open a shorter name", tb_open(&tb, "ab.txt", 6), TB_OK);
    check("  the name is exactly that", strcmp(tb_fname(&tb), "ab.txt"), 0);

    /* --- a failed open leaves the document alone --- */
    /* This is the whole safety argument for replacing in place. */
    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    check("open: back to a known document", tb_open(&tb, "first.txt", 9), TB_OK);
    check("  which is loaded", doc_is(&tb, "alpha\nbeta\n\n"), 1);

    /* Too large: bigger than the whole buffer, not merely bigger than what is
     * free. The check has to be made against the empty buffer, since the
     * document about to be discarded is not in the way. */
    static char huge[200 * 1024];
    memset(huge, 'z', sizeof(huge));
    stub_file_reset();
    stub_file_set_content(huge, (int) sizeof(huge));
    check("an oversized file is refused", tb_open(&tb, "huge.txt", 8), TB_TOO_LARGE);
    check("  the document is untouched", doc_is(&tb, "alpha\nbeta\n\n"), 1);
    check("  and so is its name", strcmp(tb_fname(&tb), "first.txt"), 0);

    /* The check has to be against the whole buffer, not the free space: the
     * common case is opening a file about the size of the one already loaded,
     * and measuring against what is free would refuse it for no reason. */
    static char bulky[3000];
    memset(bulky, 'a', sizeof(bulky));
    stub_file_reset();
    stub_file_set_content(bulky, (int) sizeof(bulky));
    check("a document filling most of the buffer", tb_open(&tb, "big.txt", 7), TB_OK);
    check("  leaves little free", tb_available(&tb) < 1000, 1);

    static char bigger[3500];
    memset(bigger, 'b', sizeof(bigger));
    stub_file_reset();
    stub_file_set_content(bigger, (int) sizeof(bigger));
    check("a file that fits the buffer but not the free space still opens",
          tb_open(&tb, "big2.txt", 8), TB_OK);
    check("  and it is the one that got loaded", tb_used(&tb), (int) sizeof(bigger));

    /* Back to something small and known for the failure cases below. */
    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    check("reopen the small document", tb_open(&tb, "first.txt", 9), TB_OK);

    /* Unopenable, and uncreatable. */
    stub_file_reset();
    stub_file_fail_open(1);
    check("an unopenable file is refused", tb_open(&tb, "nope.txt", 8), TB_NO_FILE);
    check("  the document is untouched", doc_is(&tb, "alpha\nbeta\n\n"), 1);
    check("  and so is its name", strcmp(tb_fname(&tb), "first.txt"), 0);

    check("an empty name is refused", tb_open(&tb, "", 0), TB_NO_FILE);
    check("a NULL name is refused", tb_open(&tb, NULL, 4), TB_NO_FILE);
    check("  and the document survived both", doc_is(&tb, "alpha\nbeta\n\n"), 1);

    /* A card that stops part way through the read. The bytes that never arrived
     * are still whatever was in that memory; indexing them as document text and
     * handing them back as the file's contents is worse than saying the read
     * failed, so the open is refused and the buffer is left empty rather than
     * full of nonsense. */
    stub_file_reset();
    stub_file_set_content(second, (int) sizeof(second) - 1);
    stub_file_short_read(6);
    check("a short read is refused", tb_open(&tb, "part.txt", 8), TB_NO_FILE);
    check("  and nothing of it is in the buffer", tb_used(&tb), 0);
    check("  with the whole buffer back", tb_available(&tb), tb_size(&tb));

    /* The same at startup, where it would otherwise build a document out of
     * uninitialised memory and never say a word. */
    stub_file_reset();
    stub_file_set_content(second, (int) sizeof(second) - 1);
    stub_file_short_read(6);
    text_buffer partial;
    check("a short read stops the editor starting",
          tb_init(&partial, 4, "part.txt") == NULL, 1);

    /* A size that does not fit the eZ80's 24-bit int. objsize is 32 bits wide,
     * so narrowing it before the comparison turns a huge file into a small or
     * negative number that walks straight past the guard -- and by then the
     * document has been cleared. 0xFFFFFFFF narrows to -1, which is less than
     * any capacity. */
    /* Back to a known document, since the short read above emptied the buffer. */
    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    check("reload after the short read", tb_open(&tb, "first.txt", 9), TB_OK);

    stub_file_reset();
    stub_file_set_content(second, (int) sizeof(second) - 1);
    stub_file_set_objsize(0xFFFFFFFFu);
    check("a size too wide for an int is still too large",
          tb_open(&tb, "vast.txt", 8), TB_TOO_LARGE);
    check("  and the document is untouched", doc_is(&tb, "alpha\nbeta\n\n"), 1);

    stub_file_reset();
    stub_file_set_content(second, (int) sizeof(second) - 1);
    stub_file_set_objsize(0xFFFFFFFFu);
    text_buffer vast;
    check("  and it stops the editor starting too",
          tb_init(&vast, 4, "vast.txt") == NULL, 1);

    /* A file that does not exist yet is created, which is what naming a missing
     * file on the command line does. So CTRL+O is also how a new file starts. */
    stub_file_reset();
    check("a missing file is created, not refused",
          tb_open(&tb, "new.txt", 7), TB_OK);
    check("  and the document is empty", tb_used(&tb), 0);
    check("  under the new name", strcmp(tb_fname(&tb), "new.txt"), 0);
    tb_destroy(&tb);

    const char* drawn;

    /* --- cmd_open, driven through the prompt --- */
    cap = freopen("/tmp/aed_open_capture", "w+", stdout);
    if (cap == NULL) {
        fprintf(stderr, "could not capture stdout\n");

        return 1;
    }

    /* "second.txt" typed into the prompt, then RETURN. The prefill is deleted
     * first: ui_text starts with the current name already in the field. */
    stub_key keys[64];
    int n = 0;
    for (int i = 0; i < 9; i++) {          /* backspace over "first.txt" */
        keys[n].ch = 0x7F; keys[n++].vk = VK_BACKSPACE;
    }
    const char* typed = "second.txt";
    for (const char* p = typed; *p; p++) {
        keys[n].ch = *p; keys[n++].vk = VK_NONE;
    }
    keys[n].ch = 0; keys[n++].vk = VK_RETURN;

    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    editor ed;
    check("editor starts", ed_init(&ed, 8, "first.txt") != NULL, 1);
    check("  with the first document", doc_is(&ed.buf_, "alpha\nbeta\n\n"), 1);

    stub_file_reset();
    stub_file_set_content(second, (int) sizeof(second) - 1);
    stub_set_keys(keys, n);
    cmd_open(&ed);
    check("cmd_open loaded the named file",
          doc_is(&ed.buf_, "gamma\ndelta\nepsilon\n\n"), 1);
    check("  and renamed the buffer", strcmp(tb_fname(&ed.buf_), "second.txt"), 0);
    /* The view has to be wound back too: a long line scrolled right, then a
     * short file opened, would otherwise paint from a column that no longer
     * exists in any line. */
    check("  the view is back at the left", ed.scr_.originX_, 0);
    check("  and at the top", ed.scr_.currY_, ed.scr_.topY_);

    /* Escaping the prompt changes nothing. */
    stub_key esc[] = { { 0, VK_ESCAPE, 0 } };
    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    stub_set_keys(esc, 1);
    cap_start();
    cmd_open(&ed);
    drawn = cap_read();
    check("cancelling the prompt keeps the document",
          doc_is(&ed.buf_, "gamma\ndelta\nepsilon\n\n"), 1);
    check("  and the name", strcmp(tb_fname(&ed.buf_), "second.txt"), 0);
    /* Changing your mind is not an error. Handing the empty result straight to
     * tb_open would refuse it and complain, which is a message the user did
     * nothing to deserve. */
    check("  and says nothing about it",
          strstr(drawn, "Cannot open") == NULL, 1);

    /* An unsaved document asks first, and answering ESC calls it all off --
     * the file must not be opened and the work must still be there. */
    tb_put(&ed.buf_, 'Z');
    check("the document is now dirty", tb_changed(&ed.buf_) ? 1 : 0, 1);
    /* ESC, and then a perfectly good file name behind it. Answering the save
     * prompt with ESC has to abandon the whole command: falling through to the
     * file prompt would open that name and throw the unsaved work away, which
     * is the opposite of what ESC was pressed for. */
    stub_key cancel_save[32];
    n = 0;
    cancel_save[n].ch = 0; cancel_save[n++].vk = VK_ESCAPE;
    for (int i = 0; i < 10; i++) {
        cancel_save[n].ch = 0x7F; cancel_save[n++].vk = VK_BACKSPACE;
    }
    for (const char* q = "first.txt"; *q; q++) {
        cancel_save[n].ch = *q; cancel_save[n++].vk = VK_NONE;
    }
    cancel_save[n].ch = 0; cancel_save[n++].vk = VK_RETURN;

    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    stub_set_keys(cancel_save, n);
    cmd_open(&ed);
    check("cancelling the save prompt keeps the work",
          doc_is(&ed.buf_, "Zgamma\ndelta\nepsilon\n\n"), 1);
    check("  and opens nothing", strcmp(tb_fname(&ed.buf_), "second.txt"), 0);
    check("  leaving it still unsaved", tb_changed(&ed.buf_) ? 1 : 0, 1);

    /* Answering "yes" saves first, then opens. The save is the point: without
     * the prompt at all, the 'y' would just be typed into the file name. */
    stub_key yes_save[64];
    n = 0;
    yes_save[n].ch = 'y'; yes_save[n++].vk = VK_y;
    for (int i = 0; i < 10; i++) {         /* backspace over "second.txt" */
        yes_save[n].ch = 0x7F; yes_save[n++].vk = VK_BACKSPACE;
    }
    for (const char* q = "first.txt"; *q; q++) {
        yes_save[n].ch = *q; yes_save[n++].vk = VK_NONE;
    }
    yes_save[n].ch = 0; yes_save[n++].vk = VK_RETURN;

    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    stub_set_keys(yes_save, n);
    cmd_open(&ed);
    check("answering yes wrote the document out", stub_file_size() > 0, 1);
    check("  and what it wrote is the document that was on screen",
          strncmp(stub_file_bytes(), "Zgamma", 6), 0);
    check("  then opened the named file", doc_is(&ed.buf_, "alpha\nbeta\n\n"), 1);

    /* Dirty it again for the "no" case below. */
    tb_put(&ed.buf_, 'Q');
    check("dirty once more", tb_changed(&ed.buf_) ? 1 : 0, 1);

    /* Answering "no" discards the changes and goes ahead with the open. */
    stub_key no_save[64];
    n = 0;
    no_save[n].ch = 'n'; no_save[n++].vk = VK_n;
    for (int i = 0; i < 9; i++) {          /* backspace over "first.txt" */
        no_save[n].ch = 0x7F; no_save[n++].vk = VK_BACKSPACE;
    }
    for (const char* q = "second.txt"; *q; q++) {
        no_save[n].ch = *q; no_save[n++].vk = VK_NONE;
    }
    no_save[n].ch = 0; no_save[n++].vk = VK_RETURN;

    stub_file_reset();
    stub_file_set_content(second, (int) sizeof(second) - 1);
    stub_set_keys(no_save, n);
    cmd_open(&ed);
    check("answering no discards and opens",
          doc_is(&ed.buf_, "gamma\ndelta\nepsilon\n\n"), 1);
    check("  under the new name", strcmp(tb_fname(&ed.buf_), "second.txt"), 0);
    check("  and it is clean again", tb_changed(&ed.buf_) ? 1 : 0, 0);
    check("  having written nothing out", stub_file_size(), 0);

    /* --- the view is wound back, not just the document --- */
    /* A long line scrolled right, then a short file opened: the horizontal
     * origin has to go with the old document, or the new one is painted from a
     * column none of its lines reach. */
    static char longline[300];
    memset(longline, 'w', sizeof(longline) - 3);
    longline[sizeof(longline) - 3] = '\r';
    longline[sizeof(longline) - 2] = '\n';
    longline[sizeof(longline) - 1] = 0;
    stub_file_reset();
    stub_file_set_content(longline, (int) sizeof(longline) - 1);
    stub_set_keys(NULL, 0);
    editor wide;
    check("editor starts on a long line", ed_init(&wide, 8, "wide.txt") != NULL, 1);
    cmd_end(&wide);
    check("  and the end of it is off screen", wide.scr_.originX_ > 0, 1);

    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    stub_key openit[32];
    n = 0;
    for (int i = 0; i < 9; i++) {          /* backspace over "wide.txt" is 8 */
        openit[n].ch = 0x7F; openit[n++].vk = VK_BACKSPACE;
    }
    for (const char* q = "first.txt"; *q; q++) {
        openit[n].ch = *q; openit[n++].vk = VK_NONE;
    }
    openit[n].ch = 0; openit[n++].vk = VK_RETURN;
    stub_set_keys(openit, n);
    cmd_open(&wide);
    check("opening a short file loaded it", doc_is(&wide.buf_, "alpha\nbeta\n\n"), 1);
    check("  and wound the horizontal scroll back", wide.scr_.originX_, 0);
    check("  and put the cursor at the top", wide.scr_.currY_, wide.scr_.topY_);
    check("  at the left", wide.scr_.currX_, 0);
    ed_destroy(&wide);

    /* --- a refused open says why, and changes nothing --- */
    stub_key namekeys[32];
    n = 0;
    for (int i = 0; i < 10; i++) {
        namekeys[n].ch = 0x7F; namekeys[n++].vk = VK_BACKSPACE;
    }
    for (const char* q = "huge.txt"; *q; q++) {
        namekeys[n].ch = *q; namekeys[n++].vk = VK_NONE;
    }
    namekeys[n].ch = 0; namekeys[n++].vk = VK_RETURN;

    stub_file_reset();
    stub_file_set_content(huge, (int) sizeof(huge));
    stub_set_keys(namekeys, n);
    cap_start();
    cmd_open(&ed);
    drawn = cap_read();
    check("a file too large to open leaves the document",
          doc_is(&ed.buf_, "gamma\ndelta\nepsilon\n\n"), 1);
    check("  and the name", strcmp(tb_fname(&ed.buf_), "second.txt"), 0);
    /* Silence here would look like the key had not registered. */
    check("  and says why", strstr(drawn, "File too large") != NULL, 1);

    stub_file_reset();
    stub_file_fail_open(1);
    stub_set_keys(namekeys, n);
    cap_start();
    cmd_open(&ed);
    drawn = cap_read();
    check("a file that will not open leaves the document",
          doc_is(&ed.buf_, "gamma\ndelta\nepsilon\n\n"), 1);
    check("  and says so", strstr(drawn, "Cannot open file") != NULL, 1);

    /* --- a save that could not happen stops the open --- */
    /* An unnamed buffer makes cmd_save ask for a name; cancelling that means
     * the document was never written, so opening over it would lose it. */
    stub_file_reset();
    stub_set_keys(NULL, 0);
    editor un;
    check("editor starts unnamed", ed_init(&un, 8, NULL) != NULL, 1);
    tb_put(&un.buf_, 'k');
    check("  and is dirty", tb_changed(&un.buf_) ? 1 : 0, 1);

    stub_key failed_save[32];
    n = 0;
    failed_save[n].ch = 'y'; failed_save[n++].vk = VK_y;   /* yes, save it */
    failed_save[n].ch = 0;   failed_save[n++].vk = VK_ESCAPE; /* ...no name */
    for (const char* q = "first.txt"; *q; q++) {           /* would be opened */
        failed_save[n].ch = *q; failed_save[n++].vk = VK_NONE;
    }
    failed_save[n].ch = 0; failed_save[n++].vk = VK_RETURN;

    stub_file_reset();
    stub_file_set_content(first, (int) sizeof(first) - 1);
    stub_set_keys(failed_save, n);
    cmd_open(&un);
    check("a save that did not happen stops the open", tb_used(&un.buf_), 1);
    check("  the work is still there", tb_changed(&un.buf_) ? 1 : 0, 1);
    check("  and nothing was written", stub_file_size(), 0);
    ed_destroy(&un);

    /* --- a message waits to be read --- */
    /* The footer is repainted at the top of every pass through the main loop,
     * so a message that did not block would be gone before it could be read. */
    n = 0;
    for (int i = 0; i < 10; i++) {
        namekeys[n].ch = 0x7F; namekeys[n++].vk = VK_BACKSPACE;
    }
    for (const char* q = "huge.txt"; *q; q++) {
        namekeys[n].ch = *q; namekeys[n++].vk = VK_NONE;
    }
    namekeys[n].ch = 0; namekeys[n++].vk = VK_RETURN;
    namekeys[n].ch = ' '; namekeys[n++].vk = VK_SPACE;   /* dismisses it */

    stub_file_reset();
    stub_file_set_content(huge, (int) sizeof(huge));
    stub_set_keys(namekeys, n);
    cmd_open(&ed);
    check("the message consumed the key that dismisses it",
          stub_keys_read(), n);

    ed_destroy(&ed);
    stub_set_keys(NULL, 0);

    /* --- the binding --- */
    /* A command nothing can reach is not a feature. */
    key_command kc;
    memset(&kc, 0, sizeof(kc));
    kc.k.vkey = VK_o;
    check("CTRL+O opens a file", ctrlCmds(kc, 0).cmd == cmd_open, 1);
    kc.k.vkey = VK_O;
    check("  shifted too", ctrlCmds(kc, 0).cmd == cmd_open, 1);
    /* And it did not take a key something else was already using. */
    kc.k.vkey = VK_s;
    check("CTRL+S still saves", ctrlCmds(kc, 0).cmd == CMD_SAVE, 1);
    check("CTRL+ALT+S is still save-as",
          ctrlCmds(kc, MOD_ALT).cmd == cmd_save_as, 1);
    kc.k.vkey = VK_q;
    check("CTRL+Q still quits", ctrlCmds(kc, 0).cmd == CMD_QUIT, 1);
    kc.k.vkey = VK_g;
    check("CTRL+G still goes to a line", ctrlCmds(kc, 0).cmd == cmd_goto, 1);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
