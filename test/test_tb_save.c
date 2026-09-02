/*
 * Host tests for tb_save().
 *
 * Saving used to live in the controller (cmd_ops.c's save_file), which meant the
 * model could load a file it had no way to write and `fname_` had two owners.
 * These tests build a document through the model's own API and assert on the
 * bytes handed to mos_fwrite, so they cover the write path itself -- not just
 * that some function was called.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "text_buffer.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-46s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-46s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void check_bytes(const char* name, const char* got, int gotsz,
                        const char* want) {
    const int wantsz = (int) strlen(want);
    if (gotsz == wantsz && memcmp(got, want, wantsz) == 0) {
        fprintf(stderr, "PASS  %-46s %d bytes\n", name, gotsz);
    } else {
        fprintf(stderr, "FAIL  %-46s got %d bytes %.*s, want %d bytes %s\n",
                name, gotsz, gotsz, got, wantsz, want);
        failures++;
    }
}

static void put_str(text_buffer* tb, const char* s) {
    for (; *s; s++) {
        tb_put(tb, *s);
    }
}

int main(void) {
    stub_discard_output();

    text_buffer tb;

    /* A model with no backing file cannot be saved. */
    stub_file_reset();
    if (!tb_init(&tb, 4, 8, NULL)) {
        fprintf(stderr, "tb_init failed\n");

        return 2;
    }
    check("no filename -> tb_save refuses", tb_save(&tb), 0);
    check("no filename -> nothing opened", stub_file_opens(), 0);

    /* Naming it makes it saveable, and the model owns the name. */
    tb_set_fname(&tb, "test.txt", 8);
    check("tb_set_fname -> tb_valid_file", tb_valid_file(&tb) ? 1 : 0, 1);
    check_bytes("tb_fname round-trips", tb_fname(&tb),
                (int) strlen(tb_fname(&tb)), "test.txt");

    /* Cursor at the end: the whole document is in the prefix. */
    put_str(&tb, "hello");
    check("editing marks the buffer dirty", tb_changed(&tb) ? 1 : 0, 1);
    stub_file_reset();
    check("tb_save succeeds", tb_save(&tb) ? 1 : 0, 1);
    check_bytes("writes the document", stub_file_bytes(), stub_file_size(), "hello");
    check("tb_save clears the dirty flag", tb_changed(&tb) ? 1 : 0, 0);
    check("file was closed", stub_file_closes(), 1);

    /* Cursor in the middle: prefix and suffix must be written in order, with
     * the gap between them skipped. This is the part that would silently
     * corrupt a save if the two segments were mishandled. */
    tb_home(&tb);
    tb_next(&tb);
    tb_next(&tb);
    stub_file_reset();
    check("tb_save with a mid-document cursor", tb_save(&tb) ? 1 : 0, 1);
    check_bytes("prefix+suffix rejoin in order", stub_file_bytes(),
                stub_file_size(), "hello");

    /* Multi-line, cursor mid-buffer: CRLF must survive the split. */
    tb_end(&tb);
    tb_newline(&tb);
    put_str(&tb, "world");
    tb_home(&tb);
    stub_file_reset();
    check("multi-line save", tb_save(&tb) ? 1 : 0, 1);
    check_bytes("newline preserved across the gap", stub_file_bytes(),
                stub_file_size(), "hello\r\nworld");

    /* A failed open must not report success, and must not leave the buffer
     * looking saved when it is not. */
    put_str(&tb, "z");
    stub_file_reset();
    stub_file_fail_open(1);
    check("open failure -> tb_save fails", tb_save(&tb) ? 1 : 0, 0);
    check("open failure -> nothing written", stub_file_size(), 0);
    check("open failure -> still dirty", tb_changed(&tb) ? 1 : 0, 1);

    tb_destroy(&tb);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
