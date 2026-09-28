/*
 * Two documents at once, which is what the document struct is for.
 *
 * Everything that belongs to one open file lives in a document, so two of
 * them side by side must not share anything the core keeps for them: edits
 * recorded into one undo log are not in the other's, and two paged documents
 * -- each with its scratch files open -- can be read in turn. The core keeps
 * static scratch buffers for paging; this is what says they hold nothing from
 * one call to the next that a second document could trip on.
 *
 * Built from core headers only: document.h has to stand without the editor.
 */
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "app.h"
#include "document.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static int open_doc(document* d, int kb) {
    memset(d, 0, sizeof(*d));
    if (tb_init(&d->buf_, kb, NULL) == NULL) {
        return 0;
    }
    if (undo_init(&d->undo_, 256, 16) == NULL) {
        tb_destroy(&d->buf_);

        return 0;
    }
    tb_set_undo(&d->buf_, &d->undo_);

    return 1;
}

static void close_doc(document* d) {
    tb_set_undo(&d->buf_, NULL);
    undo_destroy(&d->undo_);
    tb_destroy(&d->buf_);
}

static void put(document* d, const char* s) {
    while (*s != 0) {
        tb_put(&d->buf_, *s++);
    }
}

// Whether line `line` of `d` starts with `want`, reading it where it is.
static int line_is(document* d, int line, const char* want) {
    const tb_pos at = { .line = line, .x = 0 };
    tb_seek(&d->buf_, at);
    const split_line sl = tb_curr_line(&d->buf_);
    const int n = (int) strlen(want);
    if (sl.ssz_ < n) {
        return 0;
    }

    return memcmp(sl.suffix_, want, (size_t) n) == 0;
}

// A file of `lines` numbered lines, each "<tag>NNNN" -- large enough that a
// 4 KiB document has to page it.
static int numbered(char* out, char tag, int lines) {
    int n = 0;
    for (int i = 1; i <= lines; i++) {
        n += sprintf(out + n, "%c%04d some text to make it longer\r\n", tag, i);
    }

    return n;
}

int main(void) {
    static const app_context APP = { .name = "doc" };
    app_set(&APP);

    static document a;
    static document b;

    /* --- edits and undo belong to their own document --- */
    {
        stub_file_reset();
        check("one document opens", open_doc(&a, 4), 1);
        check("  and a second beside it", open_doc(&b, 4), 1);
        put(&a, "abc");
        put(&b, "xy");
        check("each holds its own text", tb_used(&a.buf_), 3);
        check("  the other its own", tb_used(&b.buf_), 2);

        const int b_steps = undo_count(&b.undo_);
        check("undoing in one", undo_apply(&a.undo_, &a.buf_) ? 1 : 0, 1);
        check("  takes its text back", tb_used(&a.buf_), 0);
        check("  and leaves the other's", tb_used(&b.buf_), 2);
        check("  and the other's log", undo_count(&b.undo_), b_steps);
        check("redo in one", redo_apply(&a.undo_, &a.buf_) ? 1 : 0, 1);
        check("  puts its own text back", tb_used(&a.buf_), 3);
        check("  still leaving the other alone", tb_used(&b.buf_), 2);

        close_doc(&a);
        close_doc(&b);
    }

    /* --- two paged documents, read in turn --- */
    {
        static char fa[16384];
        static char fb[16384];
        const int na = numbered(fa, 'a', 300);
        const int nb = numbered(fb, 'b', 300);
        stub_file_reset();
        stub_file_add("/a.txt", fa, na);
        stub_file_add("/b.txt", fb, nb);

        check("a large file opens into one document", open_doc(&a, 4), 1);
        check("  and loads", tb_load(&a.buf_, "/a.txt") == TB_OK ? 1 : 0, 1);
        check("  paged, being larger than its memory", a.buf_.paged_ ? 1 : 0, 1);
        check("another into a second", open_doc(&b, 4), 1);
        check("  and loads", tb_load(&b.buf_, "/b.txt") == TB_OK ? 1 : 0, 1);
        check("  paged too", b.buf_.paged_ ? 1 : 0, 1);
        check("each has its own scratch files",
              stub_file_exists("/a.txt.aedh") && stub_file_exists("/b.txt.aedh"), 1);

        /* Far apart in each, and alternating, so every read slides. */
        check("the first, near its end", line_is(&a, 290, "a0290"), 1);
        check("the second, near its start", line_is(&b, 5, "b0005"), 1);
        check("the first, back near its start", line_is(&a, 3, "a0003"), 1);
        check("the second, near its end", line_is(&b, 295, "b0295"), 1);
        check("the first, in its middle", line_is(&a, 150, "a0150"), 1);
        check("the second, in its middle", line_is(&b, 151, "b0151"), 1);

        /* An edit in one, read back after the other has slid about. */
        tb_seek(&a.buf_, (tb_pos) { .line = 150, .x = 0 });
        put(&a, "EDIT");
        check("the second slides away", line_is(&b, 2, "b0002"), 1);
        check("  and the first still has its edit", line_is(&a, 150, "EDITa0150"), 1);
        check("  which the second does not", line_is(&b, 150, "b0150"), 1);

        close_doc(&a);
        close_doc(&b);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
