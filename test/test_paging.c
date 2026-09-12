/*
 * Host tests for line numbers that do not assume the whole document is in
 * memory.
 *
 * The line index describes the lines the character buffer is holding, and
 * nothing else. Every line number the editor deals in is "which line of the
 * document", and today those are the same question only because the document is
 * entirely in memory. head_lines_ and tail_lines_ are the difference, and they
 * are zero until there is somewhere else for a document to live.
 *
 * Which makes them hard to test: the arithmetic that matters is the arithmetic
 * that never runs. tb_set_offscreen is the seam -- it says "pretend this much of
 * the document is elsewhere" without there being an elsewhere, so the numbering
 * can be held to its contract now, while a document that really is all in memory
 * can still say what the answers should be.
 *
 * See .internal/docs/PAGING.md.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "char_buffer.h"
#include "doc_store.h"
#include "text_buffer.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* Five lines, so there is somewhere to move to. */
static const char FIVE[] = "one\r\ntwo\r\nthree\r\nfour\r\nfive\r\n";

static int load_five(text_buffer* tb) {
    stub_file_reset();
    stub_file_set_content(FIVE, (int) sizeof(FIVE) - 1);

    return tb_init(tb, 4, "five.txt") != NULL;
}

int main(void) {
    stub_discard_output();

    text_buffer tb;

    /* --- with nothing elsewhere, the numbers are what they always were --- */
    {
        check("a document loads", load_five(&tb), 1);
        /* Five breaks, so six lines: the last CRLF opens an empty one. */
        check("  six lines", tb_ymax(&tb), 6);
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);
        check("  and the top is line one", tb_ypos(&tb), 1);
        tb_destroy(&tb);
    }

    /* --- a head shifts where the cursor is, and how long the document is --- */
    {
        check("a document to put a head in front of", load_five(&tb), 1);
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);

        tb_set_offscreen(&tb, 100, 0);
        check("the first line in memory is line 101", tb_ypos(&tb), 101);
        check("  and the document is 100 lines longer", tb_ymax(&tb), 106);

        /* Moving inside memory still moves one line at a time. */
        tb_down(&tb);
        check("  down one is 102", tb_ypos(&tb), 102);
        tb_down(&tb);
        tb_down(&tb);
        check("  and three down is 104", tb_ypos(&tb), 104);
        tb_up(&tb);
        check("  up one is 103", tb_ypos(&tb), 103);

        /* tb_tell is the position everything else is expressed in. */
        check("  and tb_tell agrees with tb_ypos", tb_tell(&tb).line, tb_ypos(&tb));
        tb_destroy(&tb);
    }

    /* --- a tail lengthens the document without moving the cursor --- */
    {
        check("a document to put a tail behind", load_five(&tb), 1);
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);

        tb_set_offscreen(&tb, 0, 40);
        check("the cursor is still on line one", tb_ypos(&tb), 1);
        check("  and the document is 40 lines longer", tb_ymax(&tb), 46);

        tb_set_offscreen(&tb, 100, 40);
        check("both ends count", tb_ymax(&tb), 146);
        check("  and only the head moves the cursor", tb_ypos(&tb), 101);
        tb_destroy(&tb);
    }

    /* --- a seek is in document lines, not memory lines --- */
    {
        check("a document to seek about in", load_five(&tb), 1);
        tb_set_offscreen(&tb, 100, 40);

        tb_pos at = { 103, 0 };
        tb_seek(&tb, at);
        check("seeking to 103 lands on 103", tb_ypos(&tb), 103);
        const split_line ln = tb_curr_line(&tb);
        check("  which is the third line in memory", ln.psz_ + ln.ssz_,
              (int) strlen("three"));

        /* A line that is in the head cannot be reached, because nothing has
         * been built yet that could fetch it. It stops at the top of memory
         * rather than running away or pretending -- and this is exactly where
         * step 2 hooks the slide. */
        tb_pos in_head = { 50, 0 };
        tb_seek(&tb, in_head);
        check("a line in the head stops at the top of memory",
              tb_ypos(&tb), 101);

        tb_pos in_tail = { 200, 0 };
        tb_seek(&tb, in_tail);
        check("  and one in the tail stops at the bottom", tb_ypos(&tb), 106);
        tb_destroy(&tb);
    }

    /* --- a walker sees what the cursor sees --- */
    {
        check("a document to walk", load_five(&tb), 1);
        tb_set_offscreen(&tb, 100, 40);
        tb_pos at = { 102, 0 };
        tb_seek(&tb, at);

        text_buffer cp;
        tb_copy(&cp, &tb);
        check("a copy starts where the cursor is", tb_ypos(&cp), 102);
        check("  and sees the same document length", tb_ymax(&cp), 146);
        tb_down(&cp);
        check("  and numbers its own movement the same way", tb_ypos(&cp), 103);
        check("  without moving the cursor it came from", tb_ypos(&tb), 102);
        tb_destroy(&tb);
    }

    /* --- a walker cannot write --- */
    {
        check("a document to guard", load_five(&tb), 1);
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);
        const int lines_before = tb_ymax(&tb);
        const int used_before = tb_used(&tb);

        text_buffer cp;
        tb_copy(&cp, &tb);

        /* A copy shares the original's buffers rather than owning any of its
         * own, so a write through one is not a private change -- it is a change
         * to the document the cursor is still pointing into, made behind its
         * back. Every mutator refuses, the way running out of room does. */
        check("a walker will not take a character", tb_put(&cp, 'x') ? 1 : 0, 0);
        check("  nor a line break", tb_newline(&cp) ? 1 : 0, 0);
        check("  nor delete forwards", tb_del(&cp) ? 1 : 0, 0);
        check("  nor backwards", tb_bksp(&cp) ? 1 : 0, 0);
        check("  nor take a whole line out", tb_del_line(&cp) ? 1 : 0, 0);
        check("  nor insert a span", tb_insert(&cp, "hello", 5) ? 1 : 0, 0);
        tb_pos a = { 1, 0 };
        tb_pos b = { 3, 0 };
        check("  nor delete a range", tb_range_del(&cp, a, b) ? 1 : 0, 0);
        check("  nor save", tb_save(&cp) ? 1 : 0, 0);

        check("and the document is untouched", tb_used(&tb), used_before);
        check("  every line of it", tb_ymax(&tb), lines_before);

        /* Reading and moving is what it is for, and still works. */
        tb_down(&cp);
        check("a walker still moves", tb_ypos(&cp), 2);
        const split_line ln = tb_curr_line(&cp);
        check("  and still reads", ln.psz_ + ln.ssz_, (int) strlen("two"));
        check("  without taking the cursor with it", tb_ypos(&tb), 1);

        /* The original is not a walker and is unaffected by any of this. */
        check("the document itself still takes a character",
              tb_put(&tb, 'x') ? 1 : 0, 1);
        tb_destroy(&tb);
    }

    /* --- emptying the document empties both ends --- */
    {
        check("a document to clear", load_five(&tb), 1);
        tb_set_offscreen(&tb, 100, 40);
        tb_clear(&tb);
        check("a cleared document is one line", tb_ymax(&tb), 1);
        check("  starting at line one", tb_ypos(&tb), 1);
        tb_destroy(&tb);
    }

    /* ---------------------------------------------------------------- *
     *  Sliding: the window moves, the document does not.
     * ---------------------------------------------------------------- */

    /* A document bigger than the memory it will be read into, which is the
     * whole point -- 4,000 numbered lines of exactly forty bytes each, 160,000
     * in all, against the 63,488 that 64 KiB of buffer holds.
     *
     * Forty rather than eight, because the line index has one slot per 32 bytes
     * of buffer and a document of short lines fills it before the text fills
     * memory. That is a real limit and worth its own test, but it is not the
     * ordinary case -- slow.asm averages 57 bytes a line -- and a document that
     * hits it cannot fill memory enough to have margins on both sides. */
    #define DOC_LINES 4000
    #define DOC_LEN   40
    #define DOC_BYTES (DOC_LINES * DOC_LEN)
    #define DOC_KB    64
    static char DOC[DOC_BYTES + 1];
    {
        int at = 0;
        for (int i = 0; i < DOC_LINES; i++) {
            DOC[at++] = 'l';
            for (int d = 1000; d > 0; d /= 10) {
                DOC[at++] = (char) ('0' + ((i / d) % 10));
            }
            while (at % DOC_LEN != DOC_LEN - 2) {
                DOC[at++] = '.';
            }
            DOC[at++] = '\r';
            DOC[at++] = '\n';
        }
        DOC[at] = 0;
    }

    /* What line `i` reads as, for comparing against what is on screen. */
    static char want_line[DOC_LEN];
    #define WANT(i) (memcpy(want_line, DOC + (i) * DOC_LEN, DOC_LEN - 2), \
                     want_line[DOC_LEN - 2] = 0, want_line)

    /* The line the cursor is on, as text. */
    static char seen[64];
    #define LINE_NOW(tb) (memcpy(seen, tb_curr_line(tb).suffix_, \
                                 (size_t) tb_curr_line(tb).ssz_), \
                          seen[tb_curr_line(tb).ssz_] = 0, seen)

    /* --- a slide down moves the window along --- */
    {
        stub_file_reset();
        check("an empty document to page", tb_init(&tb, DOC_KB, NULL) != NULL, 1);
        check("  which opens a store", tb_page_open(&tb, "/doc.txt") ? 1 : 0, 1);

        /* The whole document goes to the tail, as the loader will put it. */
        check("  and takes the document",
              tb_page_fill(&tb, DOC, DOC_BYTES) ? 1 : 0, 1);
        check("  counting its lines", tb.tail_lines_, DOC_LINES);
        check("  with nothing in memory yet", tb_used(&tb), 0);
        check("  and the document is its lines plus the empty last one",
              tb_ymax(&tb), DOC_LINES + 1);

        /* Priming is not a slide: memory is empty and has nothing to send the
         * other way. It fills until there is a chunk or two of room left. */
        check("priming fills memory", tb_page_prime(&tb) ? 1 : 0, 1);
        check("  which does not fit the whole document",
              tb_used(&tb) < DOC_BYTES, 1);
        check("  so some of it is still in the tail", tb.tail_lines_ > 0, 1);
        check("  the head is still empty", store_head_bytes(tb.store_), 0);
        check("  and the document is the length it was",
              tb_ymax(&tb), DOC_LINES + 1);
        check("  with room kept back for a slide", cb_available(&tb.cb_) > 0, 1);
        tb_destroy(&tb);
    }

    /* --- down then up is the identity --- */
    {
        stub_file_reset();
        tb_init(&tb, DOC_KB, NULL);
        tb_page_open(&tb, "/doc.txt");
        tb_page_fill(&tb, DOC, DOC_BYTES);
        tb_page_prime(&tb);

        /* Put the cursor somewhere with lines on both sides of it. */
        tb_pos at = { 3, 0 };
        tb_seek(&tb, at);
        const int line_before = tb_ypos(&tb);
        const int total_before = tb_ymax(&tb);
        const int used_before = tb_used(&tb);

        check("a document with the cursor on line three", line_before, 3);
        check("  reading the third line", strcmp(LINE_NOW(&tb), WANT(2)), 0);

        check("sliding down", tb_slide_down(&tb) ? 1 : 0, 1);
        check("  the cursor is on the same line of the document",
              tb_ypos(&tb), line_before);
        check("  which still says the same thing", strcmp(LINE_NOW(&tb), WANT(2)), 0);
        check("  the document is the same length", tb_ymax(&tb), total_before);
        check("  memory did not grow", tb_used(&tb) <= used_before, 1);
        check("  and some of it is now in the head", store_head_bytes(tb.store_) > 0, 1);

        check("sliding back up", tb_slide_up(&tb) ? 1 : 0, 1);
        check("  puts the cursor back on its line", tb_ypos(&tb), line_before);
        check("    still saying the same thing", strcmp(LINE_NOW(&tb), WANT(2)), 0);
        check("  the document is still the same length", tb_ymax(&tb), total_before);
        check("  and the head is empty again", store_head_bytes(tb.store_), 0);
        tb_destroy(&tb);
    }

    /* --- a walker may not slide --- */
    {
        stub_file_reset();
        tb_init(&tb, DOC_KB, NULL);
        tb_page_open(&tb, "/doc.txt");
        tb_page_fill(&tb, DOC, DOC_BYTES);
        tb_page_prime(&tb);

        /* Somewhere a slide would actually succeed from. At the top of the
         * document there is nothing in front of the cursor to send out, so a
         * refusal there says nothing about walkers. */
        tb_pos mid = { 600, 0 };
        tb_seek(&tb, mid);

        text_buffer cp;
        tb_copy(&cp, &tb);
        const int head_before = store_head_bytes(tb.store_);

        /* The other half of pitfall 1. A walker shares the cursor's buffers,
         * so a slide through one moves the window out from under the cursor
         * that owns it -- and that cursor would be left pointing at a line
         * that is no longer in memory. */
        check("a walker will not slide down", tb_slide_down(&cp) ? 1 : 0, 0);
        check("  nor up", tb_slide_up(&cp) ? 1 : 0, 0);
        check("  and the window did not move", store_head_bytes(tb.store_),
              head_before);

        /* The buffer it was copied from can, from exactly the same position --
         * which is what says the refusal was about being a walker. */
        check("but the document itself slides from there",
              tb_slide_down(&tb) ? 1 : 0, 1);
        check("  and the window did move", store_head_bytes(tb.store_) > head_before, 1);
        tb_destroy(&tb);
    }

    /* --- sliding up from the middle of the head, not off the end of it --- */
    {
        stub_file_reset();
        tb_init(&tb, DOC_KB, NULL);
        tb_page_open(&tb, "/doc.txt");
        tb_page_fill(&tb, DOC, DOC_BYTES);
        tb_page_prime(&tb);

        tb_pos mid = { 600, 0 };
        tb_seek(&tb, mid);
        const int line_before = tb_ypos(&tb);
        const int total_before = tb_ymax(&tb);

        /* Twice, so the head holds more than one slide's worth. Sliding up
         * then takes a chunk out of the middle of the head rather than all of
         * it, and lands part way through a line -- the bytes before the first
         * break belong to a line whose start is still in the head and have to
         * go back. Sliding up when the head holds exactly one chunk never
         * exercises that, because the pop empties it and starts on a boundary. */
        check("two slides down", (tb_slide_down(&tb) && tb_slide_down(&tb)) ? 1 : 0, 1);
        check("  fill the head with more than one chunk",
              store_head_bytes(tb.store_) > TB_CHUNK, 1);

        check("sliding up out of the middle of it", tb_slide_up(&tb) ? 1 : 0, 1);
        check("  leaves the cursor on its line", tb_ypos(&tb), line_before);
        check("  reading what it always read", strcmp(LINE_NOW(&tb), WANT(599)), 0);
        check("  with the document the same length", tb_ymax(&tb), total_before);

        check("and up again", tb_slide_up(&tb) ? 1 : 0, 1);
        check("  still on its line", tb_ypos(&tb), line_before);
        check("  still reading the same", strcmp(LINE_NOW(&tb), WANT(599)), 0);
        check("  with the head empty again", store_head_bytes(tb.store_), 0);
        check("  and the document still the same length", tb_ymax(&tb), total_before);
        tb_destroy(&tb);
    }

    /* --- a seek reaches lines that are not in memory --- */
    {
        stub_file_reset();
        tb_init(&tb, DOC_KB, NULL);
        tb_page_open(&tb, "/doc.txt");
        tb_page_fill(&tb, DOC, DOC_BYTES);
        tb_page_prime(&tb);

        /* Priming leaves memory holding the first part of the document, so
         * this line is well past the end of it. Step 1 pinned that a seek
         * stopped at the edge of memory; it slides to get there now. */
        const int far = DOC_LINES - 50;
        tb_pos deep = { far, 0 };
        tb_seek(&tb, deep);
        check("a seek reaches a line that was in the tail", tb_ypos(&tb), far);
        check("  and it reads as itself", strcmp(LINE_NOW(&tb), WANT(far - 1)), 0);
        check("  the document is still its own length", tb_ymax(&tb), DOC_LINES + 1);
        check("  with most of it now in the head", store_head_bytes(tb.store_) > 0, 1);

        /* And back, which is the direction that needs the head. */
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);
        check("a seek back to the first line arrives", tb_ypos(&tb), 1);
        check("  reading the first line", strcmp(LINE_NOW(&tb), WANT(0)), 0);
        check("  with the head empty again", store_head_bytes(tb.store_), 0);
        check("  and the document the length it always was",
              tb_ymax(&tb), DOC_LINES + 1);
        tb_destroy(&tb);
    }

    /* --- settling keeps the cursor clear of both margins --- */
    {
        stub_file_reset();
        tb_init(&tb, DOC_KB, NULL);
        tb_page_open(&tb, "/doc.txt");
        tb_page_fill(&tb, DOC, DOC_BYTES);
        tb_page_prime(&tb);

        /* The margins are what make a repaint free of disk: it reads about a
         * screenful either side of the cursor, through walkers that may not
         * slide, so the text has to be there already. */
        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);
        tb_settle(&tb);

        int psz = 0;
        int ssz = 0;
        cb_prefix(&tb.cb_, &psz);
        cb_suffix(&tb.cb_, &ssz);
        /* Nothing above the first line, so that margin cannot be filled -- but
         * the one below it must be. */
        check("  leaves a margin below the cursor", ssz >= TB_MARGIN, 1);
        check("  and the cursor is still on line one", tb_ypos(&tb), 1);

        /* In the middle, both margins have to be clear. */
        tb_pos mid = { DOC_LINES / 2, 0 };
        tb_seek(&tb, mid);
        tb_settle(&tb);
        cb_prefix(&tb.cb_, &psz);
        cb_suffix(&tb.cb_, &ssz);
        check("in the middle, a margin above", psz >= TB_MARGIN, 1);
        check("  and one below", ssz >= TB_MARGIN, 1);
        check("  with the cursor where it was", tb_ypos(&tb), DOC_LINES / 2);
        check("  reading its own line",
              strcmp(LINE_NOW(&tb), WANT(DOC_LINES / 2 - 1)), 0);

        check("settling again has nothing to do", tb_settle(&tb) ? 1 : 0, 0);
        tb_destroy(&tb);
    }

    /* --- every line of the document reads as itself, whatever slid --- */
    {
        stub_file_reset();
        tb_init(&tb, DOC_KB, NULL);
        tb_page_open(&tb, "/doc.txt");
        tb_page_fill(&tb, DOC, DOC_BYTES);
        tb_page_prime(&tb);

        /* The check the rest of this file is really for. Walk the whole
         * document a line at a time, which slides many times in both
         * directions, and every line has to read as the line it is.
         *
         * Two bugs hid from everything narrower than this. A slide would ship
         * the index's in-progress entry to the head as a line with no bytes,
         * so the head counted one more line than it held and every line number
         * past that point was wrong. And when the cursor was sitting on that
         * entry there was nothing after it to take off, so arriving lines were
         * appended behind it and it was stranded in the middle of the
         * document. Both only show up after enough sliding. */
        int wrong = 0;
        for (int n = 1; n <= DOC_LINES && wrong == 0; n++) {
            tb_pos p = { n, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            if (tb_ypos(&tb) != n || ln.ssz_ != DOC_LEN - 2
                    || memcmp(ln.suffix_, DOC + (n - 1) * DOC_LEN,
                              (size_t)(DOC_LEN - 2)) != 0) {
                wrong = n;
            }
        }
        check("every line down the document reads as itself", wrong, 0);

        /* And back up, which is the direction that has to fetch from the head
         * and get the partial line at its end right. */
        for (int n = DOC_LINES; n >= 1 && wrong == 0; n--) {
            tb_pos p = { n, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            if (tb_ypos(&tb) != n || ln.ssz_ != DOC_LEN - 2
                    || memcmp(ln.suffix_, DOC + (n - 1) * DOC_LEN,
                              (size_t)(DOC_LEN - 2)) != 0) {
                wrong = n;
            }
        }
        check("  and every line back up it", wrong, 0);
        check("  with the document the length it started", tb_ymax(&tb),
              DOC_LINES + 1);

        /* The head and the counters agree, which is what says no line was
         * counted without its bytes. */
        check("  and the head holds exactly the lines it is credited with",
              store_head_bytes(tb.store_), tb.head_lines_ * DOC_LEN);
        tb_destroy(&tb);
    }

    /* --- a document of short lines fills the index before the buffer --- */
    {
        /* The index has one slot per 32 bytes of buffer. A document whose lines
         * are shorter than that runs out of slots while memory still has room,
         * and everything has to cope: priming stops early, and a slide can only
         * take in as many lines as there are slots free.
         *
         * slow.asm averages 57 bytes a line so the buffer binds there, but a
         * file of short lines is an ordinary enough thing -- a column of
         * numbers, a list of labels -- and without the clamp a slide puts bytes
         * in memory that no entry describes. */
        #define SHORT_LINES 6000
        #define SHORT_LEN   4
        static char SHORT[SHORT_LINES * SHORT_LEN + 1];
        for (int i = 0; i < SHORT_LINES; i++) {
            SHORT[i * SHORT_LEN + 0] = (char) ('a' + (i % 26));
            SHORT[i * SHORT_LEN + 1] = (char) ('a' + (i / 26) % 26);
            SHORT[i * SHORT_LEN + 2] = '\r';
            SHORT[i * SHORT_LEN + 3] = '\n';
        }

        stub_file_reset();
        tb_init(&tb, DOC_KB, NULL);
        tb_page_open(&tb, "/short.txt");
        tb_page_fill(&tb, SHORT, SHORT_LINES * SHORT_LEN);
        tb_page_prime(&tb);

        check("priming stops when the index is full, not the buffer",
              cb_available(&tb.cb_) > TB_CHUNK * 2, 1);
        check("  with room left in memory it cannot use", tb_used(&tb) < SHORT_LINES * SHORT_LEN, 1);
        check("  and the document still its own length", tb_ymax(&tb), SHORT_LINES + 1);

        /* Walking it has to work anyway, which is the whole point. */
        int wrong = 0;
        for (int n = 1; n <= SHORT_LINES && wrong == 0; n++) {
            tb_pos p = { n, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            if (tb_ypos(&tb) != n || ln.ssz_ != SHORT_LEN - 2
                    || memcmp(ln.suffix_, SHORT + (n - 1) * SHORT_LEN,
                              (size_t)(SHORT_LEN - 2)) != 0) {
                wrong = n;
            }
        }
        check("every short line reads as itself", wrong, 0);
        check("  and the head holds the lines it is credited with",
              store_head_bytes(tb.store_), tb.head_lines_ * SHORT_LEN);
        tb_destroy(&tb);
    }

    /* --- lines that get shorter as the document goes on --- */
    {
        /* The case the uniform documents above cannot reach. A slide sends out
         * whole lines and brings in a chunk's worth; if what arrives is shorter
         * than what left, it is *more* lines than the slots just freed, and the
         * index runs out part way through. Long lines first and short ones
         * after is an ordinary shape -- a header, then a column of values.
         *
         * Without the clamp, the index refuses the entries while the bytes go
         * into memory regardless, and from there the document reads as
         * gibberish. */
        #define MIX_LONG   64
        #define MIX_SHORT  4
        #define MIX_N      1500
        static char MIX[MIX_N * MIX_LONG + MIX_N * MIX_SHORT + 1];
        int mix_at = 0;
        for (int i = 0; i < MIX_N; i++) {
            const int start = mix_at;
            MIX[mix_at++] = 'L';
            while (mix_at < start + MIX_LONG - 2) {
                MIX[mix_at++] = (char) ('a' + (i % 26));
            }
            MIX[mix_at++] = '\r';
            MIX[mix_at++] = '\n';
        }
        for (int i = 0; i < MIX_N; i++) {
            MIX[mix_at++] = 's';
            MIX[mix_at++] = (char) ('a' + (i % 26));
            MIX[mix_at++] = '\r';
            MIX[mix_at++] = '\n';
        }

        stub_file_reset();
        tb_init(&tb, DOC_KB, NULL);
        tb_page_open(&tb, "/mixed.txt");
        tb_page_fill(&tb, MIX, mix_at);
        tb_page_prime(&tb);
        check("a document of long lines then short ones",
              tb_ymax(&tb), MIX_N * 2 + 1);

        int wrong = 0;
        for (int n = 1; n <= MIX_N * 2 && wrong == 0; n++) {
            const int want_len = n <= MIX_N ? MIX_LONG - 2 : MIX_SHORT - 2;
            const int off = n <= MIX_N ? (n - 1) * MIX_LONG
                                       : MIX_N * MIX_LONG + (n - 1 - MIX_N) * MIX_SHORT;
            tb_pos p = { n, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            if (tb_ypos(&tb) != n || ln.ssz_ != want_len
                    || memcmp(ln.suffix_, MIX + off, (size_t) want_len) != 0) {
                wrong = n;
            }
        }
        check("  reads correctly across the change of length", wrong, 0);
        check("  and is still the length it was", tb_ymax(&tb), MIX_N * 2 + 1);
        tb_destroy(&tb);
    }

    /* --- loading a file too big for memory --- */
    {
        /* The point of all of it. A document larger than the buffer used to be
         * refused with "file too large"; it opens now, with the store holding
         * what memory cannot. */
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("a document larger than memory opens",
              tb_init(&tb, DOC_KB, "/big.txt") != NULL, 1);
        check("  with all of its lines", tb_ymax(&tb), DOC_LINES + 1);
        check("  but not all of it in memory", tb_used(&tb) < DOC_BYTES, 1);
        check("  and the rest in the store", store_tail_bytes(tb.store_) > 0, 1);
        check("  reading its first line", strcmp(LINE_NOW(&tb), WANT(0)), 0);

        /* Including the far end, which is only reachable by sliding. */
        tb_pos last = { DOC_LINES, 0 };
        tb_seek(&tb, last);
        check("  and its last", strcmp(LINE_NOW(&tb), WANT(DOC_LINES - 1)), 0);
        check("  at the line it should be", tb_ypos(&tb), DOC_LINES);
        tb_destroy(&tb);
    }

    /* --- the load does not open the tail once per chunk --- */
    {
        /* Appending opens and closes around itself, which is right for a
         * slide and wrong for a load: the loader appends a chunk at a time,
         * so the opens come out at one per 2 KiB of document. On hardware
         * that was most of a 12.4 s open on a 419 KiB file.
         *
         * What makes it a bug is that it scales, so that is what is measured:
         * open a document, then open one twice as long, and the number of
         * files opened for writing has to be the same both times. Counting
         * against a fixed number would pass just as well with the holding
         * taken out, as long as the document were small enough. */
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES / 2);
        check("a paged document opens", tb_init(&tb, DOC_KB, "/half.txt") != NULL, 1);
        const int half_opens = stub_file_opens_for_write();
        tb_destroy(&tb);

        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("  and one twice as long", tb_init(&tb, DOC_KB, "/big.txt") != NULL, 1);
        const int full_opens = stub_file_opens_for_write();
        tb_destroy(&tb);

        check("  which pages, so the loader really did chunk",
              DOC_BYTES > DOC_KB * 1024, 1);
        check("  opening the tail the same number of times either way",
              full_opens, half_opens);
    }

    /* --- a file of bare line feeds is normalised on the way in --- */
    {
        /* The loader converts as it streams, a chunk at a time, and the one
         * piece of state that cannot live inside a chunk is whether the last
         * byte of the one before was a carriage return. Get that wrong and
         * every break landing on a chunk boundary gains a second one. */
        #define LF_LINES 5000
        static char LF[LF_LINES * 20 + 1];
        int lf_at = 0;
        for (int i = 0; i < LF_LINES; i++) {
            for (int k = 0; k < 19; k++) {
                LF[lf_at++] = (char) ('a' + ((i + k) % 26));
            }
            LF[lf_at++] = '\n';        // bare, no carriage return
        }
        stub_file_reset();
        stub_file_set_content(LF, lf_at);
        check("a bare-LF document opens", tb_init(&tb, DOC_KB, "/lf.txt") != NULL, 1);
        check("  with the lines it has, not twice as many",
              tb_ymax(&tb), LF_LINES + 1);
        check("  and opens clean, because saving it changes nothing",
              tb_changed(&tb) ? 1 : 0, 0);

        int wrong = 0;
        for (int n = 1; n <= LF_LINES && wrong == 0; n++) {
            tb_pos p = { n, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            if (ln.ssz_ != 19 || memcmp(ln.suffix_, LF + (n - 1) * 20, 19) != 0) {
                wrong = n;
            }
        }
        check("  every line of it reads as itself", wrong, 0);
        tb_destroy(&tb);
    }

    /* --- a CRLF split across a chunk boundary --- */
    {
        /* Lines of three bytes, "a\r\n", so that byte 2047 is a carriage
         * return and byte 2048 a line feed: the loader's first chunk ends on
         * one half of a break and the second begins with the other.
         *
         * Whether the last byte of the previous chunk was a carriage return is
         * the only state the conversion carries across chunks. Losing it makes
         * the loader treat that line feed as bare and give it a second
         * carriage return, so the document gains a line at every chunk
         * boundary and the text gains a byte. */
        #define CR_LINES 12000
        static char CR[CR_LINES * 3 + 1];
        for (int i = 0; i < CR_LINES; i++) {
            CR[i * 3 + 0] = (char) ('a' + (i % 26));
            CR[i * 3 + 1] = '\r';
            CR[i * 3 + 2] = '\n';
        }
        /* 36,000 bytes into 32 KiB of buffer, so it really does page -- at
         * DOC_KB it would fit in memory and never go near the loader's
         * chunking. */
        stub_file_reset();
        stub_file_set_content(CR, CR_LINES * 3);
        check("a CRLF document opens", tb_init(&tb, 32, "/cr.txt") != NULL, 1);
        check("  with exactly its own lines", tb_ymax(&tb), CR_LINES + 1);
        check("  and nothing added to it", tb_changed(&tb) ? 1 : 0, 0);

        /* The lines either side of each chunk boundary, which is where a lost
         * carry shows up: 2048 / 3 falls between the carriage return of line
         * 683 and its line feed. */
        int wrong = 0;
        for (int n = 680; n <= 690 && wrong == 0; n++) {
            tb_pos p = { n, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            if (tb_ypos(&tb) != n || ln.ssz_ != 1
                    || ln.suffix_[0] != (char) ('a' + ((n - 1) % 26))) {
                wrong = n;
            }
        }
        check("  and the lines around a chunk boundary read as themselves",
              wrong, 0);
        tb_destroy(&tb);
    }

    /* --- a buffer smaller than a chunk --- */
    {
        /* Nothing stops memory being smaller than the chunk a slide moves, and
         * then a pop of a whole chunk cannot be given to it -- the filling
         * would stop before it started, leaving a document that opened with
         * nothing in it. The pop is capped to what will fit. */
        #define WIDE_LINES 200
        static char WIDE[WIDE_LINES * 100 + 1];
        for (int i = 0; i < WIDE_LINES; i++) {
            for (int k = 0; k < 98; k++) {
                WIDE[i * 100 + k] = (char) ('a' + ((i + k) % 26));
            }
            WIDE[i * 100 + 98] = '\r';
            WIDE[i * 100 + 99] = '\n';
        }
        stub_file_reset();
        stub_file_set_content(WIDE, WIDE_LINES * 100);
        check("a document opens into a buffer smaller than a chunk",
              tb_init(&tb, 1, "/wide.txt") != NULL, 1);
        check("  with something actually in memory", tb_used(&tb) > 0, 1);
        check("  and its first line readable", tb_curr_line(&tb).ssz_, 98);
        check("  counting all of its lines", tb_ymax(&tb), WIDE_LINES + 1);
        tb_destroy(&tb);
    }

    /* --- the load does not write memory's share out and read it back --- */
    {
        /* The loader used to hand every chunk to the tail and then let
         * priming pull the window back out of it. That is a buffer's worth of
         * writing and the same again of reading, for text that had just been
         * in memory -- half a megabyte of card traffic on a 419 KiB file.
         *
         * Memory is filled off the read instead, and only what will not fit
         * goes to the tail. The tail file keeps everything ever appended --
         * popping moves a mark, it does not shorten the file -- so its length
         * past the headroom is exactly what the load wrote, and what the load
         * wrote plus what memory took has to be the whole document. */
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("a paged document to load", tb_init(&tb, DOC_KB, "/big.txt") != NULL, 1);

        int tail_len = 0;
        stub_file_content("/big.txt" STORE_TAIL_SUFFIX, &tail_len);
        const int written = tail_len - STORE_HEADROOM;

        check("  memory is holding a window of it", tb_used(&tb) > 0, 1);
        check("  the load wrote the rest of it to the tail, and no more",
              written + tb_used(&tb), DOC_BYTES);
        check("  which is less than the whole document", written < DOC_BYTES, 1);
        tb_destroy(&tb);
    }

    /* --- the load does not open the tail once per chunk --- */
    {
        /* Appending opens and closes around itself, which is right for a
         * slide and wrong for a load: the loader appends a chunk at a time,
         * so the opens come out at one per 2 KiB of document. On hardware
         * that was most of a 12.4 s open on a 419 KiB file.
         *
         * What makes it a bug is that it scales, so that is what is measured:
         * open a document, then open one twice as long, and the number of
         * files opened for writing has to be the same both times. Counting
         * against a fixed number would pass just as well with the holding
         * taken out, as long as the document were small enough. */
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES / 2);
        check("a paged document opens", tb_init(&tb, DOC_KB, "/half.txt") != NULL, 1);
        const int half_opens = stub_file_opens_for_write();
        tb_destroy(&tb);

        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("  and one twice as long", tb_init(&tb, DOC_KB, "/big.txt") != NULL, 1);
        const int full_opens = stub_file_opens_for_write();
        tb_destroy(&tb);

        check("  which pages, so the loader really did chunk",
              DOC_BYTES > DOC_KB * 1024, 1);
        check("  opening the tail the same number of times either way",
              full_opens, half_opens);
    }

    /* --- down to the end, back to the top, and then saved --- */
    {
        /* What the hardware harness does to slow.asm, which is the only thing
         * that has ever found a paging bug first. Saving from the middle, as
         * the test below does, leaves the store holding roughly what the load
         * put there. Walking to the bottom empties the tail into the head and
         * walking back fills it again from the other side -- every byte of the
         * document goes through both files and through memory, and the save
         * afterwards is the only thing that says whether it all came back in
         * the right order.
         *
         * Bare line feeds, because that is what slow.asm has, and the
         * conversion is one more thing for a slide to get wrong. */
        #define TRIP_LINES 5000
        #define TRIP_LEN   20
        static char TRIP[TRIP_LINES * TRIP_LEN + 1];
        for (int i = 0; i < TRIP_LINES; i++) {
            for (int k = 0; k < TRIP_LEN - 1; k++) {
                TRIP[i * TRIP_LEN + k] = (char) ('a' + ((i + k) % 26));
            }
            TRIP[i * TRIP_LEN + TRIP_LEN - 1] = '\n';
        }
        stub_file_reset();
        stub_file_set_content(TRIP, TRIP_LINES * TRIP_LEN);
        check("a document to walk end to end", tb_init(&tb, DOC_KB, "/trip.txt") != NULL, 1);

        tb_pos bottom = { TRIP_LINES, 0 };
        tb_seek(&tb, bottom);
        check("  reaches its last line", tb_ypos(&tb), TRIP_LINES);
        check("    with the head holding the rest", store_head_bytes(tb.store_) > 0, 1);

        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);
        check("  and comes back to the first", tb_ypos(&tb), 1);
        check("    with the tail holding the rest", store_tail_bytes(tb.store_) > 0, 1);

        check("  saving after the round trip works", tb_save(&tb) ? 1 : 0, 1);
        int saved_len = 0;
        const char* saved = stub_file_content("/trip.txt", &saved_len);
        check("  and gives back every byte", saved_len, TRIP_LINES * TRIP_LEN);
        int at = -1;
        if (saved != NULL) {
            for (int i = 0; i < saved_len && i < TRIP_LINES * TRIP_LEN; i++) {
                if (saved[i] != TRIP[i]) { at = i; break; }
            }
        }
        check("    in the order they went in", at, -1);
        tb_destroy(&tb);
    }

    /* --- a document that runs out while memory is still filling --- */
    {
        /* Memory takes whole lines and holds the half line at the end of a
         * chunk over for the next one. When the buffer is small enough that
         * the whole document arrives in a single chunk, there is no next one:
         * the read ends with that half line held and nothing has been written
         * anywhere. Dropping it loses the end of the document silently --
         * the lines are all counted, because counting happens on the way in,
         * and the text simply stops.
         *
         * 1,100 bytes into a 1 KiB buffer is the shape: over the buffer, so
         * it pages, and under a chunk, so the loader reads it all at once. */
        #define TINY_LINES 110
        static char TINY[TINY_LINES * 10 + 1];
        for (int i = 0; i < TINY_LINES; i++) {
            for (int k = 0; k < 8; k++) {
                TINY[i * 10 + k] = (char) ('a' + ((i + k) % 26));
            }
            TINY[i * 10 + 8] = '\r';
            TINY[i * 10 + 9] = '\n';
        }
        stub_file_reset();
        stub_file_set_content(TINY, TINY_LINES * 10);
        check("a document barely over a small buffer opens",
              tb_init(&tb, 1, "/tiny.txt") != NULL, 1);
        check("  with all of its lines", tb_ymax(&tb), TINY_LINES + 1);

        int wrong = 0;
        for (int n = 1; n <= TINY_LINES && wrong == 0; n++) {
            tb_pos p = { n, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            if (tb_ypos(&tb) != n || ln.ssz_ != 8
                    || memcmp(ln.suffix_, TINY + (n - 1) * 10, 8) != 0) {
                wrong = n;
            }
        }
        check("  and every one of them reads as itself", wrong, 0);

        /* And it is all still there to save: the half line the loader was
         * holding is part of the document like any other. */
        check("  saving it works", tb_save(&tb) ? 1 : 0, 1);
        int saved_len = 0;
        const char* saved = stub_file_content("/tiny.txt", &saved_len);
        check("  and gives back every byte", saved_len, TINY_LINES * 10);
        check("    unchanged",
              saved != NULL && memcmp(saved, TINY, (size_t) (TINY_LINES * 10)) == 0, 1);
        tb_destroy(&tb);
    }

    /* --- a line longer than the loader holds over --- */
    {
        /* The half line held between chunks lives at the front of the
         * loader's conversion buffer, in front of the chunk being converted,
         * so it has a ceiling: a line longer than one chunk cannot be held
         * over without running the buffer off its end. Past that the loader
         * gives up on filling memory and sends the rest to the tail, which is
         * slower and entirely correct.
         *
         * A 3 KiB line is over the ceiling, and over what a slide can move as
         * well, so the line itself cannot be brought into memory -- that is
         * the older limit the filling comment in tb_page_prime describes, and
         * it is not this one's to fix. What is this one's is that the loader
         * neither overruns its buffer getting there nor loses any of the
         * document: it opens, it counts its lines, and it saves byte for
         * byte. Under a sanitiser the overrun is what this catches. */
        #define LONG_N   6000
        #define LONG_LEN 3000
        static char LONG[LONG_N + LONG_LEN + 2 + 1];
        int long_at = 0;
        for (int i = 0; i < LONG_N / 10; i++) {
            for (int k = 0; k < 8; k++) {
                LONG[long_at++] = (char) ('a' + ((i + k) % 26));
            }
            LONG[long_at++] = '\r';
            LONG[long_at++] = '\n';
        }
        for (int k = 0; k < LONG_LEN; k++) {
            LONG[long_at++] = (char) ('A' + (k % 26));
        }
        LONG[long_at++] = '\r';
        LONG[long_at++] = '\n';

        stub_file_reset();
        stub_file_set_content(LONG, long_at);
        check("a document with a line longer than a chunk opens",
              tb_init(&tb, 4, "/long.txt") != NULL, 1);
        check("  with all of its lines", tb_ymax(&tb), LONG_N / 10 + 2);

        /* Every line up to the long one, which is all of them the loader had
         * a half of at some point. */
        int wrong = 0;
        for (int n = 1; n <= LONG_N / 10 && wrong == 0; n++) {
            tb_pos p = { n, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            if (tb_ypos(&tb) != n || ln.ssz_ != 8
                    || memcmp(ln.suffix_, LONG + (n - 1) * 10, 8) != 0) {
                wrong = n;
            }
        }
        check("  and the lines before it read as themselves", wrong, 0);

        check("  saving it works", tb_save(&tb) ? 1 : 0, 1);
        int saved_len = 0;
        const char* saved = stub_file_content("/long.txt", &saved_len);
        check("  and gives back every byte", saved_len, long_at);
        check("    unchanged",
              saved != NULL && memcmp(saved, LONG, (size_t) long_at) == 0, 1);
        tb_destroy(&tb);
    }

    /* --- a range that reaches outside the window --- */
    {
        /* Select-all copy on a paged document used to come back with the
         * window and nothing else: 59,324 bytes of 160,000 on this one, 37%,
         * with tb_range_size agreeing with it the whole way. Both run on a
         * tb_copy walker, and a walker cannot slide, so the walk stopped at
         * the bottom of memory and reported success.
         *
         * A range with an end outside memory streams the document instead --
         * head, then memory, then tail -- and counts lines as it goes. */
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("a paged document to select all of",
              tb_init(&tb, DOC_KB, "/all.txt") != NULL, 1);
        check("  which really is paged", tb_used(&tb) < DOC_BYTES, 1);

        const tb_pos first = { 1, 0 };
        const tb_pos last = { DOC_LINES + 1, 0 };
        check("  the whole of it measures as the whole of it",
              tb_range_size(&tb, first, last), DOC_BYTES);

        static char_buffer out;
        check("  somewhere to put it", cb_init(&out, DOC_BYTES + 16) != NULL, 1);
        check("  copying it gives every byte",
              tb_range_copy(&tb, first, last, &out), DOC_BYTES);

        int got = 0;
        const char* txt = cb_prefix(&out, &got);
        check("    and they are the document's bytes",
              txt != NULL && got == DOC_BYTES
              && memcmp(txt, DOC, (size_t) DOC_BYTES) == 0, 1);

        /* Backwards, because a caller may hand the ends over either way. */
        check("  and the ends may be given in either order",
              tb_range_size(&tb, last, first), DOC_BYTES);

        /* A range that starts inside the window and ends past it: the case
         * that used to return a prefix of itself. */
        const tb_pos mid = { DOC_LINES / 2, 0 };
        const int want = (DOC_LINES / 2 + 1) * DOC_LEN;
        check("  a range from the middle to the end",
              tb_range_size(&tb, mid, last), want);
        check("    copies whole", tb_range_copy(&tb, mid, last, &out), want);
        txt = cb_prefix(&out, &got);
        check("      and reads as the document does",
              txt != NULL && memcmp(txt, DOC + (DOC_LINES / 2 - 1) * DOC_LEN,
                                    (size_t) want) == 0, 1);

        /* Ending part way along a line, past the window. Both bounds matter
         * and neither is exercised by a range that ends on a line start: the
         * column decides where the last line is cut, and the break that ends
         * that line is the one thing outside the range on it. */
        const tb_pos part = { DOC_LINES - 2, 7 };
        const int part_want = (DOC_LINES - 3) * DOC_LEN + 7;
        check("  a range ending part way along a line past the window",
              tb_range_size(&tb, first, part), part_want);
        check("    copies to exactly there",
              tb_range_copy(&tb, first, part, &out), part_want);
        txt = cb_prefix(&out, &got);
        check("      and reads as the document does",
              txt != NULL && memcmp(txt, DOC, (size_t) part_want) == 0, 1);

        /* Stopping early is worth testing and is not free to test: the stream
         * reads a chunk at a time and opens the file for each, so a range that
         * ends near the top of the document must cost fewer opens than one
         * that runs to the bottom. Without the early exit both read all of it.
         *
         * The window has to be somewhere else for either range to stream at
         * all -- with it at the top, a range over the first few lines is
         * answered out of memory and costs nothing whatever this does. */
        tb_seek(&tb, last);
        check("  with the window at the bottom", store_head_bytes(tb.store_) > 0, 1);

        const tb_pos early = { 3, 0 };
        stub_file_reset_counts();
        tb_range_size(&tb, first, early);
        const int short_opens = stub_file_opens();
        stub_file_reset_counts();
        tb_range_size(&tb, first, last);
        const int long_opens = stub_file_opens();
        check("    a range near the top reads less than one to the bottom",
              short_opens < long_opens, 1);
        check("      and still measures right", tb_range_size(&tb, first, early),
              2 * DOC_LEN);
        tb_seek(&tb, first);

        /* One entirely inside the window still goes the short way and still
         * gives the same answer. */
        const tb_pos p1 = { 2, 3 };
        const tb_pos p2 = { 4, 5 };
        check("  a range inside the window is unchanged",
              tb_range_size(&tb, p1, p2), DOC_LEN - 3 + DOC_LEN + 5);

        cb_destroy(&out);
        tb_destroy(&tb);
    }

    /* --- finding something that is not in the window --- */
    {
        /* tb_find walks a line at a time on a tb_copy, and a walker cannot
         * slide, so it searched the window and stopped. A match a hundred
         * lines from the end of a paged document reported "not found", in the
         * same words a real absence uses.
         *
         * A needle planted in three places -- near the top, in the middle and
         * near the end -- so that forwards, backwards and both wraps have
         * somewhere to land. */
        #define NEEDLE "wombat"
        #define N_LO   10
        #define N_MID  (DOC_LINES / 2)
        #define N_HI   (DOC_LINES - 100)
        static char FIND[DOC_BYTES + 1];
        memcpy(FIND, DOC, DOC_BYTES);
        const int NAT = 4;      /* the column the needle sits at */
        memcpy(FIND + (N_LO - 1) * DOC_LEN + NAT, NEEDLE, sizeof(NEEDLE) - 1);
        memcpy(FIND + (N_MID - 1) * DOC_LEN + NAT, NEEDLE, sizeof(NEEDLE) - 1);
        memcpy(FIND + (N_HI - 1) * DOC_LEN + NAT, NEEDLE, sizeof(NEEDLE) - 1);

        stub_file_reset();
        stub_file_set_content(FIND, DOC_BYTES);
        check("a paged document to search", tb_init(&tb, DOC_KB, "/find.txt") != NULL, 1);
        check("  which really is paged", tb_used(&tb) < DOC_BYTES, 1);

        const int nsz = (int) sizeof(NEEDLE) - 1;
        tb_pos hit = { 0, 0 };
        const tb_pos top = { 1, 0 };

        check("forwards from the top finds the first",
              tb_find(&tb, NEEDLE, nsz, top, true, &hit) ? 1 : 0, 1);
        check("  on the line it was put on", hit.line, N_LO);
        check("  at the column it was put at", hit.x, NAT);

        /* The next one along is outside the window, which is the whole point. */
        tb_pos after = { N_LO, NAT + 1 };
        check("forwards again finds the middle one",
              tb_find(&tb, NEEDLE, nsz, after, true, &hit) ? 1 : 0, 1);
        check("  which is in the middle of the document", hit.line, N_MID);

        after.line = N_MID;
        after.x = NAT + 1;
        check("and again finds the last",
              tb_find(&tb, NEEDLE, nsz, after, true, &hit) ? 1 : 0, 1);
        check("  near the end of the document", hit.line, N_HI);

        after.line = N_HI;
        after.x = NAT + 1;
        check("and again wraps round to the first",
              tb_find(&tb, NEEDLE, nsz, after, true, &hit) ? 1 : 0, 1);
        check("  back at the top", hit.line, N_LO);

        /* Backwards, which keeps the last match at or before `from`. */
        tb_pos before = { N_HI, NAT - 1 };
        check("backwards from the last finds the middle one",
              tb_find(&tb, NEEDLE, nsz, before, false, &hit) ? 1 : 0, 1);
        check("  in the middle", hit.line, N_MID);

        before.line = N_LO;
        before.x = -1;      /* column 0: nothing on this line is behind it */
        check("backwards from the first wraps to the last",
              tb_find(&tb, NEEDLE, nsz, before, false, &hit) ? 1 : 0, 1);
        check("  near the end", hit.line, N_HI);

        /* Case folding, and a needle that is not there at all. */
        check("the search folds case",
              tb_find(&tb, "WOMBAT", nsz, top, true, &hit) ? 1 : 0, 1);
        check("  landing on the same line", hit.line, N_LO);
        check("something absent is absent",
              tb_find(&tb, "aardvark", 8, top, true, &hit) ? 1 : 0, 0);

        /* A needle that would only match across a break must not. The
         * document's lines end "...\r\n" and the next begins "l", so the two
         * bytes either side of a break spell something that is never a line. */
        /* A needle that would only match across a break must not, and the
         * needle has to be one the stream could actually run into: a break's
         * own bytes are consumed as a break and never offered to the matcher,
         * so "\r\n" in a needle proves nothing. Every line here ends in a dot
         * and the next begins with an l, so ".l" exists only across one -- and
         * a partial match left standing at a break is what finds it. */
        check("a match may not span a line break",
              tb_find(&tb, ".l", 2, top, true, &hit) ? 1 : 0, 0);

        /* Overlapping matches, which is where a search that restarts from
         * nothing after a hit loses one, and where the needle's own repetition
         * has to be accounted for to find either. "aabaa" sits at both ends of
         * "aabaabaa". */
        #define N_OV 20
        memcpy(FIND + (N_OV - 1) * DOC_LEN + NAT, "aabaabaa", 8);

        /* And a decoy on the next line: "aababaa" does not contain "aabaa",
         * but a matcher that backtracks to the wrong place after the mismatch
         * at "aabab" thinks it does. Nothing else here mismatches part way
         * through a match, so without this the needle's own repetition is
         * never actually needed to get the right answer. */
        #define N_DECOY (N_OV + 1)
        memcpy(FIND + (N_DECOY - 1) * DOC_LEN + NAT, "aababaa", 7);
        stub_file_reset();
        stub_file_set_content(FIND, DOC_BYTES);
        tb_destroy(&tb);
        check("a document with a repetitive line",
              tb_init(&tb, DOC_KB, "/ov.txt") != NULL, 1);
        check("  the first of two overlapping matches",
              tb_find(&tb, "aabaa", 5, top, true, &hit) ? 1 : 0, 1);
        check("    on its line", hit.line, N_OV);
        check("    at its column", hit.x, NAT);
        tb_pos over = { N_OV, NAT + 1 };
        check("  and the second, which overlaps it",
              tb_find(&tb, "aabaa", 5, over, true, &hit) ? 1 : 0, 1);
        check("    three along from the first", hit.x, NAT + 3);

        over.x = NAT + 4;
        check("  and there is no third",
              tb_find(&tb, "aabaa", 5, over, true, &hit) ? 1 : 0, 1);
        check("    so it wraps back to the first", hit.line, N_OV);
        check("      at its column", hit.x, NAT);
        tb_destroy(&tb);
    }

    /* --- deleting a range that reaches outside the window --- */
    {
        /* Cut is copy and then delete. With copy streaming, a cut that its
         * delete cannot finish is worse than one that never worked: the
         * clipboard holds the whole selection and the document keeps most of
         * it. tb_range_del deletes a character at a time through the same
         * primitives the DELETE key uses, and nothing refills memory while it
         * runs, so it stops at the bottom of the window. */
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("a paged document to cut from",
              tb_init(&tb, DOC_KB, "/cut.txt") != NULL, 1);

        const tb_pos first = { 1, 0 };
        const tb_pos last = { DOC_LINES + 1, 0 };
        check("  select-all measures as the whole document",
              tb_range_size(&tb, first, last), DOC_BYTES);
        check("  deleting it works", tb_range_del(&tb, first, last) ? 1 : 0, 1);
        check("    and leaves one empty line", tb_ymax(&tb), 1);
        check("    with nothing in memory", tb_used(&tb), 0);
        check("    nothing in the head", store_head_bytes(tb.store_), 0);
        check("    and nothing in the tail", store_tail_bytes(tb.store_), 0);

        check("  saving the empty document works", tb_save(&tb) ? 1 : 0, 1);
        int saved_len = 0;
        stub_file_content("/cut.txt", &saved_len);
        check("    and it is empty", saved_len, 0);
        tb_destroy(&tb);

        /* And a range out of the middle, which is the harder one: text stays
         * on both sides of it, the head is not empty while it runs, and what
         * comes back has to be the two halves joined with nothing between. */
        #define CUT_FROM 100
        #define CUT_TO   3000
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("a paged document to cut the middle out of",
              tb_init(&tb, DOC_KB, "/mid.txt") != NULL, 1);

        const tb_pos from = { CUT_FROM, 0 };
        const tb_pos to = { CUT_TO, 0 };
        const int cut = (CUT_TO - CUT_FROM) * DOC_LEN;
        check("  the range measures as its lines",
              tb_range_size(&tb, from, to), cut);
        check("  deleting it works", tb_range_del(&tb, from, to) ? 1 : 0, 1);
        check("    and the document is that many lines shorter",
              tb_ymax(&tb), DOC_LINES + 1 - (CUT_TO - CUT_FROM));

        check("  saving works", tb_save(&tb) ? 1 : 0, 1);
        const char* left = stub_file_content("/mid.txt", &saved_len);
        check("    and it is the two halves", saved_len, DOC_BYTES - cut);
        check("      the first of them unchanged",
              left != NULL && memcmp(left, DOC,
                                     (size_t) ((CUT_FROM - 1) * DOC_LEN)) == 0, 1);
        check("      the second following straight on",
              left != NULL && memcmp(left + (CUT_FROM - 1) * DOC_LEN,
                                     DOC + (CUT_TO - 1) * DOC_LEN,
                                     (size_t) (DOC_BYTES - (CUT_TO - 1) * DOC_LEN)) == 0, 1);
        tb_destroy(&tb);
    }

    /* --- an edit that has to travel through the tail --- */
    {
        /* Sliding up writes memory's back into TAIL, below where the live
         * text starts. That is the one place the store writes anywhere but
         * the end of a file, and on MOS 3.0.2 a handle opened FA_OPEN_ALWAYS
         * appends whatever the position says -- so it wrote nothing where it
         * meant to and put a copy past tail_end_ where nothing reads.
         *
         * Loading used to write the whole document to TAIL, which hid it: the
         * bytes a push failed to write were already at that offset, put there
         * by the load. An edit is not. Change a line near the bottom, walk
         * back to the top so the change goes out through TAIL, and the save
         * says whether the push landed. */
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("a paged document to edit", tb_init(&tb, DOC_KB, "/edit.txt") != NULL, 1);

        tb_pos low = { DOC_LINES - 5, 0 };
        tb_seek(&tb, low);
        check("  at a line near the bottom", tb_ypos(&tb), DOC_LINES - 5);
        check("  typing into it", tb_put(&tb, 'Z') ? 1 : 0, 1);

        tb_pos top = { 1, 0 };
        tb_seek(&tb, top);
        check("  and back to the top", tb_ypos(&tb), 1);
        check("    with the edit somewhere below", store_tail_bytes(tb.store_) > 0, 1);

        check("  saving works", tb_save(&tb) ? 1 : 0, 1);
        int saved_len = 0;
        const char* saved = stub_file_content("/edit.txt", &saved_len);
        check("  and the document is one byte longer", saved_len, DOC_BYTES + 1);

        /* The edited line, as it should now read. */
        const int at = (DOC_LINES - 6) * DOC_LEN;
        check("    with the typed byte at the front of that line",
              saved != NULL && saved[at] == 'Z', 1);
        check("      and the line it was pushed off still behind it",
              saved != NULL && memcmp(saved + at + 1, DOC + at, DOC_LEN - 2) == 0, 1);
        tb_destroy(&tb);
    }

    /* --- saving a paged document --- */
    {
        /* The head, then memory, then what is left of the tail -- and what
         * comes out has to be the document, byte for byte, wherever the window
         * happened to be sitting. */
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        check("a paged document to save", tb_init(&tb, DOC_KB, "/big.txt") != NULL, 1);

        /* With the window in the middle, so all three pieces have something in
         * them. Saving from the top or the bottom would leave one empty and
         * never notice an ordering mistake. */
        tb_pos mid = { DOC_LINES / 2, 0 };
        tb_seek(&tb, mid);
        check("  with all three pieces holding something",
              store_head_bytes(tb.store_) > 0 && tb_used(&tb) > 0
              && store_tail_bytes(tb.store_) > 0, 1);

        check("saving works", tb_save(&tb) ? 1 : 0, 1);

        int saved_len = 0;
        const char* saved = stub_file_content("/big.txt", &saved_len);
        check("  and writes the whole document", saved_len, DOC_BYTES);
        check("  byte for byte",
              saved != NULL && memcmp(saved, DOC, (size_t) DOC_BYTES) == 0 ? 1 : 0, 1);

        /* Through a temp and a rename: opening the document itself with
         * FA_CREATE_ALWAYS would truncate what the save is still reading out
         * of the head and the tail. */
        check("  leaving no temp file behind", stub_file_exists("/big.txt.aeds"), 0);

        /* And the document is still usable afterwards -- the window has not
         * moved and the scratch files are still there. */
        check("  with the cursor where it was", tb_ypos(&tb), DOC_LINES / 2);
        check("  reading its own line",
              strcmp(LINE_NOW(&tb), WANT(DOC_LINES / 2 - 1)), 0);
        check("  and nothing left unsaved", tb_changed(&tb) ? 1 : 0, 0);
        tb_destroy(&tb);
        check("closing it takes the scratch away",
              stub_file_exists("/big.txt.aedh") || stub_file_exists("/big.txt.aedt"), 0);
    }

    /* --- saving one that has been edited --- */
    {
        stub_file_reset();
        stub_file_set_content(DOC, DOC_BYTES);
        tb_init(&tb, DOC_KB, "/big.txt");
        tb_pos mid = { DOC_LINES / 2, 0 };
        tb_seek(&tb, mid);

        check("typing into a paged document", tb_put(&tb, 'Z') ? 1 : 0, 1);
        check("  makes it dirty", tb_changed(&tb) ? 1 : 0, 1);
        check("saving it", tb_save(&tb) ? 1 : 0, 1);

        int saved_len = 0;
        const char* saved = stub_file_content("/big.txt", &saved_len);
        check("  writes one byte more than it read", saved_len, DOC_BYTES + 1);
        const int at = (DOC_LINES / 2 - 1) * DOC_LEN;
        check("  with the new byte where it was typed",
              saved != NULL && saved[at] == 'Z' ? 1 : 0, 1);
        check("  the document before it untouched",
              saved != NULL && memcmp(saved, DOC, (size_t) at) == 0 ? 1 : 0, 1);
        check("  and everything after it carried through",
              saved != NULL && memcmp(saved + at + 1, DOC + at,
                                      (size_t)(DOC_BYTES - at)) == 0 ? 1 : 0, 1);
        tb_destroy(&tb);
    }

    /* --- a bare-LF document goes back out as bare LF --- */
    {
        /* The document is held as CRLF whatever the file had, so one that came
         * in with bare line feeds has to be converted back on the way out.
         * Without it, opening and saving adds a byte to every line -- which on
         * a 419 KiB file is exactly its line count, and how this was found. */
        #define SV_LINES 5000
        static char SV[SV_LINES * 20 + 1];
        int sv_at = 0;
        for (int i = 0; i < SV_LINES; i++) {
            for (int k = 0; k < 19; k++) {
                SV[sv_at++] = (char) ('a' + ((i + k) % 26));
            }
            SV[sv_at++] = '\n';
        }
        stub_file_reset();
        stub_file_set_content(SV, sv_at);
        check("a bare-LF document opens paged",
              tb_init(&tb, DOC_KB, "/lf2.txt") != NULL, 1);
        check("  larger than memory", store_tail_bytes(tb.store_) > 0, 1);

        tb_pos mid = { SV_LINES / 2, 0 };
        tb_seek(&tb, mid);
        check("saving it", tb_save(&tb) ? 1 : 0, 1);

        int saved_len = 0;
        const char* saved = stub_file_content("/lf2.txt", &saved_len);
        check("  writes what it read, not a byte more", saved_len, sv_at);
        check("  byte for byte",
              saved != NULL && memcmp(saved, SV, (size_t) sv_at) == 0 ? 1 : 0, 1);
        tb_destroy(&tb);
    }

    /* --- an unpaged document never slides --- */
    {
        check("an ordinary document", load_five(&tb), 1);
        check("  does not slide down", tb_slide_down(&tb) ? 1 : 0, 0);
        check("  nor up", tb_slide_up(&tb) ? 1 : 0, 0);
        check("  and settling does nothing", tb_settle(&tb) ? 1 : 0, 0);
        tb_destroy(&tb);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
