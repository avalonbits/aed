/*
 * Host tests for the four ends of the character buffer.
 *
 * A slide moves the window a document is seen through: bytes leave one end into
 * a file and arrive at the other end from one. The cursor does not move and the
 * gap does not change size -- what changes is which part of the document the
 * buffer holds.
 *
 * The property worth checking is that the four are two pairs of inverses, and
 * that none of them disturbs the cursor. A slide that shifted the cursor by a
 * byte would be almost impossible to notice and would corrupt every position
 * the editor had already handed out.
 *
 * See .internal/docs/PAGING.md.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "char_buffer.h"
#include "line_buffer.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void check_bytes(const char* name, const char* got, int gotsz,
                        const char* want, int wantsz) {
    if (gotsz == wantsz && (gotsz == 0 || memcmp(got, want, (size_t) gotsz) == 0)) {
        fprintf(stderr, "PASS  %-54s %d bytes\n", name, gotsz);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d bytes, want %d\n", name, gotsz, wantsz);
        failures++;
    }
}

/* What the buffer holds, read out as one string: the prefix then the suffix,
 * which is the document with the gap taken out of the middle. */
static int contents(char_buffer* cb, char* out) {
    int psz = 0;
    int ssz = 0;
    const char* pre = cb_prefix(cb, &psz);
    const char* suf = cb_suffix(cb, &ssz);
    int n = 0;
    if (pre != NULL && psz > 0) {
        memcpy(out, pre, (size_t) psz);
        n += psz;
    }
    if (suf != NULL && ssz > 0) {
        memcpy(out + n, suf, (size_t) ssz);
        n += ssz;
    }
    out[n] = 0;

    return n;
}

/* A buffer of `cap` holding "abcdefghij", with the cursor after `at` of them. */
static int make(char_buffer* cb, int cap, int at) {
    if (cb_init(cb, cap) == NULL) {
        return 0;
    }
    for (int i = 0; i < 10; i++) {
        cb_put(cb, (char) ('a' + i));
    }
    cb_prev(cb, 10 - at);

    return 1;
}

