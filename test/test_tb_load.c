/*
 * Host tests for the load path's capacity handling.
 *
 * tb_read carves the read out of the tail of the gap with `cb->cend_ -= sz`,
 * where sz is the file size, and then reads sz bytes there. Nothing checked
 * that the file fits, so opening a file larger than the buffer put cend_ below
 * buf_ and mos_fread wrote outside the allocation entirely.
 *
 * ensure_newline is the other half: when the CR of a bare-LF normalisation
 * cannot be written it must not record a line boundary, because every consumer
 * of the line index assumes a two-byte CRLF.
 *
 * Run under ASan (see test/run.sh).
 */

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

static void check_bytes(const char* name, const char* got, int gotsz,
                        const char* want) {
    const int wantsz = (int) strlen(want);
    if (gotsz == wantsz && memcmp(got, want, (size_t) wantsz) == 0) {
        fprintf(stderr, "PASS  %-52s %d bytes\n", name, gotsz);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d bytes, want %d\n",
                name, gotsz, wantsz);
        failures++;
    }
}

static int doc_line_is(text_buffer* tb, int line, const char* want) {
    const tb_pos p = { line, 0 };
    tb_seek(tb, p);
    int sz = 0;
    const char* s = tb_suffix(tb, &sz);
    const int wsz = (int) strlen(want);

    return sz == wsz && (wsz == 0 || (s != NULL && memcmp(s, want, (size_t) wsz) == 0));
}

