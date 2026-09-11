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

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
