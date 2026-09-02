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

int main(void) {
    stub_discard_output();

    text_buffer tb;

    /* A file that comfortably fits still loads, and bare LFs become CRLF. */
    stub_file_reset();
    static const char small[] = "one\ntwo\nthree\n";
    stub_file_set_content(small, (int) sizeof(small) - 1);
    check("small file loads", tb_init(&tb, 4, 1, "small.txt") != NULL, 1);
    check("bare LFs became lines", tb_ymax(&tb), 4);
    check("normalising marks the buffer dirty", tb_changed(&tb) ? 1 : 0, 1);
    tb_destroy(&tb);

    /* A file larger than the buffer must be refused, not read past the end of
     * the allocation. Under ASan the unfixed code dies here. */
    stub_file_reset();
    text_buffer probe;
    if (!tb_init(&probe, 4, 1, NULL)) {
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
    check("oversized file is refused", tb_init(&tb, 4, 1, "big.txt") == NULL, 1);
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
    const int ok = tb_init(&tb, 4, 1, "exact.txt") != NULL;
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
    const int loaded = tb_init(&tb, 4, 1, "lfs.txt") != NULL;
    check("all-LF file at capacity still loads", loaded, 1);
    if (loaded) {
        check("no CRLF room -> no line boundaries recorded", tb_ymax(&tb), 1);
        check("buffer is full", tb_available(&tb), 0);
        tb_destroy(&tb);
    }
    free(lfs);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