int main(void) {
    stub_discard_output();

    text_buffer tb;

    /* A file that comfortably fits still loads, and bare LFs become CRLF. */
    stub_file_reset();
    static const char small[] = "one\ntwo\nthree\n";
    stub_file_set_content(small, (int) sizeof(small) - 1);
    check("small file loads", tb_init(&tb, 1, "small.txt") != NULL, 1);
    check("bare LFs became lines", tb_ymax(&tb), 4);
    /* Re-baselined: this used to assert dirty. A file whose breaks are all bare
     * LFs is now written back with bare LFs, so opening it and saving it leaves
     * it byte for byte as it was -- there is nothing unsaved to warn about, and
     * the exit prompt was firing on a file the user had not touched. Mixed
     * endings still open dirty, asserted below, because those really are
     * rewritten by a save. */
    check("an LF-only file opens clean", tb_changed(&tb) ? 1 : 0, 0);

    /* The line index itself, line by line. Everything above reads the *number*
     * of lines or saves the document back, and both survive an index in which
     * one line's length has been added to the next -- the text is intact and
     * only navigation is wrong. The load walk builds these lengths, so they are
     * what a test of it has to look at. */
    {
        static const char* want[] = { "one", "two", "three" };
        for (int i = 0; i < 3; i++) {
            tb_pos p = { i + 1, 0 };
            tb_seek(&tb, p);
            const split_line ln = tb_curr_line(&tb);
            const int n = ln.psz_ + ln.ssz_;
            const int w = (int) strlen(want[i]);
            char label[32];
            label[0] = ' '; label[1] = ' ';
            memcpy(label + 2, want[i], (size_t) w);
            memcpy(label + 2 + w, " is that long", 14);
            check(label, n, w);
        }
    }
    tb_destroy(&tb);

    /* A file larger than the buffer opens anyway now: it goes to the store and
     * memory holds a window on it. It used to be refused, and the refusal is
     * what this checked -- along with not reading past the end of the
     * allocation, which ASan still watches. */
    stub_file_reset();
    text_buffer probe;
    if (!tb_init(&probe, 1, NULL)) {
        fprintf(stderr, "probe init failed\n");

        return 2;
    }
    const int cap = tb_size(&probe);
    tb_destroy(&probe);

    const int toobig = cap + 64;
    char* big = malloc(toobig);
    if (big == NULL) {
        fprintf(stderr, "malloc failed\n");

        return 2;
    }
    /* Lines, so there is something for the index to hold. A single line longer
     * than a chunk is the one shape paging cannot take, and it has its own
     * test in test_paging.c. */
    for (int i = 0; i < toobig; i++) {
        big[i] = ((i % 20) == 19) ? '\n' : 'a';
    }
    stub_file_set_content(big, toobig);
    check("an oversized file opens", tb_init(&tb, 1, "big.txt") != NULL, 1);
    check("  paged, rather than all in memory", tb_used(&tb) < toobig, 1);
    check("  with the first line readable",
          tb_curr_line(&tb).ssz_, 19);
    tb_destroy(&tb);
    free(big);

    /* Exactly at capacity is still fine -- the refusal must not be off by one. */
    stub_file_reset();
    char* exact = malloc(cap);
    if (exact == NULL) {
        fprintf(stderr, "malloc failed\n");

        return 2;
    }
    memset(exact, 'a', cap);
    stub_file_set_content(exact, cap);
    const int ok = tb_init(&tb, 1, "exact.txt") != NULL;
    check("a file exactly filling the buffer loads", ok, 1);
    if (ok) {
        check("and it is completely full", tb_available(&tb), 0);
        tb_destroy(&tb);
    }
    free(exact);

    /* A file that exactly fills the buffer and is all bare LFs.
     *
     * This used to be a file AED could not represent. Every break was turned
     * into a CRLF on the way in, so a file already filling the buffer had no
     * room for a single one of the added carriage returns -- and recording the
     * line boundaries anyway would have left tb_suffix and tb_up subtracting
     * two from lines that were one byte long. It loaded as a single 992 byte
     * line, which is the whole document unreachable.
     *
     * A document keeps its own break length now, so this one is stored exactly
     * as it arrived and nothing has to fit that was not already there. It has
     * more lines than the index has slots, so it pages -- and pages correctly,
     * giving every line back and saving byte for byte. */
    stub_file_reset();
    char* lfs = malloc(cap);
    if (lfs == NULL) {
        fprintf(stderr, "malloc failed\n");

        return 2;
    }
    memset(lfs, '\n', cap);
    stub_file_set_content(lfs, cap);
    const int loaded = tb_init(&tb, 1, "lfs.txt") != NULL;
    check("all-LF file at capacity still loads", loaded, 1);
    if (loaded) {
        check("  with every one of its lines", tb_ymax(&tb), cap + 1);
        check("  and its breaks kept as they were, one byte each", tb.elen_, 1);
        check("  clean, because nothing was changed", tb_changed(&tb) ? 1 : 0, 0);

        /* And it goes back out as it came in. */
        tb_set_fname(&tb, "/lfs.out", 8);
        check("  saving it works", tb_save(&tb) ? 1 : 0, 1);
        int saved_len = 0;
        const char* saved = stub_file_content("/lfs.out", &saved_len);
        check("    giving back every byte", saved_len, cap);
        check("      unchanged",
              saved != NULL && memcmp(saved, lfs, (size_t) cap) == 0, 1);
        tb_destroy(&tb);
    }
    free(lfs);

    /* --- a document keeps the breaks it arrived with --- */
    {
        /* Nothing is converted on the way in or back on the way out. The line
         * index carries one break length for the whole document, so a file of
         * one kind is held as it is, and only a file with both has to be
         * normalised -- which is the one case that still opens dirty, because
         * saving really does rewrite it. */
        static const char lf_doc[] = "alpha\nbeta\ngamma\n";
        static const char crlf_doc[] = "alpha\r\nbeta\r\ngamma\r\n";
        static const char mixed_doc[] = "alpha\r\nbeta\ngamma\r\n";

        stub_file_reset();
        stub_file_set_content(lf_doc, (int) sizeof(lf_doc) - 1);
        check("a document of bare line feeds", tb_init(&tb, 4, "lf.txt") != NULL, 1);
        check("  is held one byte to a break", tb.elen_, 1);
        check("  and counted as three lines and the empty one", tb_ymax(&tb), 4);
        check("  reading the second", doc_line_is(&tb, 2, "beta"), 1);
        check("  clean on open", tb_changed(&tb) ? 1 : 0, 0);
        tb_destroy(&tb);

        stub_file_reset();
        stub_file_set_content(crlf_doc, (int) sizeof(crlf_doc) - 1);
        check("a document of CRLF", tb_init(&tb, 4, "crlf.txt") != NULL, 1);
        check("  is held two bytes to a break", tb.elen_, 2);
        check("  with the same lines", tb_ymax(&tb), 4);
        check("  reading the second", doc_line_is(&tb, 2, "beta"), 1);
        check("  clean on open", tb_changed(&tb) ? 1 : 0, 0);
        tb_destroy(&tb);

        stub_file_reset();
        stub_file_set_content(mixed_doc, (int) sizeof(mixed_doc) - 1);
        check("a document of both kinds", tb_init(&tb, 4, "mixed.txt") != NULL, 1);
        check("  is normalised to CRLF", tb.elen_, 2);
        check("  with the lines it had", tb_ymax(&tb), 4);
        check("  reading the second", doc_line_is(&tb, 2, "beta"), 1);
        check("  and dirty, because saving will rewrite it",
              tb_changed(&tb) ? 1 : 0, 1);
        tb_destroy(&tb);

        /* Typing into a bare-LF document adds a bare-LF break, so it stays one
         * kind all the way through. */
        stub_file_reset();
        stub_file_set_content(lf_doc, (int) sizeof(lf_doc) - 1);
        check("a bare-LF document to type into", tb_init(&tb, 4, "lf.txt") != NULL, 1);
        tb_pos at = { 2, 4 };
        tb_seek(&tb, at);
        check("  a new line splits it", tb_newline(&tb) ? 1 : 0, 1);
        check("    giving one more line", tb_ymax(&tb), 5);
        check("    and the halves read right",
              doc_line_is(&tb, 2, "beta") && doc_line_is(&tb, 3, ""), 1);
        tb_set_fname(&tb, "/typed.out", 10);
        check("  saving it works", tb_save(&tb) ? 1 : 0, 1);
        int tlen = 0;
        const char* tsaved = stub_file_content("/typed.out", &tlen);
        check("    and it is still bare line feeds", tlen,
              (int) sizeof(lf_doc) - 1 + 1);
        check("      with no carriage return anywhere",
              tsaved != NULL && memchr(tsaved, '\r', (size_t) tlen) == NULL, 1);
        tb_destroy(&tb);
    }

    /* Line endings survive the round trip.
     *
     * The buffer is always CRLF -- the line index subtracts 2 for a break -- so
     * what a file was written with has to be remembered separately and put back
     * on the way out. Without that, opening a Unix file and saving it silently
     * rewrote every line ending in it. */
    {
        stub_file_reset();
        static const char unix_txt[] = "alpha\nbeta\ngamma\n";
        stub_file_set_content(unix_txt, (int) sizeof(unix_txt) - 1);
        text_buffer lf;
        check("LF file loads", tb_init(&lf, 1, "unix.txt") != NULL, 1);
        check("LF file is clean on open", tb_changed(&lf) ? 1 : 0, 0);

        stub_file_reset();
        check("LF file saves", tb_save(&lf) ? 1 : 0, 1);
        check_bytes("LF file goes back out unchanged",
                    stub_file_bytes(), stub_file_size(), unix_txt);
        tb_destroy(&lf);
    }

    {
        stub_file_reset();
        static const char dos_txt[] = "alpha\r\nbeta\r\n";
        stub_file_set_content(dos_txt, (int) sizeof(dos_txt) - 1);
        text_buffer crlf;
        check("CRLF file loads", tb_init(&crlf, 1, "dos.txt") != NULL, 1);
        check("CRLF file is clean on open", tb_changed(&crlf) ? 1 : 0, 0);

        stub_file_reset();
        check("CRLF file saves", tb_save(&crlf) ? 1 : 0, 1);
        check_bytes("CRLF file goes back out unchanged",
                    stub_file_bytes(), stub_file_size(), dos_txt);
        tb_destroy(&crlf);
    }

    /* Mixed endings cannot round trip: the document is one thing or the other
     * on the way out. It goes out as CRLF, which rewrites the LF-only lines --
     * a real change to the file, so this one is dirty from the moment it opens
     * and the exit prompt is telling the truth. */
    {
        stub_file_reset();
        static const char mixed_txt[] = "alpha\r\nbeta\ngamma\r\n";
        stub_file_set_content(mixed_txt, (int) sizeof(mixed_txt) - 1);
        text_buffer mixed;
        check("mixed file loads", tb_init(&mixed, 1, "mixed.txt") != NULL, 1);
        check("mixed endings open dirty", tb_changed(&mixed) ? 1 : 0, 1);

        stub_file_reset();
        check("mixed file saves", tb_save(&mixed) ? 1 : 0, 1);
        check_bytes("mixed endings are normalised to CRLF",
                    stub_file_bytes(), stub_file_size(),
                    "alpha\r\nbeta\r\ngamma\r\n");
        tb_destroy(&mixed);
    }

    /* The gap sits wherever the cursor is, so the document reaches the writer as
     * two segments split at an arbitrary point. Saving with the cursor parked
     * mid-document has to produce the same bytes as saving from the start. */
    {
        stub_file_reset();
        static const char unix2[] = "alpha\nbeta\ngamma\ndelta\n";
        stub_file_set_content(unix2, (int) sizeof(unix2) - 1);
        text_buffer mid;
        check("LF file loads for the split test",
              tb_init(&mid, 1, "unix2.txt") != NULL, 1);

        tb_pos p;
        p.line = 3;
        p.x = 2;
        tb_seek(&mid, p);

        stub_file_reset();
        check("split-gap file saves", tb_save(&mid) ? 1 : 0, 1);
        check_bytes("LF round trip holds with the gap mid-document",
                    stub_file_bytes(), stub_file_size(), unix2);
        tb_destroy(&mid);
    }

    /* A lone CR is text, not a line ending, and an LF-only file can contain
     * one. Only the CR of a CRLF is dropped on the way out. */
    {
        stub_file_reset();
        static const char cr_txt[] = "a\rb\nc\n";
        stub_file_set_content(cr_txt, (int) sizeof(cr_txt) - 1);
        text_buffer cr;
        check("LF file with a lone CR loads", tb_init(&cr, 1, "cr.txt") != NULL, 1);

        stub_file_reset();
        check("lone-CR file saves", tb_save(&cr) ? 1 : 0, 1);
        check_bytes("the lone CR survives the round trip",
                    stub_file_bytes(), stub_file_size(), cr_txt);
        tb_destroy(&cr);
    }

    /* A name longer than the buffer holds. It arrives from argv, and tb_load
     * used to copy strlen(fname) bytes into a fixed 256 -- an overflow waiting
     * for a long enough path. ASan is what makes this test bite. */
    {
        static char longname[TB_FNAME_MAX + 200];
        memset(longname, 'n', sizeof(longname) - 1);
        longname[sizeof(longname) - 1] = 0;

        stub_file_reset();
        static const char small2[] = "x\r\n";
        stub_file_set_content(small2, (int) sizeof(small2) - 1);
        text_buffer big;
        check("a very long file name loads",
              tb_init(&big, 1, longname) != NULL, 1);
        check("  and the name is clamped, not overflowed",
              (int) strlen(tb_fname(&big)), TB_FNAME_MAX - 1);
        tb_destroy(&big);
    }

    /* --- the break length, decided once, on both ways in --- */
    {
        /*
         * A document is read by one of two paths: straight into memory when it
         * fits, or a chunk at a time into the store when it does not. Both have
         * to answer the same question -- is a break here one byte or two --
         * and both used to answer it with their own copy of the same eight
         * lines. They now share break_len_at and break_len_agrees.
         *
         * elen_ is the number every later edit reads to know how much a break
         * is, so both paths are checked for both kinds, and for a file holding
         * both, which is refused as it stands and read again normalised.
         *
         * 1 KiB pages; 64 KiB does not.
         */
        static char lf[3000];
        static char crlf[4000];
        int a = 0;
        int b = 0;
        for (int i = 0; i < 250; i++) {
            for (int k = 0; k < 8; k++) {
                lf[a++] = (char) ('a' + ((i + k) % 26));
                crlf[b++] = (char) ('a' + ((i + k) % 26));
            }
            lf[a++] = '\n';
            crlf[b++] = '\r';
            crlf[b++] = '\n';
        }

        static text_buffer t;
        struct { const char* text; int len; int kb; int want; const char* what; }
        cases[] = {
            { lf,   a, 64, 1, "bare feeds, read into memory" },
            { lf,   a,  1, 1, "bare feeds, read into the store" },
            { crlf, b, 64, 2, "CRLF, read into memory" },
            { crlf, b,  1, 2, "CRLF, read into the store" },
        };
        for (int i = 0; i < 4; i++) {
            stub_file_reset();
            stub_file_set_content(cases[i].text, cases[i].len);
            check(cases[i].what, tb_init(&t, cases[i].kb, "/e.txt") != NULL, 1);
            check("  the break length it settles on", t.elen_, cases[i].want);
            check("    and it opens clean", tb_changed(&t) ? 1 : 0, 0);
            tb_destroy(&t);
        }

        /* A file of both kinds, down each path. Refused as it stands, read
         * again with every break made CRLF, and dirty because that is a
         * change the file on disk does not have yet. */
        static char both[4000];
        int c = 0;
        for (int i = 0; i < 250; i++) {
            for (int k = 0; k < 8; k++) {
                both[c++] = (char) ('a' + ((i + k) % 26));
            }
            if (i % 2 == 0) {
                both[c++] = '\r';
            }
            both[c++] = '\n';
        }
        const int kbs[2] = { 64, 1 };
        for (int i = 0; i < 2; i++) {
            stub_file_reset();
            stub_file_set_content(both, c);
            check(i == 0 ? "both kinds, read into memory"
                         : "both kinds, read into the store",
                  tb_init(&t, kbs[i], "/b.txt") != NULL, 1);
            check("  every break is CRLF now", t.elen_, 2);
            check("    so it opens dirty", tb_changed(&t) ? 1 : 0, 1);
            tb_destroy(&t);
        }
    }

    /* --- documents whose shape the buffer cannot hold --- */
    {
        /*
         * A document is paged by keeping part of it in memory and the rest in
         * a store, and the part in memory is whole lines. So a *single line*
         * larger than the buffer cannot be held at all, however large the
         * store is -- there is no line boundary to split it on.
         *
         * That is a real file: anything minified, a long row of data, a log
         * written without breaks. What the editor owes is to say so rather
         * than to open it half way or misbehave, and TB_TOO_LARGE is what the
         * caller turns into a message. Nothing tested it.
         *
         * The line either side of the limit is checked too, so the refusal is
         * pinned to the size of the buffer rather than to any line being long.
         */
        static char ONE[130000];
        static text_buffer big;

        memset(ONE, 'x', 40000);
        ONE[40000] = '\r';
        ONE[40001] = '\n';
        stub_file_reset();
        stub_file_set_content(ONE, 40002);
        check("a 40,000 byte line in a 48 KiB buffer",
              tb_init(&big, 48, NULL) != NULL, 1);
        check("  opens", tb_load(&big, "/long.txt"), TB_OK);
        check("    as one line and the empty one after it", tb_ymax(&big), 2);
        tb_destroy(&big);

        memset(ONE, 'x', 120000);
        stub_file_reset();
        stub_file_set_content(ONE, 120000);
        check("a 120,000 byte line with no break at all",
              tb_init(&big, 48, NULL) != NULL, 1);
        check("  is refused", tb_load(&big, "/huge.txt"), TB_TOO_LARGE);
        tb_destroy(&big);

        /* And through tb_init, which is how the editor opens a file: it
         * reports the refusal by handing back nothing. */
        stub_file_reset();
        stub_file_set_content(ONE, 120000);
        check("  and opening it outright gives nothing back",
              tb_init(&big, 48, "/huge.txt") == NULL ? 1 : 0, 1);
    }

    /* --- an empty document --- */
    {
        /* Saving nothing writes nothing, and the file is still created. An
         * editor that dropped the file instead would lose a deliberate
         * emptying of one. */
        stub_file_reset();
        stub_file_set_content("", 0);
        static text_buffer e;
        check("an empty document opens", tb_init(&e, 8, "/e.txt") != NULL, 1);
        check("  with one line in it", tb_ymax(&e), 1);
        check("  and nothing in that line", tb_used(&e), 0);

        tb_set_fname(&e, "/eo.txt", 7);
        check("  saving it works", tb_save(&e) ? 1 : 0, 1);
        int sz = -1;
        stub_file_content("/eo.txt", &sz);
        check("    writing no bytes", sz, 0);
        check("    to a file that is there", stub_file_exists("/eo.txt") ? 1 : 0, 1);
        tb_destroy(&e);
    }

    /* --- an open that fails must not empty the file --- */
    {
        /*
         * What this is about happened to a real file. A 16 KB source opened
         * as a blank screen, and was a blank file on the card afterwards.
         *
         * tb_load opens with FA_OPEN_ALWAYS, which makes a file that is not
         * there. So the retry behind it is only ever reached when the open
         * failed for some *other* reason -- and it used to retry with
         * FA_CREATE_ALWAYS, which truncates. A failure to open became a
         * deletion of the reader's work, and the editor then showed them the
         * empty result as though that were the file.
         *
         * FA_CREATE_NEW refuses a file that already exists, so the retry can
         * still make a new file and can no longer empty an old one.
         */
        static const char KEEP[] = "one\r\ntwo\r\nthree\r\n";
        stub_file_reset();
        stub_file_add("/keep.txt", KEEP, (int) sizeof(KEEP) - 1);

        static text_buffer t;
        tb_init(&t, 8, NULL);
        stub_file_fail_opens(1);        /* the first open, and only that one */
        const tb_result r = tb_load(&t, "/keep.txt");

        check("an open that fails is reported", r != TB_OK ? 1 : 0, 1);

        /* Asked of the card rather than of the editor: the question is what is
         * on disk afterwards. */
        {
            static char back[64];
            const char rh = mos_fopen("/keep.txt", FA_READ);
            const int got = rh != 0
                          ? (int) mos_fread(rh, back, sizeof(back)) : -1;
            if (rh != 0) {
                mos_fclose(rh);
            }
            check("  and the file still holds what it held",
                  got, (int) sizeof(KEEP) - 1);
            check("    byte for byte",
                  got == (int) sizeof(KEEP) - 1
                      && memcmp(back, KEEP, (size_t) got) == 0 ? 1 : 0, 1);
        }
        tb_destroy(&t);

        /* And the retry still does the job it is there for. */
        stub_file_reset();
        static text_buffer fresh;
        tb_init(&fresh, 8, NULL);
        check("a name with no file behind it still opens",
              tb_load(&fresh, "/new.txt"), TB_OK);

        /*
         * And puts nothing on the card.
         *
         * Opening used to pass FA_OPEN_ALWAYS, which creates the file there
         * and then -- before the reader has typed a character. So `aed
         * notes.c` for a notes.c that something else had deleted left a
         * nought-byte notes.c behind, and a nought-byte file where a 16 KB one
         * used to be is indistinguishable from the editor having emptied it.
         * It cost a long hunt through the save path for a fault that was not
         * there. The file appears when it is saved.
         */
        {
            static FILINFO info;
            check("  without making one", ffs_stat(&info, "/new.txt"),
                  FR_NO_FILE);
            const char h = mos_fopen("/new.txt", FA_READ);
            if (h != 0) {
                mos_fclose(h);
            }
            check("    which the card agrees about", h != 0 ? 1 : 0, 0);
        }
        /* Saving is what makes it, and then it is there. */
        check("  saving the new document", tb_save(&fresh) ? 1 : 0, 1);
        {
            static FILINFO info;
            check("    and now the card has it",
                  ffs_stat(&info, "/new.txt"), FR_OK);
        }
        tb_destroy(&fresh);

        /*
         * A file the card can see and will not open is a failure, never a new
         * document. mos_fopen answers zero to both, so absence is established
         * by asking the card rather than assumed from the refusal -- and the
         * wrong guess here is the one that hands the reader an empty screen
         * carrying the name of a file that is still on the card, ready for the
         * next save to write nothing over it.
         */
        stub_file_reset();
        stub_file_add("/there.txt", KEEP, (int) sizeof(KEEP) - 1);
        static text_buffer locked;
        tb_init(&locked, 8, NULL);
        stub_file_fail_opens(1);        /* the open, and nothing after it */
        check("a file that is there but will not open is a failure",
              tb_load(&locked, "/there.txt"), TB_NO_FILE);
        check("  and it keeps no name to save over",
              tb_valid_file(&locked) ? 1 : 0, 0);
        {
            static char back[64];
            const char rh = mos_fopen("/there.txt", FA_READ);
            const int got = rh != 0
                          ? (int) mos_fread(rh, back, sizeof(back)) : -1;
            if (rh != 0) {
                mos_fclose(rh);
            }
            check("    and the file is untouched", got,
                  (int) sizeof(KEEP) - 1);
        }
        tb_destroy(&locked);

        /* The same when the card cannot say either way. */
        stub_file_reset();
        stub_file_add("/there.txt", KEEP, (int) sizeof(KEEP) - 1);
        static text_buffer erred;
        tb_init(&erred, 8, NULL);
        stub_file_fail_opens(1);
        stub_stat_result(FR_DISK_ERR);
        check("a card that cannot answer is a failure too",
              tb_load(&erred, "/there.txt"), TB_NO_FILE);
        stub_stat_result(0);
        tb_destroy(&erred);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
