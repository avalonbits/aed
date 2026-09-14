/*
 * Host tests for walkers: the read-only copies a repaint, a search and the
 * range operations make with tb_copy.
 *
 * A walker shares the original's buffers. It used to move the gap to read --
 * every tb_down was a cb_next, and a cb_next is a memmove -- which duplicates
 * rather than destroys, so the cursor that owns the buffer still read the same
 * bytes. But only while the gap stayed wider than the distance the walker had
 * travelled. Narrower, and the walker overwrote the document it was reading.
 *
 * That is what forced prime_spare to keep a third of the free space at the
 * cursor, and what stopped the buffer shrinking. A walker moves by number now:
 * wline_ is the line and woff_ is the byte offset of its start from lo_, and
 * the bytes are found rather than brought into place.
 *
 * So the tests are about two things. The walker has to read the right bytes,
 * including for the one line in the buffer that spans the gap and comes back
 * in two pieces. And the document has to be untouched afterwards, however
 * narrow the gap was -- which is the failure the whole change is for, and
 * which is silent.
 *
 * See .internal/docs/WALKER.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

#include "char_buffer.h"
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

/*
 * The walker's current line as the document has it, whichever way the buffer
 * split it. A line that spans the gap arrives in two runs, and joining them is
 * the caller's job -- scr_paint_row, scan_split and scr_glyph_at_split all do
 * exactly this.
 */
static int line_is(text_buffer* w, const char* want) {
    const split_line ln = tb_curr_line(w);
    const int wsz = (int) strlen(want);

    if (ln.psz_ + ln.ssz_ != wsz) {
        return 0;
    }
    if (ln.psz_ > 0 && memcmp(ln.prefix_, want, (size_t) ln.psz_) != 0) {
        return 0;
    }
    if (ln.ssz_ > 0
            && memcmp(ln.suffix_, want + ln.psz_, (size_t) ln.ssz_) != 0) {
        return 0;
    }

    return 1;
}

/* Whether the walker's line arrived in two runs rather than one. */
static int is_split(text_buffer* w) {
    const split_line ln = tb_curr_line(w);

    return ln.psz_ > 0 && ln.ssz_ > 0;
}

/* A document of `lines` lines, each `len` bytes of text plus a CRLF. */
static char* make_doc(int lines, int len, int* size) {
    const int per = len + 2;
    char* doc = (char*) malloc((size_t) (lines * per + 1));
    if (doc == NULL) {
        return NULL;
    }
    int at = 0;
    for (int i = 0; i < lines; i++) {
        for (int k = 0; k < len; k++) {
            doc[at++] = (char) ('a' + ((i + k) % 26));
        }
        doc[at++] = '\r';
        doc[at++] = '\n';
    }
    doc[at] = 0;
    *size = at;

    return doc;
}

/* The text of line `i` of such a document, without its break. */
static const char* doc_line(int i, int len) {
    static char out[128];
    for (int k = 0; k < len; k++) {
        out[k] = (char) ('a' + ((i + k) % 26));
    }
    out[len] = 0;

    return out;
}

/* Does the document still say what it said? Saves it and compares the bytes. */
static int still_says(text_buffer* tb, const char* want, int wsz,
                      const char* path) {
    tb_set_fname(tb, path, (int) strlen(path));
    if (!tb_save(tb)) {
        return 0;
    }
    int got = 0;
    const char* saved = stub_file_content(path, &got);

    return got == wsz && saved != NULL
        && memcmp(saved, want, (size_t) wsz) == 0;
}