int main(void) {
    stub_discard_output();
    static char buf[128];
    static char all[128];
    char_buffer cb;

    /* --- taking from the front takes the oldest text --- */
    {
        check("a buffer with the cursor in the middle", make(&cb, 32, 5), 1);
        check("  five bytes before it", cb_used(&cb), 10);

        check("three come off the front", cb_take_front(&cb, buf, 3), 3);
        check_bytes("  the first three", buf, 3, "abc", 3);
        check_bytes("  leaving the rest", all, contents(&cb, all), "defghij", 7);

        /* The cursor was after "abcde" and is still after "de" -- the same
         * place in the document, now that "abc" is not in the buffer. */
        int psz = 0;
        cb_prefix(&cb, &psz);
        check("  and the cursor did not move", psz, 2);
        cb_destroy(&cb);
    }

    /* --- giving to the front puts it back --- */
    {
        check("a buffer to put text in front of", make(&cb, 32, 5), 1);
        cb_take_front(&cb, buf, 3);
        check("giving them back", cb_give_front(&cb, "abc", 3) ? 1 : 0, 1);
        check_bytes("  restores the buffer", all, contents(&cb, all), "abcdefghij", 10);
        int psz = 0;
        cb_prefix(&cb, &psz);
        check("  and the cursor with it", psz, 5);
        cb_destroy(&cb);
    }

    /* --- taking from the back takes the newest --- */
    {
        check("a buffer to take the end off", make(&cb, 32, 5), 1);
        check("three come off the back", cb_take_back(&cb, buf, 3), 3);
        check_bytes("  the last three", buf, 3, "hij", 3);
        check_bytes("  leaving the rest", all, contents(&cb, all), "abcdefg", 7);
        int psz = 0;
        cb_prefix(&cb, &psz);
        check("  and the cursor did not move", psz, 5);
        cb_destroy(&cb);
    }

    /* --- giving to the back appends --- */
    {
        check("a buffer to append to", make(&cb, 32, 5), 1);
        cb_take_back(&cb, buf, 3);
        check("giving them back", cb_give_back(&cb, "hij", 3) ? 1 : 0, 1);
        check_bytes("  restores the buffer", all, contents(&cb, all), "abcdefghij", 10);
        int psz = 0;
        cb_prefix(&cb, &psz);
        check("  and the cursor with it", psz, 5);
        cb_destroy(&cb);
    }

    /* --- a whole slide: out of one end, in at the other --- */
    {
        check("a buffer to slide", make(&cb, 32, 5), 1);
        const int used_before = cb_used(&cb);

        /* Down: the front goes to the head, new text arrives at the back. */
        check("the front gives up three", cb_take_front(&cb, buf, 3), 3);
        check("  and three arrive at the back", cb_give_back(&cb, "klm", 3) ? 1 : 0, 1);
        check_bytes("  so the window moved along", all, contents(&cb, all),
                    "defghijklm", 10);
        check("  holding the same amount", cb_used(&cb), used_before);
        int psz = 0;
        cb_prefix(&cb, &psz);
        check("  with the cursor where it was", psz, 2);

        /* Up: the reverse, exactly. */
        check("the back gives up three", cb_take_back(&cb, buf, 3), 3);
        check_bytes("  which is what arrived", buf, 3, "klm", 3);
        check("  and three arrive at the front", cb_give_front(&cb, "abc", 3) ? 1 : 0, 1);
        check_bytes("  so the window is back where it was", all, contents(&cb, all),
                    "abcdefghij", 10);
        cb_prefix(&cb, &psz);
        check("  and so is the cursor", psz, 5);
        cb_destroy(&cb);
    }

    /* --- neither end reaches past the cursor --- */
    {
        /* With the cursor at the document's start there is nothing in front of
         * it, so there is nothing to take off the front -- even though the
         * buffer is full of text, all of which is behind the cursor.
         *
         * Taking it anyway would be worse than refusing. Those bytes are after
         * the cursor, so moving them out of the buffer would leave the cursor
         * pointing at a position outside the text the buffer holds, which is
         * not something the rest of the editor can express. A caller that gets
         * fewer bytes than it asked for has a cursor too close to that end to
         * slide, and the margins are what stop that happening. */
        check("a buffer with the cursor at the very start", make(&cb, 32, 0), 1);
        check("  has ten bytes in it", cb_used(&cb), 10);
        check("but nothing in front of the cursor to take",
              cb_take_front(&cb, buf, 3), 0);
        check_bytes("  so nothing moved", all, contents(&cb, all), "abcdefghij", 10);

        check("  while the back still gives", cb_take_back(&cb, buf, 3), 3);
        check_bytes("    the last three", buf, 3, "hij", 3);
        cb_destroy(&cb);

        check("a buffer with the cursor at the very end", make(&cb, 32, 10), 1);
        check("nothing behind the cursor to take", cb_take_back(&cb, buf, 3), 0);
        check_bytes("  so nothing moved", all, contents(&cb, all), "abcdefghij", 10);
        check("  while the front still gives", cb_take_front(&cb, buf, 3), 3);
        check_bytes("    the first three", buf, 3, "abc", 3);
        cb_destroy(&cb);

        /* And in between, each end gives only its own side. */
        check("a buffer with four in front and six behind", make(&cb, 32, 4), 1);
        check("the front gives four and stops", cb_take_front(&cb, buf, 100), 4);
        check_bytes("  which is all of the prefix", buf, 4, "abcd", 4);
        cb_destroy(&cb);
    }

    /* --- asking for more than an end holds gives what it holds --- */
    {
        check("a buffer to over-ask", make(&cb, 32, 5), 1);
        check("the front holds five", cb_take_front(&cb, buf, 100), 5);
        check_bytes("  and gives all five", buf, 5, "abcde", 5);
        check("the back holds the other five", cb_take_back(&cb, buf, 100), 5);
        check_bytes("  and gives those", buf, 5, "fghij", 5);
        check("  leaving nothing", cb_used(&cb), 0);
        check("  and nothing more to give", cb_take_front(&cb, buf, 1), 0);
        cb_destroy(&cb);
    }

    /* --- giving more than the gap can hold is refused, changing nothing --- */
    {
        check("a buffer with almost no gap", make(&cb, 12, 5), 1);
        check("  two bytes of gap", cb_available(&cb), 2);
        check("giving three is refused", cb_give_front(&cb, "xyz", 3) ? 1 : 0, 0);
        check_bytes("  and nothing changed", all, contents(&cb, all), "abcdefghij", 10);
        check("giving three at the back is refused too",
              cb_give_back(&cb, "xyz", 3) ? 1 : 0, 0);
        check_bytes("  and still nothing changed", all, contents(&cb, all),
                    "abcdefghij", 10);
        check("giving two fits exactly", cb_give_front(&cb, "xy", 2) ? 1 : 0, 1);
        check_bytes("  and lands at the front", all, contents(&cb, all),
                    "xyabcdefghij", 12);
        check("  filling the buffer", cb_available(&cb), 0);
        cb_destroy(&cb);
    }

    /* ---------------------------------------------------------------- *
     *  The line index's four ends, which have to move with the text.
     * ---------------------------------------------------------------- */

    /* Six lines of the lengths given, with the cursor on line `at`. */
    line_buffer lb;
    static const int LENS[6] = { 11, 22, 33, 44, 55, 66 };

    {
        /* Built the way the loader builds it: count the bytes of a line with
         * lb_cinc, then close it with lb_new(size) -- which keeps `size` on
         * this line and gives the remainder, none, to the next. */
        check("an index of six lines", lb_init(&lb, 32) != NULL, 1);
        for (int i = 0; i < 6; i++) {
            for (int k = 0; k < LENS[i]; k++) {
                lb_cinc(&lb);
            }
            if (i < 5) {
                lb_new(&lb, lb_csize(&lb));
            }
        }
        check("  the cursor is on the last of them", lb_curr(&lb), 5);
        check("  which is as long as it was made", lb_csize(&lb), 66);

        /* Back to the middle, so there is something on each side. */
        lb_up(&lb);
        lb_up(&lb);
        check("moving up two puts the cursor on the fourth", lb_curr(&lb), 3);
        check("  with the fourth line's length", lb_csize(&lb), 44);
    }

    /* --- taking from the front takes the earliest lines --- */
    {
        int out[8];
        check("two lines come off the front", lb_take_front(&lb, out, 2), 2);
        check("  the first", out[0], 11);
        check("  and the second", out[1], 22);
        check("the cursor is now the second line in memory", lb_curr(&lb), 1);
        check("  still the same line, still that long", lb_csize(&lb), 44);
    }

    /* --- and giving them back undoes it exactly --- */
    {
        int back[2] = { 11, 22 };
        check("giving them back", lb_give_front(&lb, back, 2) ? 1 : 0, 1);
        check("  puts the cursor back", lb_curr(&lb), 3);
        check("  on the same line", lb_csize(&lb), 44);
        lb_up(&lb);
        check("  with the one before it intact", lb_csize(&lb), 33);
        lb_down(&lb);
        check("  and the one after", (lb_down(&lb), lb_csize(&lb)), 55);
        lb_up(&lb);
    }

    /* --- taking from the back takes the latest --- */
    {
        int out[8];
        check("one line comes off the back", lb_take_back(&lb, out, 1), 1);
        check("  the last one", out[0], 66);
        check("the cursor has not moved", lb_curr(&lb), 3);
        check("  nor has its line", lb_csize(&lb), 44);
        check("giving it back", lb_give_back(&lb, out, 1) ? 1 : 0, 1);
        check("  leaves the cursor alone too", lb_curr(&lb), 3);
    }

    /* --- the line the cursor is on cannot leave --- */
    {
        int out[8];
        line_buffer top;
        check("an index with the cursor on the first line",
              lb_init(&top, 16) != NULL, 1);
        lb_new(&top, 0);
        lb_up(&top);
        check("  which is where it is", lb_curr(&top), 0);
        check("nothing in front of it to take", lb_take_front(&top, out, 1), 0);
        check("  and the cursor is untouched", lb_curr(&top), 0);
        lb_destroy(&top);
    }

    /* --- giving more than there are slots for is refused --- */
    {
        /* Sized to the room there actually is: an array too small to cover it
         * is a read past the end of the test's own stack, which is a bug in
         * the test rather than in what it is testing. */
        int in[64];
        for (int i = 0; i < (int)(sizeof(in) / sizeof(in[0])); i++) {
            in[i] = i + 1;
        }
        const int room = lb_room(&lb);
        check("  there is room to give into", room > 0 && room < 64, 1);
        check("giving one more than there is room for",
              lb_give_front(&lb, in, room + 1) ? 1 : 0, 0);
        check("  changes nothing", lb_room(&lb), room);
        check("giving exactly the room works",
              lb_give_front(&lb, in, room) ? 1 : 0, 1);
        check("  and fills it", lb_room(&lb), 0);

        /* And the lines after the cursor are still there. One entry too many
         * lands on the first of them, which is inside the allocation and so
         * invisible to the sanitiser -- the only way to see it is to look at
         * what it would have overwritten. */
        check("  with the line after the cursor untouched",
              (lb_down(&lb), lb_csize(&lb)), 55);
        check("  and the one after that", (lb_down(&lb), lb_csize(&lb)), 66);
        lb_destroy(&lb);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
