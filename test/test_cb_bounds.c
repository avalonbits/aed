/*
 * Host tests for the buffer-full boundary.
 *
 * cb_put used to write unconditionally, so filling a char_buffer past capacity
 * ran off the end of its malloc. That was reachable from the `File name:`
 * prompt, where ui_text calls cb_put once per keystroke with no limit against a
 * 256-byte buffer.
 *
 * A refused write must also leave the caller's bookkeeping alone: if tb_put
 * still advanced x_ and the line length after a rejected cb_put, the overflow
 * would simply be traded for a text/line-index desync.
 *
 * Run under ASan (see test/run.sh) so an out-of-bounds write fails loudly
 * rather than silently corrupting the heap.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "char_buffer.h"
#include "line_buffer.h"
#include "text_buffer.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

int main(void) {
    stub_discard_output();

    /* --- char_buffer --- */
    char_buffer cb;
    if (!cb_init(&cb, 8)) {
        fprintf(stderr, "cb_init failed\n");

        return 2;
    }
    for (int i = 0; i < 8; i++) {
        check("cb_put accepts up to capacity", cb_put(&cb, 'x') ? 1 : 0, 1);
    }
    check("buffer is exactly full", cb_used(&cb), 8);
    check("cb_available is zero", cb_available(&cb), 0);

    /* The write that used to run off the end of the allocation. */
    /* Moving the cursor across a gap smaller than the move, which is what a
     * nearly full buffer gives. The move is a memmove now rather than a byte
     * loop, and the two sides overlap exactly here -- with the buffer full the
     * gap is nothing at all, so source and destination are the same bytes.
     * memcpy would be free to get this wrong; the byte loop it replaced copied
     * in the safe direction by construction. */
    {
        char_buffer ov;
        check("a buffer to fill", cb_init(&ov, 8) != NULL, 1);
        for (int i = 0; i < 8; i++) {
            cb_put(&ov, (char) ('1' + i));
        }
        check("  full, so the gap is empty", cb_available(&ov), 0);
        cb_prev(&ov, 8);
        check("  the whole of it moved back", cb_used(&ov), 8);
        int n = 0;
        const char* suf = cb_suffix(&ov, &n);
        check("  and all of it is on the far side", n, 8);
        check("  with its bytes in order",
              suf != NULL && memcmp(suf, "12345678", 8) == 0 ? 1 : 0, 1);
        cb_next(&ov, 8);
        int pn = 0;
        const char* pre = cb_prefix(&ov, &pn);
        check("  and forward again puts them back", pn, 8);
        check("  still in order",
              pre != NULL && memcmp(pre, "12345678", 8) == 0 ? 1 : 0, 1);
        cb_destroy(&ov);
    }

    /* A move longer than there is text. Both directions clamp to what is
     * actually there; without that the block move runs off the allocation, and
     * a count from a caller is not always one the buffer can honour -- tb_up
     * and tb_down both compute theirs from the line index. ASan is what makes
     * this bite: the failure is a read and a write past the end, not a wrong
     * answer. */
    {
        char_buffer big;
        check("a buffer to overrun", cb_init(&big, 8) != NULL, 1);
        for (int i = 0; i < 8; i++) {
            cb_put(&big, (char) ('a' + i));
        }
        cb_prev(&big, 1000000);
        int n = 0;
        const char* suf = cb_suffix(&big, &n);
        check("  a move past the start stops at the start", n, 8);
        check("  with the text intact",
              suf != NULL && memcmp(suf, "abcdefgh", 8) == 0 ? 1 : 0, 1);
        cb_next(&big, 1000000);
        int pn = 0;
        const char* pre = cb_prefix(&big, &pn);
        check("  a move past the end stops at the end", pn, 8);
        check("  with the text still intact",
              pre != NULL && memcmp(pre, "abcdefgh", 8) == 0 ? 1 : 0, 1);
        cb_destroy(&big);
    }

    check("cb_put refuses when full", cb_put(&cb, 'y') ? 1 : 0, 0);
    check("refused put does not grow the buffer", cb_used(&cb), 8);
    check("still refuses on a second attempt", cb_put(&cb, 'z') ? 1 : 0, 0);
    check("contents untouched", memcmp(cb.buf_, "xxxxxxxx", 8), 0);

    /* Room reappears once something is removed. */
    check("bksp frees a slot", cb_bksp(&cb) ? 1 : 0, 1);
    check("cb_put accepts again", cb_put(&cb, 'w') ? 1 : 0, 1);
    cb_destroy(&cb);

    /* --- text_buffer: a refused put must not move the cursor --- */
    text_buffer tb;
    if (!tb_init(&tb, 1, NULL)) {   /* 1KB budget -> small char buffer */
        fprintf(stderr, "tb_init failed\n");

        return 2;
    }
    const int cap = tb_size(&tb);
    for (int i = 0; i < cap; i++) {
        if (!tb_put(&tb, 'a')) {
            check("tb_put should fill to capacity", 0, 1);
            break;
        }
    }
    check("text buffer full", tb_available(&tb), 0);

    const int x_before = tb_xpos(&tb);
    check("tb_put refuses when full", tb_put(&tb, 'b') ? 1 : 0, 0);
    check("refused tb_put leaves the column alone", tb_xpos(&tb), x_before);
    check("refused tb_put does not grow the buffer", tb_available(&tb), 0);

    /* A newline needs two bytes; with none free it must refuse outright rather
     * than write a lone CR and desync the line index. */
    check("tb_newline refuses with no room", tb_newline(&tb) ? 1 : 0, 0);
    check("refused newline wrote nothing", tb_available(&tb), 0);

    /* One free byte is still not enough for CRLF. */
    check("bksp frees one byte", tb_bksp(&tb) ? 1 : 0, 1);
    check("one free byte is not enough for CRLF", tb_newline(&tb) ? 1 : 0, 0);
    check("still nothing written", tb_available(&tb), 1);

    /* Two is. */
    check("bksp frees a second byte", tb_bksp(&tb) ? 1 : 0, 1);
    const int y_before = tb_ypos(&tb);
    check("tb_newline succeeds with two bytes free", tb_newline(&tb) ? 1 : 0, 1);
    check("line count advanced", tb_ypos(&tb), y_before + 1);
    tb_destroy(&tb);

    /* cb_prev with a count of zero skips its loop entirely, so the read it
     * ends with is unguarded -- and with the cursor at the very end of the
     * buffer, cend_ is one past the last byte. tb_home does exactly this
     * (cb_prev(cb, 0)) whenever HOME is pressed with the cursor already at the
     * start of a line, and the last line of a document is where the cursor sits
     * at the end of the buffer. Sized exactly so the sanitizer sees the
     * over-read rather than it landing in slack. */
    {
        char_buffer* edge = malloc(sizeof(char_buffer));
        cb_init(edge, 8);
        for (int i = 0; i < 8; i++) {
            cb_put(edge, 'a' + i);
        }
        /* Everything is behind the cursor now, so cend_ is at the very end. */
        check("the buffer is full", cb_used(edge), 8);
        check("with nothing ahead of the cursor", cb_peek(edge), 0);
        check("cb_prev of zero reads nothing off the end", cb_prev(edge, 0), 0);
        cb_destroy(edge);
        free(edge);
    }

    /* --- where a rebalance puts the free space --- */
    {
        /*
         * Sliding one way eats the free space at one end and makes it at the
         * other, and the only time the live text has to be moved is when an end
         * runs dry. So the free space belongs at the ends: every byte there is
         * another slide that costs a pointer move.
         *
         * The gap keeps a small share, for typing. It used to keep a third,
         * because a walker read by moving the gap and had to be able to travel
         * a screenful through it without catching the cursor's own cend_.
         * Walkers move by number now -- see .internal/docs/WALKER.md -- so the
         * gap is for inserts and nothing else.
         */
        char_buffer* cb = (char_buffer*) malloc(sizeof(char_buffer));
        cb_init(cb, 8192);
        for (int i = 0; i < 4096; i++) {
            cb_put(cb, (char) ('a' + (i % 26)));
        }
        /* Take from the front until the end below the text runs dry, which is
         * what asks for the spread. */
        static char out[256];
        for (int i = 0; i < 8; i++) {
            cb_take_front(cb, out, 256);
        }
        static char in[256];
        memset(in, 'z', sizeof(in));
        check("giving to the back of a buffer works",
              cb_give_back(cb, in, 256) ? 1 : 0, 1);

        const int below = (int) (cb->lo_ - cb->buf_);
        const int gap = (int) (cb->cend_ - cb->curr_);
        const int above = (int) ((cb->buf_ + cb->size_) - cb->hi_);
        check("  and the free space adds up", below + gap + above,
              cb_available(cb));
        check("  with most of it at the ends",
              (below + above) > gap * 3 ? 1 : 0, 1);
        check("  and some of it still at the cursor", gap > 0 ? 1 : 0, 1);

        /* And the same spread when it is typing that asks for it. A run of
         * slides leaves the free space at the ends and shuts the gap; cb_put
         * takes a share of it back rather than reporting a full buffer, and
         * the share it takes is small for the same reason. */
        {
            char_buffer* t = (char_buffer*) malloc(sizeof(char_buffer));
            cb_init(t, 8192);
            for (int i = 0; i < 4096; i++) {
                cb_put(t, 'a');
            }
            cb_take_front(t, out, 256);     /* free space below the text */
            cb_take_front(t, out, 256);
            int put = 0;
            while ((int) (t->cend_ - t->curr_) > 0 && cb_put(t, 'b')) {
                put++;
            }
            check("  typing shuts the gap", (int) (t->cend_ - t->curr_), 0);
            check("    and one more keystroke goes in", cb_put(t, 'c') ? 1 : 0, 1);
            const int g = (int) (t->cend_ - t->curr_);
            const int ends = (int) (t->lo_ - t->buf_)
                + (int) ((t->buf_ + t->size_) - t->hi_);
            check("      by taking a share of the ends back", g > 0 ? 1 : 0, 1);
            check("      a small one, so the ends keep theirs",
                  ends > g * 3 ? 1 : 0, 1);
            cb_destroy(t);
            free(t);
        }

        /* A small gap is still a working buffer: typing refills it when it
         * finally shuts, and a span that will fit always goes in. */
        int typed = 0;
        while (typed < 4096 && cb_put(cb, 'q')) {
            typed++;
        }
        check("  typing into it keeps working", typed, 4096);
        static char span[1024];
        memset(span, 'w', sizeof(span));
        check("  and a span that fits still goes in whole",
              cb_write(cb, span, 1024) ? 1 : 0, 1);
        check("    all of it", cb_used(cb), 4096 - 2048 + 256 + 4096 + 1024);
        cb_destroy(cb);
        free(cb);
    }

    /* --- the sizes, without the pointers --- */
    {
        /*
         * cb_prefix_size and cb_suffix_size are the two halves of a char_buffer
         * measured without asking for the pointer that goes with them. They are
         * static inline in the header so that settling, which asks for both on
         * every cursor movement of a paged document, does not pay a call across
         * a translation unit for a pointer subtraction.
         *
         * Being a second copy of the arithmetic, they have to keep answering
         * what cb_prefix and cb_suffix answer through their `sz`. The empty
         * cases are where a copy would drift: those two report an empty half by
         * returning NULL and writing zero, so the size is the only thing the
         * inline versions can be compared against.
         */
        static char_buffer cb;
        check("a buffer to measure", cb_init(&cb, 64) != NULL, 1);

        int psz = 0;
        int ssz = 0;
        cb_prefix(&cb, &psz);
        cb_suffix(&cb, &ssz);
        check("  empty: the prefix agrees", cb_prefix_size(&cb), psz);
        check("    and is nothing", cb_prefix_size(&cb), 0);
        check("  empty: the suffix agrees", cb_suffix_size(&cb), ssz);
        check("    and is nothing", cb_suffix_size(&cb), 0);

        for (int i = 0; i < 10; i++) {
            cb_put(&cb, (char) ('a' + i));
        }
        cb_prefix(&cb, &psz);
        cb_suffix(&cb, &ssz);
        check("  ten in front of the cursor: the prefix agrees",
              cb_prefix_size(&cb), psz);
        check("    and is ten", cb_prefix_size(&cb), 10);
        check("  with nothing behind it", cb_suffix_size(&cb), ssz);
        check("    which is nothing", cb_suffix_size(&cb), 0);

        /* Walk the cursor back: bytes cross from one half to the other, and
         * both have to keep agreeing at every step. */
        int wrong = 0;
        for (int i = 0; i < 10; i++) {
            cb_prev(&cb, 1);
            cb_prefix(&cb, &psz);
            cb_suffix(&cb, &ssz);
            if (cb_prefix_size(&cb) != psz || cb_suffix_size(&cb) != ssz) {
                wrong = i + 1;
            }
        }
        check("  and through every position of the cursor", wrong, 0);
        check("    ending with all of it behind", cb_suffix_size(&cb), 10);
        check("      and none in front", cb_prefix_size(&cb), 0);

        /*
         * And with free space at the *front* of the buffer, which is where a
         * copy of this arithmetic goes wrong: the live bytes start at lo_, not
         * at buf_, and the two are only the same until something is taken off
         * the front. A slide down does that on every chunk it sends to the
         * head. Measuring from buf_ then counts the free space as text.
         */
        static char out[16];
        for (int i = 0; i < 10; i++) {
            cb_next(&cb, 1);        // all ten back in front of the cursor
        }
        check("  taking four off the front", cb_take_front(&cb, out, 4), 4);
        check("    leaves six", cb_used(&cb), 6);
        cb_prefix(&cb, &psz);
        cb_suffix(&cb, &ssz);
        check("    and the prefix agrees", cb_prefix_size(&cb), psz);
        check("      counting the six, not the ten", cb_prefix_size(&cb), 6);
        check("    with the suffix agreeing too", cb_suffix_size(&cb), ssz);
        cb_destroy(&cb);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