int main(void) {
    stub_discard_output();

    static text_buffer tb;

    /* --- an empty half of a line still says it is empty --- */
    {
        /* tb_curr_line fills a split_line by calling tb_prefix and tb_suffix,
         * and a struct on the stack is whatever was there before. cb_prefix
         * reports an empty prefix through its own `sz` rather than the
         * caller's, so tb_prefix returning early left psz_ untouched -- a
         * garbage length beside a NULL pointer, handed to the view, on every
         * line where the cursor sits in column zero. Nothing crashed because
         * every caller tests the pointer first. */
        stub_file_reset();
        stub_file_set_content("abc\r\ndef\r\n", 10);
        check("a document with the cursor at the very start",
              tb_init(&tb, 4, "/pre.txt") != NULL, 1);

        int sz = 12345;
        check("  tb_prefix has nothing to give",
              tb_prefix(&tb, &sz) == NULL ? 1 : 0, 1);
        check("    and says so in the size", sz, 0);

        sz = 12345;
        check("  tb_suffix has the line", tb_suffix(&tb, &sz) != NULL ? 1 : 0, 1);
        check("    and says how much of it", sz, 3);

        const split_line ln = tb_curr_line(&tb);
        check("  so a split_line starts out empty on the left", ln.psz_, 0);
        check("    with the line on the right", ln.ssz_, 3);
        tb_destroy(&tb);
    }

    /* --- a walker reads every line, wherever the gap is --- */
    {
        #define LINES 60
        #define LEN   20
        int size = 0;
        char* doc = make_doc(LINES, LEN, &size);
        if (doc == NULL) {
            fprintf(stderr, "FAIL  out of memory\n");

            return 1;
        }
        stub_file_reset();
        stub_file_set_content(doc, size);
        check("a document to walk", tb_init(&tb, 16, "/walk.txt") != NULL, 1);

        /* The cursor in the middle, so the gap is in the middle: the walker
         * starts below it, crosses it, and ends above it. */
        tb_pos mid = { LINES / 2, 7 };
        tb_seek(&tb, mid);

        text_buffer w;
        tb_copy(&w, &tb);
        check("a walker starts on the line the cursor is on",
              tb_ypos(&w), tb_ypos(&tb));
        tb_pos top = { 1, 0 };
        tb_seek(&w, top);
        check("  and can be sent to the top of the window", tb_ypos(&w), 1);

        int wrong = 0;
        int splits = 0;
        for (int n = 1; n <= LINES; n++) {
            if (!line_is(&w, doc_line(n - 1, LEN))) {
                wrong = n;
                break;
            }
            splits += is_split(&w);
            if (n < LINES) {
                tb_down(&w);
            }
        }
        check("  every line of the document reads as itself", wrong, 0);
        check("    with exactly one of them split across the gap", splits, 1);
        check("  ending on the last line", tb_ypos(&w), LINES);

        /* And back up, which reads the same lines in the other order. */
        wrong = 0;
        for (int n = LINES; n >= 1; n--) {
            if (!line_is(&w, doc_line(n - 1, LEN))) {
                wrong = n;
                break;
            }
            if (n > 1) {
                tb_up(&w);
            }
        }
        check("  and every line again walking back up", wrong, 0);

        check("  without moving the cursor it came from",
              tb_ypos(&tb), LINES / 2);
        check("    or its column", tb.x_, 7);
        check("  and the document is byte for byte what it was",
              still_says(&tb, doc, size, "/out.txt"), 1);

        tb_destroy(&tb);
        free(doc);
    }

    /* --- the line that spans the gap --- */
    {
        /* One line in the buffer has the gap inside it: the one the cursor is
         * on. Everything that reads a walker's line has to take it in two
         * pieces, and the pieces have to be the line in order. */
        int size = 0;
        char* doc = make_doc(10, 26, &size);
        stub_file_reset();
        stub_file_set_content(doc, size);
        check("a document with the cursor mid-line",
              tb_init(&tb, 16, "/split.txt") != NULL, 1);
        tb_pos at = { 4, 13 };
        tb_seek(&tb, at);

        text_buffer w;
        tb_copy(&w, &tb);
        check("the walker's own line is the split one", is_split(&w), 1);

        const split_line ln = tb_curr_line(&w);
        check("  cut where the cursor is", ln.psz_, 13);
        check("    with the rest after it", ln.ssz_, 13);
        check("  and the two halves are the line", line_is(&w, doc_line(3, 26)), 1);

        /* Its neighbours are whole. */
        tb_up(&w);
        check("  the line above is in one piece", is_split(&w), 0);
        check("    and reads right", line_is(&w, doc_line(2, 26)), 1);
        tb_down(&w);
        tb_down(&w);
        check("  the line below is too", is_split(&w), 0);
        check("    and reads right", line_is(&w, doc_line(4, 26)), 1);

        tb_destroy(&tb);
        free(doc);
    }

    /* --- a gap narrower than the walk --- */
    {
        /*
         * The failure this change exists to remove.
         *
         * A walker used to read by moving the gap, which leaves the bytes it
         * passed behind it -- so the owner still saw them, until the walker's
         * writes reached where the owner's cend_ pointed. Past that the
         * document was overwritten with a copy of itself shifted along, and
         * nothing said so: the repaint looked plausible and the file saved
         * wrong.
         *
         * A buffer filled to within a few dozen bytes of full has a gap that
         * small, and walking twenty lines of forty bytes travels eight hundred.
         * Before the walker stopped moving the gap this corrupted the document
         * outright. It must now not touch it.
         */
        #define TIGHT_LINES 20
        #define TIGHT_LEN   40
        int size = 0;
        char* doc = make_doc(TIGHT_LINES, TIGHT_LEN, &size);
        stub_file_reset();
        stub_file_set_content(doc, size);
        /* One kilobyte of buffer against 840 bytes of document. */
        check("a document that nearly fills its buffer",
              tb_init(&tb, 1, "/tight.txt") != NULL, 1);
        check("  really nearly", tb_available(&tb) < 200 ? 1 : 0, 1);

        tb_pos mid = { TIGHT_LINES / 2, 0 };
        tb_seek(&tb, mid);
        const int gap_was = tb_available(&tb);
        const int walk = TIGHT_LINES * (TIGHT_LEN + 2);
        check("  with a gap narrower than the walk about to happen",
              gap_was < walk ? 1 : 0, 1);

        text_buffer w;
        tb_copy(&w, &tb);
        tb_pos top = { 1, 0 };
        tb_seek(&w, top);
        int wrong = 0;
        for (int n = 1; n <= TIGHT_LINES; n++) {
            if (!line_is(&w, doc_line(n - 1, TIGHT_LEN))) {
                wrong = n;
                break;
            }
            if (n < TIGHT_LINES) {
                tb_down(&w);
            }
        }
        check("  the walk reads every line correctly", wrong, 0);
        check("  and the document is untouched afterwards",
              still_says(&tb, doc, size, "/tight.out"), 1);

        tb_destroy(&tb);
        free(doc);
    }

    /* --- a walker stops at the window, and says so --- */
    {
        /* A walker may not slide, so the ends of the window are the ends of
         * what it can reach. Stepping past one has to stop rather than run on
         * into whatever the index holds next. */
        #define PAGED_LINES 4000
        #define PAGED_LEN   40
        static char big[PAGED_LINES * PAGED_LEN + 1];
        for (int i = 0; i < PAGED_LINES; i++) {
            for (int k = 0; k < PAGED_LEN - 2; k++) {
                big[i * PAGED_LEN + k] = (char) ('a' + ((i + k) % 26));
            }
            big[i * PAGED_LEN + PAGED_LEN - 2] = '\r';
            big[i * PAGED_LEN + PAGED_LEN - 1] = '\n';
        }
        stub_file_reset();
        stub_file_set_content(big, PAGED_LINES * PAGED_LEN);
        check("a paged document", tb_init(&tb, 64, "/paged.txt") != NULL, 1);
        check("  really pages", tb.paged_ ? 1 : 0, 1);

        tb_pos deep = { PAGED_LINES / 2, 0 };
        tb_seek(&tb, deep);
        check("  with its text away from the start of the buffer",
              tb.cb_.lo_ > tb.cb_.buf_ ? 1 : 0, 1);

        text_buffer w;
        tb_copy(&w, &tb);
        check("  a walker starts where the cursor is",
              tb_ypos(&w), tb_ypos(&tb));
        check("    reading its line", line_is(&w, doc_line(PAGED_LINES / 2 - 1,
                                                          PAGED_LEN - 2)), 1);

        /* Up until it will go no further, which is the top of the window. */
        int at = tb_ypos(&w);
        int steps = 0;
        while (steps < PAGED_LINES) {
            tb_up(&w);
            if (tb_ypos(&w) == at) {
                break;
            }
            at = tb_ypos(&w);
            steps++;
        }
        check("  it stops going up", steps < PAGED_LINES ? 1 : 0, 1);
        check("    inside the window rather than at the document's top",
              at > 1 ? 1 : 0, 1);
        check("    still reading the line it stopped on",
              line_is(&w, doc_line(at - 1, PAGED_LEN - 2)), 1);
        check("  without having moved the cursor",
              tb_ypos(&tb), PAGED_LINES / 2);

        /* And down to the far end of the window. */
        steps = 0;
        while (steps < PAGED_LINES) {
            tb_down(&w);
            if (tb_ypos(&w) == at) {
                break;
            }
            at = tb_ypos(&w);
            steps++;
        }
        check("  it stops going down too", steps < PAGED_LINES ? 1 : 0, 1);

        /* And it stops on an empty line, which is right and worth pinning.
         * The last entry in the index is the line that carries on past memory:
         * the rest of its text is still in the tail, so what memory holds of
         * it is nothing at all. The cursor never rests there because tb_settle
         * moves it on, and a walker cannot settle -- so this is the one place
         * a walker sees a line the document does not have. Measured identical
         * before and after a walker stopped moving the gap. */
        check("    on the line that carries on past memory",
              line_is(&w, ""), 1);
        tb_up(&w);
        check("    with the last whole line right above it",
              line_is(&w, doc_line(at - 2, PAGED_LEN - 2)), 1);
        check("  and the window has not moved under it",
              tb_ypos(&tb), PAGED_LINES / 2);

        tb_destroy(&tb);
    }

    /* --- a column is not a line --- */
    {
        /* woff_ is the start of the line and x_ is where in it the walker is.
         * Moving along a line must leave the line alone, and moving between
         * lines must keep the column where it can. */
        int size = 0;
        char* doc = make_doc(10, 20, &size);
        stub_file_reset();
        stub_file_set_content(doc, size);
        check("a document to walk along", tb_init(&tb, 16, "/cols.txt") != NULL, 1);
        tb_pos at = { 5, 7 };
        tb_seek(&tb, at);

        text_buffer w;
        tb_copy(&w, &tb);
        check("a walker inherits the column", w.x_, 7);
        check("  reading its whole line even so",
              line_is(&w, doc_line(4, 20)), 1);

        tb_end(&w);
        check("  to the end of the line", w.x_, 20);
        check("    without changing the line it is on",
              line_is(&w, doc_line(4, 20)), 1);

        tb_home(&w);
        check("  and home again", w.x_, 0);
        check("    same line", line_is(&w, doc_line(4, 20)), 1);

        tb_down(&w);
        check("  a step down is the next line", tb_ypos(&w), 6);
        check("    read in full", line_is(&w, doc_line(5, 20)), 1);

        tb_destroy(&tb);
        free(doc);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
