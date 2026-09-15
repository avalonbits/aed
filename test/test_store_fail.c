/*
 * What a paged document does when the card will not take the writes.
 *
 * A document larger than memory keeps most of itself in two files, and every
 * slide of the window writes to one and reads from the other. On real hardware
 * those writes fail: the card fills, or it is pulled, or the write just does
 * not land. Nothing tested any of it. Coverage of the two slides and of the
 * load and save paths stopped exactly at the `if` that notices, so every
 * rollback below was code no test had ever run.
 *
 * What a failed slide owes its caller is that the document is unchanged -- not
 * merely that it survives. A slide takes text out of memory before it pushes
 * it, so a push that fails has to put the text back, in the buffer and in the
 * index both, or the document quietly loses a chunk of itself.
 *
 * The invariant every case here checks is the one the whole buffer rests on:
 * the index's lengths add up to the bytes memory holds. When they stop, the
 * next edit lands in the wrong place.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "text_buffer.h"
#include "text_buffer_int.h"
#include "line_buffer.h"
#include "char_buffer.h"
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

/* The index's lengths, added up: has to be what the buffer holds. */
static int index_sum(text_buffer* tb) {
    const int n = lb_lines(&tb->lb_);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += lb_at(&tb->lb_, i);
    }

    return sum;
}

static int agrees(text_buffer* tb) {
    return tb_used(tb) == index_sum(tb);
}

static int saved_has(text_buffer* tb, const char* name, const char* want) {
    (void) tb;
    int sz = 0;
    const char* got = stub_file_content(name, &sz);
    const int wsz = (int) strlen(want);

    return got != NULL && sz == wsz && memcmp(got, want, (size_t) wsz) == 0;
}

/* A document of numbered lines, big enough to page at 48 KiB. */
#define LINE_LEN 40
#define LINES    4000
static char DOC[LINES * LINE_LEN + 1];

static void build(void) {
    int at = 0;
    for (int i = 0; i < LINES; i++) {
        at += snprintf(DOC + at, LINE_LEN + 1, "line %06d ", i);
        while (at % LINE_LEN != LINE_LEN - 2) {
            DOC[at++] = '.';
        }
        DOC[at++] = '\r';
        DOC[at++] = '\n';
    }
}

static text_buffer tb;

static int open_paged(void) {
    stub_file_reset();
    stub_file_set_content(DOC, LINES * LINE_LEN);

    return tb_init(&tb, 48, "/big.txt") != NULL && tb.paged_;
}

