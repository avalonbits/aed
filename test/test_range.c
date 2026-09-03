/*
 * Host tests for positions and range operations.
 *
 * These are the floor under copy/cut/paste. There is no absolute offset in this
 * model -- tb_goto_offset moves within the current line only, and the bytes are
 * split across the gap -- so a span of text is addressed as a pair of
 * (line, byte-in-line) positions, and every operation on one has to walk lines
 * rather than index into an array.
 *
 * The cases that matter are the ones where that walking can go wrong: a range
 * that stays on one line, one that crosses the gap, one that crosses several
 * line breaks, one handed over backwards, and one that runs to the very end of
 * the document.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agon/mos.h>

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

static void check_s(const char* name, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-52s %s\n", name, "ok");
    } else {
        fprintf(stderr, "FAIL  %-52s got \"%s\", want \"%s\"\n", name, got, want);
        failures++;
    }
}

static tb_pos at(int line, int x) {
    tb_pos p;
    p.line = line;
    p.x = x;

    return p;
}

/* The clipboard's contents as a C string, with the CRLFs made visible so a
 * mismatch reads clearly. */
static const char* clip_of(char_buffer* cb) {
    static char out[512];
    int sz = 0;
    char* p = cb_prefix(cb, &sz);
    int n = 0;
    for (int i = 0; i < sz && n < (int) sizeof(out) - 3; i++) {
        if (p[i] == '\r') {
            out[n++] = '<'; out[n++] = 'R'; out[n++] = '>';
        } else if (p[i] == '\n') {
            out[n++] = '<'; out[n++] = 'N'; out[n++] = '>';
        } else {
            out[n++] = p[i];
        }
    }
    out[n] = 0;

    return out;
}

/* The whole document, lines joined with '/', so a delete can be checked in one
 * assertion instead of several. */
static const char* doc_of(text_buffer* tb) {
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_seek(&cp, at(1, 0));

    static char out[1024];
    int n = 0;
    int tpos = tb_ypos(&cp);
    for (;;) {
        int sz = 0;
        char* line = tb_suffix(&cp, &sz);
        if (line != NULL && sz > 0 && n + sz < (int) sizeof(out) - 2) {
            memcpy(out + n, line, (size_t) sz);
            n += sz;
        }
        tb_down(&cp);
        const int npos = tb_ypos(&cp);
        if (npos == tpos) {
            break;
        }
        tpos = npos;
        out[n++] = '/';
    }
    out[n] = 0;

    return out;
}

/* "one/two/three/" -- three lines and the empty one the trailing CRLF makes. */
static const char DOC[] = "one\r\ntwo\r\nthree\r\n";

static bool loaded = false;

static void load(text_buffer* tb) {
    if (loaded) {
        tb_destroy(tb);
    }
    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    tb_init(tb, 4, "doc.txt");
    loaded = true;
}

