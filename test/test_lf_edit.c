/*
 * Editing a document whose breaks are one byte.
 *
 * A document keeps the breaks its file had, so a break is one byte or two and
 * the code that takes one out has to ask which. Three places did not, and each
 * of them removed two bytes from a one-byte break:
 *
 *   * tb_del_merge -- DELETE at the end of a line -- deleted twice, so the
 *     first character of the line being joined on went with the break, and the
 *     index was left counting a byte the text no longer had.
 *   * tb_bksp_merge -- BACKSPACE at the start of one -- the same, backwards,
 *     taking the last character of the line above.
 *   * tb_range_del, which counts down in the units tb_range_size hands it --
 *     and those are CRLF units by contract, whatever the document holds -- but
 *     subtracted the document's own break length instead, so a byte of every
 *     break went unaccounted for and the loop ate the next character to make
 *     it up.
 *
 * lb_merge_prev was the fourth: it takes the break off the count itself,
 * because tb_bksp_merge goes straight to the character buffer, and it took two.
 *
 * The invariant underneath all of them is that the index's lengths add up to
 * the bytes the buffer is holding. When they stop, the next edit lands in the
 * wrong place -- and tb_del_line, which deletes until the index says the line
 * is empty, never finishes at all.
 *
 * Every case here is run against CRLF as well, because CRLF is what was right
 * before and has to stay right.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "cmd_ops.h"
#include "text_buffer.h"
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

/* The index's line lengths, added up. This has to be what the buffer holds. */
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

/* The document, streamed, with the breaks it really has. */
static char seen[256];
static int seen_n = 0;
static bool seen_sink(void* ctx, const char* buf, int sz) {
    (void) ctx;
    for (int i = 0; i < sz && seen_n < (int) sizeof(seen) - 1; i++) {
        seen[seen_n++] = buf[i];
    }

    return true;
}

/* What the document saves as, which is the only view with the real breaks in
 * it -- the range stream normalises to CRLF on the way out. */
static const char* saved_text(text_buffer* tb, int* sz) {
    tb_set_fname(tb, "/out.txt", 8);
    if (!tb_save(tb)) {
        *sz = -1;

        return NULL;
    }

    return stub_file_content("/out.txt", sz);
}

static int saved_is(text_buffer* tb, const char* want) {
    int sz = 0;
    const char* got = saved_text(tb, &sz);
    const int wsz = (int) strlen(want);

    return sz == wsz && got != NULL && memcmp(got, want, (size_t) wsz) == 0;
}

static text_buffer* load(text_buffer* tb, const char* doc) {
    stub_file_reset();
    stub_file_set_content(doc, (int) strlen(doc));

    return tb_init(tb, 8, "/d.txt");
}