int main(void) {
    stub_discard_output();
    build();

    /* --- a slide down whose push to the head is refused --- */
    {
        check("a paged document", open_paged(), 1);
        tb_pos mid = { 2000, 0 };
        tb_seek(&tb, mid);
        check("  with the cursor in the middle", tb_ypos(&tb), 2000);

        const int used = tb_used(&tb);
        const int lines = lb_lines(&tb.lb_);
        const int head = store_head_bytes(tb.store_);
        const int ymax = tb_ymax(&tb);

        stub_file_short_write(0);        /* the card takes nothing */
        const bool slid = tb_slide_down(&tb);
        stub_file_short_write(-1);

        check("  a slide down that cannot write says so",
              slid ? 1 : 0, 0);
        check("    and gives the text back to memory", tb_used(&tb), used);
        check("    and the lines back to the index", lb_lines(&tb.lb_), lines);
        check("    leaving the index describing the text", agrees(&tb), 1);
        check("    with the head where it was", store_head_bytes(tb.store_), head);
        check("    and the document the same length", tb_ymax(&tb), ymax);
        check("    and the line under the cursor still readable",
              tb_curr_line(&tb).ssz_ > 0 ? 1 : 0, 1);
        tb_destroy(&tb);
    }

    /* --- a slide up whose push to the tail is refused --- */
    {
        check("a paged document to slide up in", open_paged(), 1);
        tb_pos mid = { 2000, 0 };
        tb_seek(&tb, mid);

        const int used = tb_used(&tb);
        const int lines = lb_lines(&tb.lb_);
        const int tail = store_tail_bytes(tb.store_);
        const int ymax = tb_ymax(&tb);

        stub_file_short_write(0);
        const bool slid = tb_slide_up(&tb);
        stub_file_short_write(-1);

        check("  a slide up that cannot write says so", slid ? 1 : 0, 0);
        check("    and gives the text back", tb_used(&tb), used);
        check("    and the lines back", lb_lines(&tb.lb_), lines);
        check("    leaving the index describing the text", agrees(&tb), 1);
        check("    with the tail where it was", store_tail_bytes(tb.store_), tail);
        check("    and the document the same length", tb_ymax(&tb), ymax);
        tb_destroy(&tb);
    }

    /* --- the cursor keeps moving after a refused slide --- */
    {
        /* A slide that fails must leave the buffer usable, so the ordinary way
         * of moving through a document still works afterwards. */
        check("a paged document to move through", open_paged(), 1);
        tb_pos mid = { 2000, 0 };
        tb_seek(&tb, mid);

        stub_file_short_write(0);
        for (int i = 0; i < 40; i++) {
            tb_down(&tb);
        }
        for (int i = 0; i < 80; i++) {
            tb_up(&tb);
        }
        stub_file_short_write(-1);

        check("  the index still describes the text", agrees(&tb), 1);
        check("    and the cursor is on a line that reads",
              tb_curr_line(&tb).ssz_ >= 0 ? 1 : 0, 1);

        /* And with the card working again, it can reach both ends. */
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);
        check("  and once the card takes writes again, the top", tb_ypos(&tb), 1);
        check("    reading the first line",
              memcmp(tb_curr_line(&tb).suffix_, "line 000000", 11) == 0 ? 1 : 0, 1);
        check("    with the index still agreeing", agrees(&tb), 1);
        tb_destroy(&tb);
    }

    /* --- opening a document the card will not give --- */
    {
        stub_file_reset();
        stub_file_set_content(DOC, LINES * LINE_LEN);
        stub_file_fail_open(1);
        const bool ok = tb_init(&tb, 48, "/big.txt") != NULL;
        stub_file_fail_open(0);
        check("a document that will not open says so", ok ? 1 : 0, 0);
    }

    /* --- saving to a card that takes nothing --- */
    {
        check("a paged document to save", open_paged(), 1);

        /* Edited first, so there is something to lose. A document that was
         * never changed is already clean and would say so whatever the save
         * did, which proves nothing. */
        tb_pos at = { 10, 0 };
        tb_seek(&tb, at);
        tb_put(&tb, 'X');
        check("  edited, so it is dirty", tb_changed(&tb) ? 1 : 0, 1);

        tb_set_fname(&tb, "/out.txt", 8);
        stub_file_short_write(0);
        const bool saved = tb_save(&tb);
        stub_file_short_write(-1);
        check("  a save that cannot write says so", saved ? 1 : 0, 0);
        check("    and the document is still there", tb_ymax(&tb), LINES + 1);
        check("    and still readable", agrees(&tb), 1);
        check("    and still knows it is unsaved -- the editor would"
              " otherwise say it was written", tb_changed(&tb) ? 1 : 0, 1);

        /* And with the card working, the save goes through and it is clean. */
        check("  saving again once the card takes writes",
              tb_save(&tb) ? 1 : 0, 1);
        check("    leaves it clean", tb_changed(&tb) ? 1 : 0, 0);
        tb_destroy(&tb);
    }

    /* --- and the same for a document small enough to sit in memory --- */
    {
        /*
         * tb_save has two halves and they fail separately: a paged document
         * goes out through tb_save_paged and returns before the code below it
         * is reached. Testing only the paged one leaves the ordinary case --
         * every document small enough to fit, which is most of them -- with
         * its failure path unrun.
         */
        static const char SMALL[] = "alpha\r\nbeta\r\ngamma\r\n";
        stub_file_reset();
        stub_file_set_content(SMALL, (int) sizeof(SMALL) - 1);
        check("a document that fits in memory", tb_init(&tb, 8, "/s.txt") != NULL, 1);
        check("  and does not page", tb.paged_ ? 1 : 0, 0);

        tb_pos at = { 1, 0 };
        tb_seek(&tb, at);
        tb_put(&tb, 'X');
        check("  edited, so it is dirty", tb_changed(&tb) ? 1 : 0, 1);

        tb_set_fname(&tb, "/small.txt", 10);
        stub_file_short_write(0);
        const bool saved = tb_save(&tb);
        stub_file_short_write(-1);
        check("  a save that cannot write says so", saved ? 1 : 0, 0);
        check("    and it still knows it is unsaved", tb_changed(&tb) ? 1 : 0, 1);

        check("  saving again once the card takes writes",
              tb_save(&tb) ? 1 : 0, 1);
        check("    leaves it clean", tb_changed(&tb) ? 1 : 0, 0);
        check("    with the edit in the file",
              saved_has(&tb, "/small.txt", "Xalpha\r\nbeta\r\ngamma\r\n"), 1);
        tb_destroy(&tb);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
