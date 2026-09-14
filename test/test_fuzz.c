/*
 * A scripted storm of commands, with two invariants checked after every one.
 *
 * The tests next door each aim at one thing. This one aims at the states no
 * one thought to write down: it drives the editor through a deterministic
 * pseudo-random sequence of real commands -- the ones keys are bound to -- and
 * after each asks two questions that have to stay true whatever has happened.
 *
 *   1. The cursor's line agrees with the document. tb_ypos comes from the line
 *      index and tb_curr_line comes from the character buffer, and nothing in
 *      the types stops the two drifting apart. When they do, the next edit
 *      lands on the wrong line -- which is how this was found: a paste
 *      replaced a selection one line above the one the selection was on.
 *
 *   2. The document is still made of its own lines. tb_ymax counts them and
 *      streaming it counts them again; a disagreement means the index and the
 *      text stopped describing the same document.
 *
 * Deterministic: the seeds are fixed, so a failure is reproducible and the
 * report says which op number and which seed. Streaming is what both checks
 * read the document through -- tb_range_walk goes through the store rather
 * than through a walker, so the check cannot disturb what it is checking.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "cmd_ops.h"
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

static unsigned long rng = 0;
static unsigned next_rand(void) {
    rng = rng * 6364136223846793005UL + 1442695040888963407UL;

    return (unsigned) ((rng >> 33) & 0x7fffffff);
}

/* The whole document, streamed. */
static char doc_buf[8192];
static int doc_n = 0;
static bool doc_sink(void* ctx, const char* buf, int sz) {
    (void) ctx;
    for (int i = 0; i < sz && doc_n < (int) sizeof(doc_buf); i++) {
        doc_buf[doc_n++] = buf[i];
    }

    return true;
}

static void read_doc(text_buffer* tb) {
    doc_n = 0;
    tb_pos a = { 1, 0 };
    tb_pos b = { tb_ymax(tb), 1 << 20 };
    tb_range_walk(tb, a, b, doc_sink, NULL);
}

/* 0 when the cursor's own line is the line the document has at tb_ypos.
 * Reads what read_doc last put in doc_buf, so one stream answers both. */
static int line_disagrees(text_buffer* tb) {
    const int want_line = tb_ypos(tb);
    int at = 0;
    int line = 1;
    while (line < want_line && at < doc_n) {
        if (doc_buf[at] == '\n') {
            line++;
        }
        at++;
    }
    int end = at;
    while (end < doc_n && doc_buf[end] != '\r' && doc_buf[end] != '\n') {
        end++;
    }

    const split_line cl = tb_curr_line(tb);
    const int got = cl.psz_ + cl.ssz_;
    if (got != end - at) {
        return 1;
    }
    for (int i = 0; i < got; i++) {
        const char c = i < cl.psz_ ? cl.prefix_[i] : cl.suffix_[i - cl.psz_];
        if (c != doc_buf[at + i]) {
            return 1;
        }
    }

    return 0;
}

/* 0 when the index and the text agree on how many lines there are. */
static int count_disagrees(text_buffer* tb) {
    int lines = 1;
    for (int i = 0; i < doc_n; i++) {
        if (doc_buf[i] == '\n') {
            lines++;
        }
    }

    return lines == tb_ymax(tb) ? 0 : 1;
}

static void one_op(editor* ed, unsigned r) {
    switch (r % 22) {
    case 0:  cmd_down(ed); break;
    case 1:  cmd_up(ed); break;
    case 2:  cmd_left(ed); break;
    case 3:  cmd_right(ed); break;
    case 4:  cmd_home(ed); break;
    case 5:  cmd_end(ed); break;
    case 6:  cmd_page_down(ed); break;
    case 7:  cmd_page_up(ed); break;
    case 8:  { key k; memset(&k, 0, sizeof(k));
               k.key = (char) ('a' + (next_rand() % 26));
               cmd_putc(ed, k); break; }
    case 9:  cmd_del(ed); break;
    case 10: cmd_bksp(ed); break;
    case 11: cmd_newl(ed); break;
    case 12: cmd_del_line(ed); break;
    case 13: cmd_find_next(ed); break;
    case 14: cmd_find_prev(ed); break;
    case 15: cmd_repaint_rows(ed, 0, 40); break;
    case 16: cmd_doc_top(ed); break;
    case 17: cmd_doc_end(ed); break;
    case 18: cmd_w_left(ed); break;
    case 19: cmd_w_right(ed); break;
    case 20: ed->selecting_ = true; cmd_down(ed); cmd_copy(ed);
             ed->selecting_ = false; break;
    case 21: cmd_paste(ed); break;
    }
}

int main(void) {
    stub_discard_output();

    static const char* DOCS[] = {
        "alpha\r\nbeta\r\ngamma\r\ndelta\r\nepsilon\r\n",
        // Bare line feeds. This is the one that found tb_del_merge deleting
        // two characters for a one-byte break -- and the hang that came of it.
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        "\tindented\r\n\t\tdeeper\r\nplain\r\n\tmixed\ttabs\there\r\n",
        "no trailing newline",
        "a\r\n\r\n\r\nb\r\n",
    };
    const int ndocs = (int) (sizeof(DOCS) / sizeof(DOCS[0]));
    const int seeds = 4;
    const int ops = 150;

    int line_bad = 0;
    int count_bad = 0;
    int ran = 0;
    for (int d = 0; d < ndocs; d++) {
        for (int s = 1; s <= seeds; s++) {
            rng = (unsigned long) s * 2654435761UL + 12345UL;
            stub_file_reset();
            stub_file_set_content(DOCS[d], (int) strlen(DOCS[d]));

            static editor ed;
            if (ed_init(&ed, 8, "/fuzz.txt") == NULL) {
                fprintf(stderr, "FAIL  editor would not start\n");

                return 1;
            }
            memcpy(ed.find_, "e", 2);
            ed.findsz_ = 1;

            for (int i = 0; i < ops; i++) {
                one_op(&ed, next_rand());
                ran++;
                read_doc(&ed.buf_);     // once, for both checks below
                if (line_disagrees(&ed.buf_) && line_bad == 0) {
                    line_bad = 1;
                    {
                        const split_line cl = tb_curr_line(&ed.buf_);
                        fprintf(stderr,
                            "      doc %d seed %d op %d: ypos says line %d of "
                            "%d, and its line reads [%.*s%.*s]\n",
                            d, s, i, tb_ypos(&ed.buf_), tb_ymax(&ed.buf_),
                            cl.psz_, cl.prefix_ ? cl.prefix_ : "",
                            cl.ssz_, cl.suffix_ ? cl.suffix_ : "");
                    }
                }
                if (count_disagrees(&ed.buf_) && count_bad == 0) {
                    count_bad = 1;
                    fprintf(stderr,
                            "      doc %d seed %d op %d: tb_ymax says %d\n",
                            d, s, i, tb_ymax(&ed.buf_));
                }
            }
            ed_destroy(&ed);
        }
    }

    check("commands leave the cursor on the line it claims", line_bad, 0);
    check("  and the index counting the lines the text has", count_bad, 0);
    check("  over every command, all the way through", ran, ndocs * seeds * ops);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
