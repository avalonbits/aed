/*
 * Host tests for the document store: the two scratch files that hold whatever
 * is not in memory.
 *
 * The property the whole paging design rests on is that text on disk is never
 * edited -- only pushed and popped at the end facing memory. These check that
 * the pushes and pops are exact inverses, which is what makes sliding a window
 * over a document safe: slide down and back up and the document has to be the
 * one you started with, byte for byte.
 *
 * See .internal/docs/PAGING.md.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "doc_store.h"

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

static const char DOC[] =
    "aaaaaaaaaabbbbbbbbbbccccccccccddddddddddeeeeeeeeeeffffffffffgggggggggg";
#define DOC_LEN ((int) sizeof(DOC) - 1)

/* A store holding the whole document in its tail, the way open leaves it. */
static int filled(doc_store* st) {
    stub_file_reset();
    if (!store_init(st, "/doc.txt")) {
        return 0;
    }

    return store_tail_append(st, DOC, DOC_LEN) ? 1 : 0;
}

int main(void) {
    stub_discard_output();
    static char buf[256];
    doc_store st;

    /* --- a new store is empty on both sides --- */
    {
        stub_file_reset();
        check("a store opens", store_init(&st, "/doc.txt") ? 1 : 0, 1);
        check("  with nothing in its head", store_head_bytes(&st), 0);
        check("  nor its tail", store_tail_bytes(&st), 0);
        check("  and both files exist", stub_file_exists("/doc.txt.aedh")
                                        && stub_file_exists("/doc.txt.aedt"), 1);
        store_destroy(&st);
        check("destroying takes them away", stub_file_exists("/doc.txt.aedh")
                                            || stub_file_exists("/doc.txt.aedt"), 0);
    }

    /* --- a document with no name still gets somewhere to put its scratch --- */
    {
        stub_file_reset();
        check("an unnamed document still opens a store",
              store_init(&st, NULL) ? 1 : 0, 1);
        check("  beside nothing, in the current directory",
              stub_file_exists("aed.aedh"), 1);
        store_destroy(&st);
    }

    /* --- building the tail at open --- */
    {
        check("a document goes into the tail", filled(&st), 1);
        check("  all of it", store_tail_bytes(&st), DOC_LEN);
        check("  and the head is still empty", store_head_bytes(&st), 0);

        /* The headroom is seeked past, never written, so the file is longer
         * than the document by exactly that much and nothing was transferred
         * to make it so. */
        int len = 0;
        stub_file_content("/doc.txt.aedt", &len);
        check("  with the headroom in front of it", len,
              STORE_HEADROOM + DOC_LEN);
        store_destroy(&st);
    }

    /* --- popping the tail hands the document over in order --- */
    {
        check("a document to walk out of the tail", filled(&st), 1);
        check("ten bytes come out", store_tail_pop(&st, buf, 10), 10);
        check_bytes("  the first ten", buf, 10, "aaaaaaaaaa", 10);
        check("  and the tail is that much shorter", store_tail_bytes(&st),
              DOC_LEN - 10);
        check("ten more come out", store_tail_pop(&st, buf, 10), 10);
        check_bytes("  carrying on where the last stopped", buf, 10,
                    "bbbbbbbbbb", 10);
        store_destroy(&st);
    }

    /* --- asking for more than there is gives what there is --- */
    {
        check("a document to empty", filled(&st), 1);
        check("asking for more than the tail holds", store_tail_pop(&st, buf, 1000),
              DOC_LEN);
        check("  empties it", store_tail_bytes(&st), 0);
        check("  and a pop from an empty tail gives nothing",
              store_tail_pop(&st, buf, 10), 0);
        store_destroy(&st);
    }

    /* --- the head is a stack --- */
    {
        stub_file_reset();
        store_init(&st, "/doc.txt");
        check("pushing onto the head", store_head_push(&st, "first", 5) ? 1 : 0, 1);
        check("  then more", store_head_push(&st, "second", 6) ? 1 : 0, 1);
        check("  counts both", store_head_bytes(&st), 11);

        check("popping gives the last six back", store_head_pop(&st, buf, 6), 6);
        check_bytes("  which is what went on last", buf, 6, "second", 6);
        check("  leaving the first", store_head_bytes(&st), 5);
        check("popping again gives it", store_head_pop(&st, buf, 5), 5);
        check_bytes("  in one piece", buf, 5, "first", 5);
        check("  and the head is empty", store_head_bytes(&st), 0);
        check("  with nothing more to give", store_head_pop(&st, buf, 5), 0);
        store_destroy(&st);
    }

    /* --- a push after a pop writes over what the pop left behind --- */
    {
        stub_file_reset();
        store_init(&st, "/doc.txt");
        store_head_push(&st, "abcdefghij", 10);
        store_head_pop(&st, buf, 4);
        check("the head is six after popping four", store_head_bytes(&st), 6);
        check("pushing four more", store_head_push(&st, "WXYZ", 4) ? 1 : 0, 1);
        check("  puts it back to ten", store_head_bytes(&st), 10);
        check("popping all of it", store_head_pop(&st, buf, 10), 10);
        /* There is no ffs_ftruncate below MOS 2.3.0, so the file still carries
         * the "ghij" that was popped -- the push wrote over it rather than
         * appending after it, which is the whole reason head_len_ exists. */
        check_bytes("  gives what was pushed, not what was left",
                    buf, 10, "abcdefWXYZ", 10);
        store_destroy(&st);
    }

    /* --- a slide down and back up is the identity --- */
    {
        check("a document to slide over", filled(&st), 1);

        /* Down: memory's front goes to the head, the tail's front comes in. */
        check("the tail gives up a chunk", store_tail_pop(&st, buf, 20), 20);
        check("  and the head takes one", store_head_push(&st, "0123456789", 10) ? 1 : 0, 1);

        /* Up: the head gives it back, memory's back goes to the tail. */
        check("the head gives the chunk back", store_head_pop(&st, buf, 10), 10);
        check_bytes("  unchanged", buf, 10, "0123456789", 10);
        check("  and the head is empty again", store_head_bytes(&st), 0);

        check("the tail takes its bytes back",
              store_tail_push(&st, "aaaaaaaaaabbbbbbbbbb", 20) ? 1 : 0, 1);
        check("  and is the length it started", store_tail_bytes(&st), DOC_LEN);

        /* The whole document, read back out, has to be what went in. */
        const int n = store_tail_pop(&st, buf, sizeof(buf));
        check_bytes("and the document survived the round trip", buf, n,
                    DOC, DOC_LEN);
        store_destroy(&st);
    }

    /* --- the headroom is finite, and says so before it is gone --- */
    {
        stub_file_reset();
        store_init(&st, "/doc.txt");
        store_tail_append(&st, DOC, DOC_LEN);

        check("there is room for a chunk", store_tail_has_room(&st, 2048) ? 1 : 0, 1);
        check("  and for all the headroom", store_tail_has_room(&st, STORE_HEADROOM) ? 1 : 0, 1);
        check("  but not for a byte more",
              store_tail_has_room(&st, STORE_HEADROOM + 1) ? 1 : 0, 0);

        /* Pushing back more than was ever taken is what spends it: the tail
         * grows towards the front of the file, and the front is where the file
         * begins. */
        static char big[STORE_HEADROOM];
        memset(big, 'z', sizeof(big));
        check("pushing the whole headroom back works",
              store_tail_push(&st, big, STORE_HEADROOM) ? 1 : 0, 1);
        check("  and then there is none left",
              store_tail_has_room(&st, 1) ? 1 : 0, 0);
        check("  so another push is refused",
              store_tail_push(&st, "x", 1) ? 1 : 0, 0);
        check("  leaving the tail as it was",
              store_tail_bytes(&st), STORE_HEADROOM + DOC_LEN);
        store_destroy(&st);
    }

    /* --- nothing works on a store that never opened --- */
    {
        doc_store dead;
        memset(&dead, 0, sizeof(dead));
        check("a closed store has no head", store_head_bytes(&dead), 0);
        check("  nor tail", store_tail_bytes(&dead), 0);
        check("  takes no push", store_head_push(&dead, "x", 1) ? 1 : 0, 0);
        check("  gives no pop", store_head_pop(&dead, buf, 1), 0);
        check("  and destroying it is harmless", (store_destroy(&dead), 1), 1);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