int main(void) {
    stub_discard_output();

    text_buffer tb;
    load(&tb);
    check_s("the document loaded", doc_of(&tb), "one/two/three/");

    /* --- tell and seek --- */
    tb_seek(&tb, at(2, 1));
    check("seek lands on the line", tb_ypos(&tb), 2);
    check("  and the byte", tb_xpos(&tb), 2);
    tb_pos here = tb_tell(&tb);
    check("tell reports the line back", here.line, 2);
    check("  and the byte back", here.x, 1);

    /* Clamping, so a caller need not know how long a line is or how many there
     * are -- the selection code will routinely ask for the end of things. */
    tb_seek(&tb, at(2, 99));
    check("seek past the end of a line stops at its end", tb_xpos(&tb), 4);
    tb_seek(&tb, at(99, 0));
    check("seek past the last line stops on it", tb_ypos(&tb), 4);
    tb_seek(&tb, at(-5, -5));
    check("seek before the start stops at the start", tb_ypos(&tb), 1);
    check("  at column one", tb_xpos(&tb), 1);

    /* x never counts the CRLF: the end of a line's text and the start of the
     * next are distinct positions, which is what makes a range unambiguous
     * about whether the line break is inside it. */
    tb_seek(&tb, at(1, 99));
    check("the end of line one is past its text", tb_xpos(&tb), 4);

    /* --- cmp --- */
    check("earlier line is less", tb_cmp(at(1, 0), at(2, 0)) < 0, 1);
    check("later line is greater", tb_cmp(at(3, 0), at(2, 9)) > 0, 1);
    check("same line, earlier byte", tb_cmp(at(2, 1), at(2, 3)) < 0, 1);
    check("the same position", tb_cmp(at(2, 1), at(2, 1)), 0);

    /* --- range size --- */
    check("within one line", tb_range_size(&tb, at(1, 0), at(1, 3)), 3);
    check("  handed over backwards", tb_range_size(&tb, at(1, 3), at(1, 0)), 3);
    check("an empty range", tb_range_size(&tb, at(2, 2), at(2, 2)), 0);
    /* "one" + CRLF + "two" */
    check("across one line break", tb_range_size(&tb, at(1, 0), at(2, 3)), 8);
    /* "one\r\ntwo\r\n" + "three" */
    check("across two", tb_range_size(&tb, at(1, 0), at(3, 5)), 15);
    /* the whole document, trailing empty line included */
    check("the whole document", tb_range_size(&tb, at(1, 0), at(4, 0)),
          (int) sizeof(DOC) - 1);

    /* --- range copy --- */
    char_buffer clip;
    check("a clipboard", cb_init(&clip, 256) != NULL, 1);

    check("copying within a line", tb_range_copy(&tb, at(1, 0), at(1, 3), &clip), 3);
    check_s("  gives the bytes", clip_of(&clip), "one");

    check("copying backwards is the same range",
          tb_range_copy(&tb, at(1, 3), at(1, 0), &clip), 3);
    check_s("  and the same bytes", clip_of(&clip), "one");

    check("copying across a line break",
          tb_range_copy(&tb, at(1, 1), at(2, 2), &clip), 6);
    check_s("  keeps the break as CRLF", clip_of(&clip), "ne<R><N>tw");

    check("copying several lines",
          tb_range_copy(&tb, at(1, 0), at(3, 5), &clip), 15);
    check_s("  gives all of them", clip_of(&clip), "one<R><N>two<R><N>three");

    check("copying the whole document",
          tb_range_copy(&tb, at(1, 0), at(4, 0), &clip), (int) sizeof(DOC) - 1);
    check_s("  including the last break", clip_of(&clip),
            "one<R><N>two<R><N>three<R><N>");

    check("an empty range copies nothing",
          tb_range_copy(&tb, at(2, 2), at(2, 2), &clip), 0);
    check_s("  and leaves the clipboard empty", clip_of(&clip), "");

    /* The document is only ever read. */
    check_s("copying changed nothing", doc_of(&tb), "one/two/three/");
    check("  and did not dirty it", tb_changed(&tb) ? 1 : 0, 0);

    /* A range that will not fit must copy nothing at all: a caller that then
     * cut it would delete text the clipboard could not keep. */
    char_buffer tiny;
    cb_init(&tiny, 4);
    check("a range too big for the clipboard is refused",
          tb_range_copy(&tb, at(1, 0), at(3, 5), &tiny), -1);
    check_s("  leaving it empty, not half full", clip_of(&tiny), "");
    check("a range that just fits is copied",
          tb_range_copy(&tb, at(1, 0), at(1, 3), &tiny), 3);
    cb_destroy(&tiny);

    /* --- range delete --- */
    load(&tb);
    check("delete within a line", tb_range_del(&tb, at(2, 0), at(2, 2)) ? 1 : 0, 1);
    check_s("  takes just those bytes", doc_of(&tb), "one/o/three/");
    check("  leaves the cursor where it began", tb_ypos(&tb), 2);
    check("  at the start of the range", tb_xpos(&tb), 1);
    check("  and marks the document dirty", tb_changed(&tb) ? 1 : 0, 1);

    load(&tb);
    check("delete across a line break",
          tb_range_del(&tb, at(1, 1), at(2, 2)) ? 1 : 0, 1);
    check_s("  joins what is left of both lines", doc_of(&tb), "oo/three/");

    load(&tb);
    check("delete handed over backwards",
          tb_range_del(&tb, at(2, 2), at(1, 1)) ? 1 : 0, 1);
    check_s("  is the same deletion", doc_of(&tb), "oo/three/");

    load(&tb);
    check("delete a whole line and its break",
          tb_range_del(&tb, at(2, 0), at(3, 0)) ? 1 : 0, 1);
    check_s("  removes the line entirely", doc_of(&tb), "one/three/");

    load(&tb);
    check("delete everything", tb_range_del(&tb, at(1, 0), at(4, 0)) ? 1 : 0, 1);
    check_s("  empties the document", doc_of(&tb), "");
    check("  leaving one empty line", tb_ymax(&tb), 1);

    load(&tb);
    check("an empty range deletes nothing",
          tb_range_del(&tb, at(2, 1), at(2, 1)) ? 1 : 0, 0);
    check_s("  and changes nothing", doc_of(&tb), "one/two/three/");

    /* --- insert --- */
    load(&tb);
    tb_seek(&tb, at(2, 1));
    check("insert within a line", tb_insert(&tb, "XY", 2) ? 1 : 0, 1);
    check_s("  puts it at the cursor", doc_of(&tb), "one/tXYwo/three/");
    check("  and leaves the cursor after it", tb_xpos(&tb), 4);

    load(&tb);
    tb_seek(&tb, at(1, 3));
    check("insert with a line break", tb_insert(&tb, "A\r\nB", 4) ? 1 : 0, 1);
    check_s("  splits the line", doc_of(&tb), "oneA/B/two/three/");

    /* Text from elsewhere may use bare LFs; it has to paste the same way it
     * loads, or the line index and the text disagree about where lines end. */
    load(&tb);
    tb_seek(&tb, at(1, 3));
    check("a bare LF inserts as a line break",
          tb_insert(&tb, "A\nB", 3) ? 1 : 0, 1);
    check_s("  the same as CRLF would", doc_of(&tb), "oneA/B/two/three/");

    /* Text ending in a bare CR still ends with a line break: the CR is held
     * back until something follows it, and nothing does. */
    load(&tb);
    tb_seek(&tb, at(1, 3));
    check("a trailing bare CR is still a break",
          tb_insert(&tb, "A\r", 2) ? 1 : 0, 1);
    check_s("  so the line is split", doc_of(&tb), "oneA//two/three/");

    check("inserting nothing is refused", tb_insert(&tb, "", 0) ? 1 : 0, 0);
    check("inserting from NULL is refused", tb_insert(&tb, NULL, 4) ? 1 : 0, 0);

    /* A paste that will not fit must not land half way. */
    load(&tb);
    static char flood[8192];
    memset(flood, 'z', sizeof(flood));
    const int before = tb_used(&tb);
    check("a paste with no room is refused",
          tb_insert(&tb, flood, (int) sizeof(flood)) ? 1 : 0, 0);
    check("  and nothing of it went in", tb_used(&tb), before);
    check_s("  the document is untouched", doc_of(&tb), "one/two/three/");

    /* --- copy, delete, insert: the round trip cut and paste will make --- */
    load(&tb);
    check("cut: copy the range", tb_range_copy(&tb, at(1, 1), at(2, 2), &clip), 6);
    check("  then delete it", tb_range_del(&tb, at(1, 1), at(2, 2)) ? 1 : 0, 1);
    check_s("  the document shrank", doc_of(&tb), "oo/three/");
    int csz = 0;
    char* cbuf = cb_prefix(&clip, &csz);
    tb_seek(&tb, at(2, 5));
    check("paste: put it back somewhere else",
          tb_insert(&tb, cbuf, csz) ? 1 : 0, 1);
    check_s("  and the text arrives whole", doc_of(&tb), "oo/threene/tw/");

    /* --- the line index runs out before the characters do --- */
    /* The two buffers are bounded separately, and on a document of short lines
     * the index is the one that fills. tb_newline used to write the CRLF and
     * only then discover lb_new had no room, leaving a line break the index
     * knew nothing about -- exactly the disagreement between text and index its
     * own capacity check exists to prevent. */
    text_buffer small;
    stub_file_reset();
    check("a small document", tb_init(&small, 1, "small.txt") != NULL, 1);
    check("  has far more character room than lines",
          tb_available(&small) > lb_avai(&small.lb_) * 4, 1);

    int accepted = 0;
    while (tb_newline(&small)) {
        accepted++;
    }
    check("newlines are accepted until the index is full", accepted > 0, 1);
    check("  which is what ran out, not the characters",
          tb_available(&small) > 0, 1);
    /* "Full" means lb_new has nowhere to go, which is one slot short of empty:
     * a split writes the line's own size and the remainder to the slot after
     * it, so the last slot can never be the one being split. */
    check("  and the index has no room to split again",
          lb_can_new(&small.lb_) ? 1 : 0, 0);
    check("  with exactly the spare that requires left",
          lb_avai(&small.lb_), 1);

    const int used_before = tb_used(&small);
    check("one more is refused", tb_newline(&small) ? 1 : 0, 0);
    check("  without writing the break anyway", tb_used(&small), used_before);
    check("  so every line break has an index entry",
          tb_ymax(&small), accepted + 1);

    tb_destroy(&small);

    /* And a paste has to count the lines it brings, not just the bytes: room
     * for the characters is not room for the text. */
    text_buffer roomy;
    stub_file_reset();
    check("another small document", tb_init(&roomy, 1, "roomy.txt") != NULL, 1);
    int lines_left = lb_avai(&roomy.lb_);
    static char manylines[256];
    int mn = 0;
    for (int i = 0; i < lines_left + 2; i++) {
        manylines[mn++] = '\r';
        manylines[mn++] = '\n';
    }
    check("the paste has more breaks than there are lines",
          mn / 2 > lb_avai(&roomy.lb_), 1);
    check("  but plenty of character room", mn < tb_available(&roomy), 1);
    const int roomy_before = tb_used(&roomy);
    check("it is refused", tb_insert(&roomy, manylines, mn) ? 1 : 0, 0);
    check("  and none of it went in", tb_used(&roomy), roomy_before);

    /* One that does fit still goes in, so the check is not simply refusing. */
    check("a paste within the line budget is accepted",
          tb_insert(&roomy, "a\r\nb\r\nc", 7) ? 1 : 0, 1);
    tb_destroy(&roomy);

    /* Exactly on the boundary, which is where the spare slot matters. A paste
     * bringing as many breaks as there are free slots is one too many: the last
     * split has nowhere to put the remainder. Counting only the breaks lets it
     * through, and it then stops half way with the text already in. */
    text_buffer edge;
    stub_file_reset();
    check("a document to fill exactly", tb_init(&edge, 1, "edge.txt") != NULL, 1);
    const int slots = lb_avai(&edge.lb_);
    int en = 0;
    for (int i = 0; i < slots; i++) {
        manylines[en++] = '\r';
        manylines[en++] = '\n';
    }
    check("the paste brings exactly as many breaks as there are slots",
          en / 2, slots);
    const int edge_before = tb_used(&edge);
    check("it is refused", tb_insert(&edge, manylines, en) ? 1 : 0, 0);
    check("  without landing half of it", tb_used(&edge), edge_before);

    /* One break fewer is the largest paste that fits, and it must go in. */
    check("one break fewer is accepted",
          tb_insert(&edge, manylines, en - 2) ? 1 : 0, 1);
    check("  and all of it landed", tb_used(&edge), edge_before + en - 2);
    tb_destroy(&edge);

    cb_destroy(&clip);
    tb_destroy(&tb);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
