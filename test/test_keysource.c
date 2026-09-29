/*
 * Prompts, and the editor's loop, read from a key source that does not block.
 *
 * A source's poll answers at once -- a key, or nothing yet -- and a widget
 * waiting on it runs the source's idle between polls. That is what lets a
 * program keep its own state moving while a prompt is up. These run real
 * prompts, and the loop itself, from a scripted source that answers "nothing
 * yet" a few times before each key, and count the turns the waiting gave it.
 */

#include <stdio.h>
#include <string.h>

#include <agon/keyboard.h>
#include <agon/mos.h>

#include "aed.h"
#include "editor.h"
#include "keys.h"
#include "user_input.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* Keys handed out one at a time, each after `gap` polls that find nothing.
 * Once they run out every poll answers ESCAPE, so a prompt that wanted more
 * than the script holds is cancelled rather than waiting for ever. */
typedef struct {
    const key_press* keys;
    int n;
    int at;
    int gap;
    int empty;
    int polls;
    int idles;
} script;

static bool script_poll(void* ctx, key_press* kp) {
    script* s = (script*) ctx;
    s->polls++;
    if (s->at >= s->n) {
        const key_press esc = { 27, VK_ESCAPE, 0 };
        *kp = esc;

        return true;
    }
    if (s->empty < s->gap) {
        s->empty++;

        return false;
    }
    s->empty = 0;
    *kp = s->keys[s->at++];

    return true;
}

static void script_idle(void* ctx) {
    ((script*) ctx)->idles++;
}

static script sc;
static key_source src = { script_poll, script_idle, &sc };

static void play(const key_press* keys, int n, int gap) {
    memset(&sc, 0, sizeof(sc));
    sc.keys = keys;
    sc.n = n;
    sc.gap = gap;
}

int main(void) {
    stub_discard_output();

    static screen scr;
    static user_input ui;
    stub_set_screen(80, 25);
    scr_init(&scr, 32);
    ui_init(&ui, 256, scr.whole_.bottomY_, scr.whole_.cols_);
    check("a prompt reads the keyboard unless told otherwise",
          ui.keys_ == &KEYS_MOS ? 1 : 0, 1);
    ui_set_keys(&ui, &src);
    check("  and reads the source it is given", ui.keys_ == &src ? 1 : 0, 1);

    /* --- a prompt answered from a source that is not ready each time --- */
    {
        static const key_press keys[] = {
            { '1', VK_1, 0 }, { '2', VK_2, 0 }, { 13, VK_RETURN, 0 },
        };
        play(keys, 3, 3);
        int line = 0;
        check("go-to answers from the script", ui_goto(&ui, &scr, &line), YES_OPT);
        check("  with the line typed", line, 12);
        check("  every key taken", sc.at, 3);
        check("  and the program had a turn at every empty poll", sc.idles, 9);
    }

    /* --- a question, and a line of text --- */
    {
        static const key_press yes[] = { { 'y', VK_y, 0 } };
        play(yes, 1, 2);
        check("a yes/no question answers from it",
              ui_dialog(&ui, &scr, "Sure?"), YES_OPT);
        check("  after its two turns", sc.idles, 2);

        static const key_press typed[] = {
            { 'a', VK_a, 0 }, { 'b', VK_b, 0 }, { 13, VK_RETURN, 0 },
        };
        play(typed, 3, 1);
        char* buf = NULL;
        int sz = 0;
        check("a text prompt answers from it",
              ui_text(&ui, &scr, "Find: ", NULL, &buf, &sz), YES_OPT);
        check("  with what was typed",
              sz == 2 && buf != NULL && memcmp(buf, "ab", 2) == 0 ? 1 : 0, 1);
    }

    /* --- a source with nothing to do while waiting --- */
    {
        static const key_press keys[] = { { 'n', VK_n, 0 } };
        play(keys, 1, 4);
        src.idle = NULL;
        check("no idle is fine: the wait just polls",
              ui_dialog(&ui, &scr, "Sure?"), NO_OPT);
        check("  as often as it takes", sc.polls, 5);
        src.idle = script_idle;
    }

    ui_set_keys(&ui, NULL);
    check("NULL goes back to the keyboard", ui.keys_ == &KEYS_MOS ? 1 : 0, 1);
    ui_destroy(&ui);
    scr_destroy(&scr);

    /* --- the keyboard itself answers at once --- */
    {
        /* The stub queue answers ESCAPE once its script runs out, so no prompt
         * can hang on it; a stall is how it says nothing has arrived yet. */
        key_press kp;
        stub_set_keys(NULL, 0);
        stub_keys_stall(1);
        check("keys_poll with nothing queued says so", keys_poll(&kp) ? 1 : 0, 0);
        static const stub_key queue[] = {
            { 'a', 22, 0, 1 },                      /* a release */
            { 0,  117, 0, 0 },                      /* SHIFT going down */
            { 'q', 38, 0, 0 },                      /* a key */
        };
        stub_set_keys(queue, 3);
        check("  and past a release and a modifier", keys_poll(&kp) ? 1 : 0, 1);
        check("  hands over the key behind them", kp.ch, 'q');
        check("  having taken all three", stub_keys_read(), 3);

        static const stub_key w[] = { { 'w', 44, 0, 0 } };
        stub_set_keys(w, 1);
        check("waiting on no source waits on the keyboard", ks_wait(NULL).ch, 'w');
    }

    /* --- the editor's loop reads the same source --- */
    {
        static editor ed;
        stub_file_reset();
        static const char DOC[] = "one\r\n";
        stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
        ed_init(&ed, 8, "doc.txt");
        static const key_press keys[] = {
            { 'x', VK_x, 0 },
            { 17, VK_q, MOD_CTRL },     /* quit: the document has changed... */
            { 'n', VK_n, 0 },           /* ...so it asks, and this is "don't save" */
        };
        play(keys, 3, 2);
        ui_set_keys(&ed.ui_, &src);
        /* The keyboard holds a CTRL+Q of its own, so a loop that read it
         * instead of the source would leave at once rather than never. */
        static const stub_key quit[] = { { 17, 38, MOD_CTRL, 0 } };
        stub_set_keys(quit, 1);
        ed_run(&ed);
        check("the loop ran from the source until told to leave", sc.at, 3);
        check("  giving the program its turns there too", sc.idles, 6);
        const tb_pos top = { .line = 1, .x = 0 };
        tb_seek(&ed.doc_->buf_, top);
        const split_line sl = tb_curr_line(&ed.doc_->buf_);
        check("  and the key it read is in the document",
              sl.ssz_ >= 4 && memcmp(sl.suffix_, "xone", 4) == 0 ? 1 : 0, 1);
        ed_destroy(&ed);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
