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
    tb_destroy(&tb);

    /* A file larger than the buffer must be refused, not read past the end of
     * the allocation. Under ASan the unfixed code dies here. */
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
    memset(big, 'a', toobig);
    stub_file_set_content(big, toobig);
    check("oversized file is refused", tb_init(&tb, 1, "big.txt") == NULL, 1);
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

    /* A file that exactly fills the buffer and is all bare LFs passes the size
     * check, then has no room for a single normalising CR. ensure_newline must
     * not record line boundaries it cannot represent as CRLF: doing so would
     * leave tb_suffix and tb_up subtracting 2 from lines that are 1 byte long. */
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
        check("no CRLF room -> no line boundaries recorded", tb_ymax(&tb), 1);
        check("buffer is full", tb_available(&tb), 0);
        tb_destroy(&tb);
    }
    free(lfs);

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

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