int main(void) {
    stub_discard_output();
    static text_buffer tb;

    /* --- DELETE at the end of a line joins the next one on --- */
    {
        check("a document of bare line feeds", load(&tb, "one\ntwo\n") != NULL, 1);
        tb_pos at = { 1, 3 };
        tb_seek(&tb, at);           /* the end of "one" */
        check("  DELETE at the end of a line joins them",
              tb_del_merge(&tb) ? 1 : 0, 1);
        check("    taking only the break", saved_is(&tb, "onetwo\n"), 1);
        check("    leaving the index describing the text", agrees(&tb), 1);
        check("    and one line where there were two", tb_ymax(&tb), 2);
        tb_destroy(&tb);

        check("the same document with CRLF", load(&tb, "one\r\ntwo\r\n") != NULL, 1);
        tb_seek(&tb, at);
        check("  joins them too", tb_del_merge(&tb) ? 1 : 0, 1);
        check("    taking both bytes of the break",
              saved_is(&tb, "onetwo\r\n"), 1);
        check("    and still adding up", agrees(&tb), 1);
        tb_destroy(&tb);
    }

    /* --- BACKSPACE at the start of a line joins it to the one above --- */
    {
        check("a document of bare line feeds to backspace in",
              load(&tb, "one\ntwo\n") != NULL, 1);
        tb_pos at = { 2, 0 };
        tb_seek(&tb, at);           /* the start of "two" */
        check("  BACKSPACE at the start of a line joins it up",
              tb_bksp_merge(&tb) ? 1 : 0, 1);
        check("    taking only the break", saved_is(&tb, "onetwo\n"), 1);
        check("    leaving the index describing the text", agrees(&tb), 1);
        check("    with the cursor where the line above ended", tb.x_, 3);
        tb_destroy(&tb);

        check("the same document with CRLF",
              load(&tb, "one\r\ntwo\r\n") != NULL, 1);
        tb_seek(&tb, at);
        check("  joins it up too", tb_bksp_merge(&tb) ? 1 : 0, 1);
        check("    taking both bytes", saved_is(&tb, "onetwo\r\n"), 1);
        check("    and still adding up", agrees(&tb), 1);
        check("    with the cursor in the same place", tb.x_, 3);
        tb_destroy(&tb);
    }

    /* --- deleting a range that covers a whole line --- */
    {
        check("a document of bare line feeds to cut from",
              load(&tb, "one\ntwo\nthree\n") != NULL, 1);
        tb_pos a = { 2, 0 };
        tb_pos b = { 3, 0 };
        check("  the range is the line and its break",
              tb_range_size(&tb, a, b), 5);   /* CRLF units, by contract */
        check("  deleting it works", tb_range_del(&tb, a, b) ? 1 : 0, 1);
        check("    takes the line and nothing else",
              saved_is(&tb, "one\nthree\n"), 1);
        check("    leaving the index describing the text", agrees(&tb), 1);
        tb_destroy(&tb);

        check("the same document with CRLF",
              load(&tb, "one\r\ntwo\r\nthree\r\n") != NULL, 1);
        check("  deleting the same range works",
              tb_range_del(&tb, a, b) ? 1 : 0, 1);
        check("    takes the line and nothing else",
              saved_is(&tb, "one\r\nthree\r\n"), 1);
        check("    and still adds up", agrees(&tb), 1);
        tb_destroy(&tb);
    }

    /* --- deleting every line of a document, one at a time --- */
    {
        /* tb_del_line deletes until the index says the line is empty. With the
         * index a byte ahead of the text it never got there, and the editor
         * hung -- on the last line of a document, from one keystroke. */
        check("a document of bare line feeds to empty",
              load(&tb, "one\ntwo\nthree\n") != NULL, 1);
        int guard = 0;
        while (guard++ < 20 && tb_del_line(&tb)) {
            if (!agrees(&tb)) {
                break;
            }
        }
        check("  every line of it can be deleted", guard < 20, 1);
        check("    ending with nothing in it", tb_used(&tb), 0);
        check("    and the index agreeing", agrees(&tb), 1);
        tb_destroy(&tb);

        check("the same document with CRLF",
              load(&tb, "one\r\ntwo\r\nthree\r\n") != NULL, 1);
        guard = 0;
        while (guard++ < 20 && tb_del_line(&tb)) {
            if (!agrees(&tb)) {
                break;
            }
        }
        check("  empties the same way", guard < 20, 1);
        check("    with nothing left", tb_used(&tb), 0);
        check("    and the index agreeing", agrees(&tb), 1);
        tb_destroy(&tb);
    }

    /* --- and the same through the keys, which is how anyone meets it --- */
    {
        static editor ed;
        stub_file_reset();
        stub_file_set_content("one\ntwo\nthree\n", 14);
        check("an editor on a document of bare line feeds",
              ed_init(&ed, 8, "/k.txt") != NULL, 1);

        tb_pos end_of_two = { 2, 3 };
        tb_seek(&ed.buf_, end_of_two);
        cmd_del(&ed);               /* DELETE at the end of line 2 */
        check("  DELETE at the end of a line", agrees(&ed.buf_), 1);
        check("    joins the next one on whole",
              saved_is(&ed.buf_, "one\ntwothree\n"), 1);

        tb_pos start_of_two = { 2, 0 };
        tb_seek(&ed.buf_, start_of_two);
        cmd_bksp(&ed);              /* BACKSPACE at the start of line 2 */
        check("  BACKSPACE at the start of one", agrees(&ed.buf_), 1);
        check("    joins it to the line above whole",
              saved_is(&ed.buf_, "onetwothree\n"), 1);

        /* CTRL+D down the whole document, including the last line. */
        int guard = 0;
        while (guard++ < 20 && tb_ymax(&ed.buf_) > 1) {
            cmd_del_line(&ed);
            if (!agrees(&ed.buf_)) {
                break;
            }
        }
        cmd_del_line(&ed);
        check("  and CTRL+D takes every line without hanging", guard < 20, 1);
        check("    leaving the index agreeing", agrees(&ed.buf_), 1);
        ed_destroy(&ed);
    }

    /* --- deleting the last line there is --- */
    {
        /*
         * lb_del on the last line has nothing below to pull up, so it empties
         * the line instead. It said `lb->curr_ = 0`, setting the pointer where
         * it meant the length -- after which the index had no current line and
         * lb_curr answered with the distance from the buffer to address zero.
         *
         * Nothing in the editor reached it, because tb_del_line empties a line
         * before asking for it and the branch above runs instead. It is the
         * module's own contract all the same, and a walk that indexes the
         * index by number went looking for line minus four hundred million the
         * first time anything did reach it.
         */
        static line_buffer lb;
        check("a line index to empty", lb_init(&lb, 8) != NULL, 1);
        lb_cadd(&lb, 5);            /* one line, five bytes, and it is the last */
        check("  holding one line of five", lb_csize(&lb), 5);
        check("  which is the last one", lb_last(&lb) ? 1 : 0, 1);

        check("  deleting it reports something happened",
              lb_del(&lb) ? 1 : 0, 1);
        check("    the line is still there", lb_lines(&lb), 1);
        check("      and empty", lb_csize(&lb), 0);
        check("    the cursor is still a cursor", lb_curr(&lb), 0);
        check("      pointing into the index", lb_at(&lb, 0), 0);

        check("  and deleting an empty last line does nothing",
              lb_del(&lb) ? 1 : 0, 0);
        lb_destroy(&lb);
    }

    /* --- seen, for its own sake: the streamed form is CRLF either way --- */
    {
        check("a bare-LF document to stream", load(&tb, "a\nb\n") != NULL, 1);
        seen_n = 0;
        tb_pos a = { 1, 0 };
        tb_pos b = { tb_ymax(&tb), 1 << 20 };
        tb_range_walk(&tb, a, b, seen_sink, NULL);
        check("  a range gives back CRLF by contract", seen_n, 6);
        check("    however the document keeps them", tb.elen_, 1);
        check("  which is why tb_range_del counts in those units, and why "
              "this file exists", agrees(&tb), 1);
        tb_destroy(&tb);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
